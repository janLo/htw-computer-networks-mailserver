#include <string.h>
#include <stdio.h>
#include <netdb.h>


#include "forward.h"
#include "config.h"
#include "connection.h"
#include "fail.h"
#include "smtp.h"


//! Max number of retrys of a command.
#define SEND_MAXTRY 3

//! States of a forwarding mail
/*!
 * This are the states, a forwarding mail can have before, during an after the 
 * forward process.
 */
enum fwd_states {
    NEW,        /*!< This value tells that the connection to te target was created, but no datawas send or recived now. */
    HELO,       /*!< The mail have this state after sending the HELO command and waiting for reply. */
    MAIL,       /*!< The mail have this state after sending the MAIL FROM command and waiting for reply. */
    RCPT,       /*!< The mail have this state after sending the RCPT TO command and waiting for reply. */
    DATA,       /*!< The mail have this state after sending the DATA command and waiting for reply. */
    SEND,       /*!< The mail have this state after sending the mail body lines accept. */
    QUIT        /*!< This indicates, that the forward is over. */
};

//! Check results of the response code
/*!
 * This enum holds the return values of the Checking of the reply codes from the
 * Server.
 */
enum response {
    R_OK,       /*!< The reply Code was as expected. */
    R_RETRY,    /*!< The server tells vou to retry the command. */
    R_NOP,      /*!< The server has not yet submitted the final response. */
    R_FAIL      /*!< The server tells you the sending failed for some reason. */
};

//! The stucture representing a forward mail
/*!
 * This structure holds all data, related to a forwarded mail. This includes the
 * addresses, the body data, the connection fd and so on.
 * It will be initialized during fwd_queue() and freed with fwd_free_mail().
 * \sa fwd_queue(), fwd_free_mail()
 */
struct fwd_mail {
    int             fwd_writeback_fd;   /*!< The fd of the connection. */
    char *          fwd_from;           /*!< The from adress. */
    char *          fwd_to;             /*!< The to adress. */
    enum fwd_states fwd_state;          /*!< The state of the mail. */
    int             fwd_trycount;       /*!< The count of trys of the current command. */
    int             fwd_failable;       /*!< Flag to tell if a error report should be sended to sender on failture. */
    body_line_t *   fwd_body;           /*!< The body of the mail. */
}; 


static inline char * fwd_send_host(char * addr) {
    char *       buf = NULL;
    const char * pos = NULL;
    size_t       len = 0;

    pos = config_get_relayhost();

    if (NULL == pos) {
        pos = strchr(addr, '@')+1;
    } 
    len = strlen(pos) + 1;

    buf = malloc(sizeof(char) * len);
    memcpy(buf, pos, len);

    if (gethostbyname(buf) == NULL) {
        free(buf);
        buf = smtp_resolve_mx(pos);
    }
    return buf;
}

static inline body_line_t * copy_body(body_line_t* old){
    body_line_t * new  = NULL;
    body_line_t * tmp  = NULL;
    body_line_t * old_ = old;
    char *        buf  = NULL;

    while (NULL != old_) {
        if (NULL == tmp) {
        tmp = malloc(sizeof(body_line_t));
        } else {
            tmp->line_next = malloc(sizeof(body_line_t));
            tmp = tmp->line_next;
        }
        memcpy(tmp, old_, sizeof(body_line_t));
        tmp->line_next = NULL;

        if (NULL != tmp->line_data && 0 != tmp->line_len){
            buf = malloc(sizeof(char) * (tmp->line_len + 1));
            memcpy(buf, tmp->line_data, tmp->line_len);
            buf[tmp->line_len] = '\0';
            tmp->line_data = buf;
        } else {
            tmp->line_data = NULL;
        }
        if (NULL == new) {
            new = tmp;
        }
        old_ = old_->line_next;
    }
    return new;
}

//! Deletes all body lines of a forward
static inline void fwd_delete_body_lines(body_line_t * start){
    body_line_t * tmp1 = start;
    body_line_t * tmp2 = start;

    while (NULL != tmp1){
        tmp2 = tmp1;
        tmp1 = tmp1->line_next;
        if(NULL != tmp2->line_data && tmp2->line_len != 0){
            free(tmp2->line_data);
        }
        free(tmp2);
    }
}


//! extracts the replycode from the string
int extract_status(char* buff){
    int ret = 0;
    char *pos;

    if((pos = strchr(buff, ' ')) != NULL){
        if ( 3 > (pos - buff))
            return 0;
        *pos = '\0';
        ret = atoi(buff);
        *pos = ' ';
    }

    return ret;
}

//! write a command
static inline int fwd_write_command(int remote_fd, const char *command, const char * data){
    size_t  len1 = strlen(command);
    size_t  len2 = strlen(data);
    size_t  len  = len1 + len2;
    char *  buf  = malloc(sizeof(char) * len + 3);

    memcpy(buf,      command, len1);
    memcpy(buf+len1, data,    len2);

    buf[len]   = '\r';
    buf[len+1] = '\n';
    buf[len+2] = '\0';

    if ( conn_writeback(remote_fd, buf, len + 2) <= 0){
        ERROR_SYS("Writing on Remote Socket");
        printf("Error while Writing on remote socket\n");
        free(buf);
        return FWD_FAIL;
    }
    free(buf);
    return FWD_OK;
}

static inline int check_cmd_reply(char * buf, int expected) {
   int status = extract_status(buf);
   if (0 == status) {
       return  R_NOP;
   }
   if (expected == status) {
       
       return R_OK;
   }
   if(status > 499 || status < 400){
       return R_FAIL;
   }
   return R_RETRY;
}

static inline int fwd_write_body(int remote_fd, body_line_t * body) {
    body_line_t * elem = body;
    char buff[4069];

    printf("write body\n");

    while (NULL != elem) {
        
        if (NULL == elem->line_data || 0 == elem->line_len){
            continue;
        }

        memcpy(buff, elem->line_data, elem->line_len);
        buff[elem->line_len] = '\r';
        buff[elem->line_len+1] = '\n';
        buff[elem->line_len+2] = '\0';
        
        if ( conn_writeback(remote_fd, buff, elem->line_len + 2) <= 0){
            ERROR_SYS("Writing on Remote Socket");
            printf("Error while Writing on remote socket\n");
            return FWD_FAIL;
        }
        printf("fwd_data: %s\n",buff);
        
        elem = elem->line_next;
    }

    if ( conn_writeback(remote_fd, ".\r\n", 3) <= 0){
        ERROR_SYS("Writing on Remote Socket");
        printf("Error while Writing on remote socket\n");
        return FWD_FAIL;
    }

    return FWD_OK;
}

static inline void fwd_prepend_body_msg(char * msg, int len, fwd_mail_t * fwd) {
    body_line_t * new_head   = malloc(sizeof(fwd_mail_t));
    new_head->line_len       = len;
    new_head->line_data      = malloc(sizeof(char) * len+1);
    new_head->line_next      = fwd->fwd_body;
    fwd->fwd_body            = new_head;
    new_head->line_data[len] = '\0';
    memcpy(new_head->line_data, msg, len);
}

static inline void fwd_build_and_prepend_body_msg(const char * msg1, const char * msg2, fwd_mail_t * fwd){
    static char buff[1024];
    int  len1, len2;

    len1 = strlen(msg1);
    len2 = strlen(msg2);
    memcpy(buff,        msg1, len1);
    memcpy(buff + len1, msg2, len2);
    fwd_prepend_body_msg(buff, len1 + len2, fwd);
}

static inline void fwd_return_failture(fwd_mail_t * fwd, char * msg, int msglen) {
    const char * myhost = "localhost";
    int len1, len2;
    char * mailaddr;

    if (NULL != config_get_hostname()) {
        myhost = config_get_hostname();
    }

    len1 = strlen(myhost);
    len2 = strlen(FWD_POSTMASTER);

    mailaddr = malloc(len1+len2+2);
    memcpy(mailaddr, FWD_POSTMASTER, len2);
    memcpy(mailaddr + len2 + 1, myhost, len1);
    mailaddr[len2] = '@';
    mailaddr[len2 + 1 + len1] = '\0';


    fwd_prepend_body_msg(FWD_ERROR_REPLY2, strlen(FWD_ERROR_REPLY2), fwd);
    fwd_prepend_body_msg(msg, msglen, fwd);
    fwd_prepend_body_msg(FWD_ERROR_REPLY1, strlen(FWD_ERROR_REPLY1), fwd);
    fwd_prepend_body_msg(FWD_ERROR_HEAD_SUBJ, strlen(FWD_ERROR_HEAD_SUBJ), fwd);

    fwd_build_and_prepend_body_msg(FWD_ERROR_HEAD_TO,   fwd->fwd_from, fwd);
    fwd_build_and_prepend_body_msg(FWD_ERROR_HEAD_FROM, myhost,        fwd);

    fwd_queue(fwd->fwd_body, mailaddr, fwd->fwd_from, 0);
    free(mailaddr);
}

int fwd_queue(body_line_t * body, char * from, char * to, int failable){
    int          new      = 0;
    char *       host     = fwd_send_host(to);
    fwd_mail_t * new_mail = malloc(sizeof(fwd_mail_t));
    size_t len;

    if( -1 == (new = conn_new_fwd_socket(host, new_mail)) ) {
        return FWD_FAIL;
    }
    
    new_mail->fwd_writeback_fd = new;
    new_mail->fwd_state        = NEW;
    new_mail->fwd_trycount     = 0;
    new_mail->fwd_body         = copy_body(body);
    new_mail->fwd_failable     = failable;

    len = strlen(from) +1;
    new_mail->fwd_from = malloc(sizeof(char) * len);
    memcpy(new_mail->fwd_from, from, len);

    len = strlen(to) +1;
    new_mail->fwd_to = malloc(sizeof(char) * len);
    memcpy(new_mail->fwd_to, to, len);

    free(host);
    return FWD_OK;
}

int fwd_process_input(char * msg, ssize_t msglen, fwd_mail_t * fwd){
    int status;

    printf("input for fwd!\n");

    switch (fwd->fwd_state) {
        case NEW:
            status = check_cmd_reply(msg, 220);
            if (R_OK != status && R_NOP != status){
                printf("foo\n");
                status = R_FAIL;
            } else {
                const char * myhost = "localhost";

                if (NULL != config_get_hostname()) {
                    myhost = config_get_hostname();
                }
                if (FWD_FAIL == fwd_write_command(fwd->fwd_writeback_fd, "HELO ", myhost)) {
                    return CONN_QUIT;
                }
                fwd->fwd_trycount++;
                fwd->fwd_state = HELO;
            }
            break;

        case HELO:
            status = check_cmd_reply(msg, 250);
            if (R_OK == status) {
                fwd->fwd_trycount = 0;
                if (FWD_FAIL == fwd_write_command(fwd->fwd_writeback_fd, "MAIL FROM:", fwd->fwd_from)) {
                    return CONN_QUIT;
                }
                fwd->fwd_trycount++;
                fwd->fwd_state = MAIL;
            }
            if (R_RETRY == status){
                fwd->fwd_state = NEW;
            } 
            break;

        case MAIL:
            status = check_cmd_reply(msg, 250);
            if (R_OK == status) {
                fwd->fwd_trycount = 0;
                if (FWD_FAIL == fwd_write_command(fwd->fwd_writeback_fd, "RCPT TO:", fwd->fwd_to)) {
                    return CONN_QUIT;
                }
                fwd->fwd_trycount++;
                fwd->fwd_state = RCPT;
            }
            if (R_RETRY == status){
                fwd->fwd_state = HELO;
            } 
            break;

        case RCPT:
            status = check_cmd_reply(msg, 250);
            if (R_OK == status) {
                fwd->fwd_trycount = 0;
                if (FWD_FAIL == fwd_write_command(fwd->fwd_writeback_fd, "DATA", "")) {
                    return CONN_QUIT;
                }
                fwd->fwd_trycount++;
                fwd->fwd_state = DATA;
            }
            if (R_RETRY == status){
                fwd->fwd_state = MAIL;
            } 
            break;

        case DATA:
            status = check_cmd_reply(msg, 354);
            if (R_OK == status) {
                fwd->fwd_trycount = 0;
                if (FWD_FAIL == fwd_write_body(fwd->fwd_writeback_fd, fwd->fwd_body)) {
                    return CONN_QUIT;
                }
                fwd->fwd_trycount++;
                fwd->fwd_state = SEND;
            }
            if (R_RETRY == status){
                fwd->fwd_state = RCPT;
            } 
            break;

        case SEND:
            status = check_cmd_reply(msg, 250);
            if (R_OK == status) {
                fwd->fwd_trycount = 0;
                if (FWD_FAIL == fwd_write_command(fwd->fwd_writeback_fd, "QUIT", "")) {
                    return CONN_QUIT;
                }
                fwd->fwd_trycount++;
                fwd->fwd_state = QUIT;
            }
            if (R_RETRY == status){
                fwd->fwd_state = DATA;
            }    
            break;

        case QUIT:
            status = check_cmd_reply(msg, 221);
            if (R_OK == status) {
                printf("quit successful\n");
            }
            return CONN_QUIT;
            break;

    }

    if (R_FAIL == status) {
        if (fwd->fwd_failable) {
            fwd_return_failture(fwd, msg, msglen);
        }
        /* handle error mail srtuff */
        fwd->fwd_state = QUIT;
        return CONN_QUIT;
    }

    return CONN_OK;
}

//! Frees all reources assigned to a forwarded mail
int fwd_free_mail(fwd_mail_t * fwd) {
    if (NULL != fwd->fwd_to)
        free(fwd->fwd_to);
    if (NULL != fwd->fwd_from)
        free(fwd->fwd_from);
    if (NULL != fwd->fwd_body)
        fwd_delete_body_lines(fwd->fwd_body);
    free(fwd);
    printf("fwd freed\n");
    return FWD_OK;
}
