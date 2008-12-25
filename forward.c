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

//! get thr host for sending
/*! 
 * This determines the host where the mail should be relayed to. This can be
 * the Relayhost given as commandline option, the host after the @ char of the
 * mail adress (if it is a host) or the dns mx entry of the name after the @
 * sign.
 * The host will be given as char sequence and musst be freed manual!
 * \param addr The mail adress the host should determined for.
 * \return The pointer to host as char sequence.
 * \sa smtp_resolve_mx()
 */
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

//! Copys existing body lines
/*! 
 * This copys existing body lines into new memory and returns the pointer to the
 * new head element. This is used to transfer body lines from the smtp to the
 * forward module. In future releases (if anyday this will be performant and
 * MULTITHREADED or better MULTIPROCESSED, this will be solved better.
 * \param old The old body lines to copy.
 * \return The head element of the new body lines.
 */
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
/*! 
 * Delete the whole given body line list.
 * \param start The head element of the List to delete.
 */
inline void fwd_delete_body_lines(body_line_t * start){
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


//! Extracts the replycode from the string
/*!
 * extracts the preply code of the server from a message. If this is a multi-line
 * message and the current is not the last (reply code is delimeted by '-' and
 * not by ' ') 0 will be returned. In any case the code can be extracted, the
 * code will be returned. If not, 0 will also be returned.
 * \param buff The message from the server as char sequence.
 * \return The reply code ort 0 (see above).
 */
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

//! Write a command
/*! 
 * This writes a command to the server. Internal thr command string will be
 * concatenated with the argument and sended to the given fd.
 * \param remote_fd The file descriptor to write the command.
 * \param command   The command string as char sequence (null terminated).
 * \param data      The argument of the command (also null terminated).
 * \return FWD_OK on success, FWD_FAIL else.
 */
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

//! Check the reply of the server
/*! 
 * This checks rhe reply code of a server message. If thr code isas the given
 * expected, R_OK will be returned. If the code cannot be extracted, R_NOP will
 * returned. If the server indicates that the command will be successful on
 * retry, R_RETRY will returnd and on a reply code indicates a fatal error
 * R_FAIL will b returned.
 * \param buf      The message from the server as char sequence.
 * \param expected The expected reply code.
 * \return One of the values of the response enum (see above).
 */
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

//! Write the body to th server
/*!
 * This writes the whole list of given body lines to the server. The lines are
 * sended in wingle write()s, each terminated with <cr><lf>. At the and a
 * '.<cr><lf>' will be sended to indicate the end of the message.
 * \param remote_fd The fd to write thr body.
 * \param body      The head element of the body line list.
 * \return FWD_OK on success, FWD_FAIL else.
 */
static inline int fwd_write_body(int remote_fd, body_line_t * body) {
    body_line_t * elem = body;
   // static char buff[4069];

    printf("write body\n");

    while (NULL != elem) {
        
        if (NULL == elem->line_data || 0 == elem->line_len){
            continue;
        }

        if ( conn_writeback(remote_fd, elem->line_data, elem->line_len + 2) <= 0){
            ERROR_SYS("Writing on Remote Socket");
            printf("Error while Writing on remote socket\n");
            return FWD_FAIL;
        }
        printf("fwd_data: %s\n",elem->line_data);
        
        elem = elem->line_next;
    }

    if ( conn_writeback(remote_fd, ".\r\n", 3) <= 0){
        ERROR_SYS("Writing on Remote Socket");
        printf("Error while Writing on remote socket\n");
        return FWD_FAIL;
    }

    return FWD_OK;
}

//! Prepend a body line element
/*!
 * This prepends a body line element on at the mail body head element of the
 * given forward mail.
 * \param msg The content for the element to prepend.
 * \param len The length of the message without null terminator.
 * \param fwd The structure of the forward mail.
 */
static inline void fwd_prepend_body_msg(char * msg, int len, fwd_mail_t * fwd) {
    body_line_t * new_head   = malloc(sizeof(fwd_mail_t));
    new_head->line_len       = len;
    new_head->line_data      = malloc(sizeof(char) * len+1);
    new_head->line_next      = fwd->fwd_body;
    fwd->fwd_body            = new_head;
    new_head->line_data[len] = '\0';
    memcpy(new_head->line_data, msg, len);
}

//! Builds a new body line element and prepend
/*! 
 * This builds a new body line element of two char sequences. The sequences
 * will simple be concartenated. The new element will be prepended bevore the
 * head element of the given forward structure.
 * \param msg1 The first line part (null terminated).
 * \param msg2 The second line part (null terminated).
 * \param fwd  The structure of the forward mail.
 * \sa fwd_prepend_body_msg()
 */
static inline void fwd_build_and_prepend_body_msg(const char * msg1, const char * msg2, fwd_mail_t * fwd){
    static char buff[1024];
    int  len1, len2;

    len1 = strlen(msg1);
    len2 = strlen(msg2);
    memcpy(buff,        msg1, len1);
    memcpy(buff + len1, msg2, len2);
    fwd_prepend_body_msg(buff, len1 + len2, fwd);
}

//! Build and send a error message 
/*!
 * This will be invoked if a forward has failed to send a error report back to
 * the initial sender. It builds the error mail existing of the error message
 * from the relay host and the original mail data.
 * The error mail will then be forwarded to the sender.
 * \param fwd     The failed forward message.
 * \param msg     The error message from the server.
 * \param msglen  The length of the error message (without null terminator).
 */
static inline void fwd_return_failture(fwd_mail_t * fwd, char * msg, int msglen) {
    const char * myhost = "localhost";
    int len1, len2;
    char * mailaddr;

    if (NULL != config_get_hostname()) {
        myhost = config_get_hostname();
    }

    len1 = strlen(myhost);
    len2 = strlen(FWD_POSTMASTER);

    mailaddr = malloc(sizeof(char) *(len1+len2+2));
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

//! Queue a message to forward
/*! 
 * This is the start point for a forward message. In this function the
 * structure will be builded and the connection to the relayhost created.
 * The given data will all be copied, so it can be freed outside.
 * \param body     The body line list of the mail.
 * \param from     The mail adress of the sender.
 * \param to       The adress of the recipient.
 * \param failable A flag to tell the forwarder if a error mail should be sent
 *                 back if thr forward fails.
 * \return FWD_OK on success, FWD_FAIL else.
 */
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

//! Process inpot of a forward connection
/*!
 * This is the callack which is executed is any data is readable from a forward
 * fd. It tracks the state of the forard message and processes the readed data
 * right. It alo triggers the actions related to some data at a specivic state
 * and manage the transitions between other states.
 * It also handles the error if one will be raised in any action.
 * \param msg    The readed data as char sequence.
 * \param msglen The length of the data without null terminator.
 * \param fwd    The structure of the forward mail.
 * \return CONN_CONT if the connection should be alive abter this data, 
 *         CONN_QUIT if the connection module schould close the socket and free
 *         all related resources.
 */
int fwd_process_input(char * msg, ssize_t msglen, fwd_mail_t * fwd){
    int status;

    printf("input for fwd!\n");

    switch (fwd->fwd_state) {

        /* New con, no data readed before, waiting for 220 greet, send HELO */
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

        /* Greet reded, HELO sended, wait for 250 reply, send MAIL FROM */
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

        /* MAIL FROM sended, wait for wait for 250 reply, send RCPT TO */
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

        /* RCPT TO sended, wait for wait for 250 reply, send DATA */
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

        /* DATA sended, wait for wait for 354  reply, send the body data */
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

        /* Body data dended, wait for wait for 250 reply, send QUIT */
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

        /* QUIT sended, wait for wait for 221 reply, return with CONN_QUIT */
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
/*! 
 * This is the cleanup callback for the connection module. it will be called if
 * a connection will be cleaned up. Every heap data of the forward mail should
 * be freed here.
 * \param fwd The structure of the forward mail.
 * \return FWD_OK in any vases for the moment.
 */
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
