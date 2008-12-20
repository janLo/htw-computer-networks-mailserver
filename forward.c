#include <string.h>
#include <stdio.h>

#include "forward.h"
#include "config.h"
#include "connection.h"
#include "fail.h"

#define SEND_MAXTRY 3

enum fwd_states {
    NEW,
    HELO,
    MAIL,
    RCPT,
    DATA,
    QUIT
};

struct fwd_mail {
    int             fwd_writeback_fd;
    char *          fwd_from;
    char *          fwd_to;
    enum fwd_states fwd_state; 
    body_line_t *   fwd_body_head;
    body_line_t *   fwd_body_pos;
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
        if(NULL != tmp2->line_data){
            free(tmp2->line_data);
        }
        free(tmp2);
    }
}


int fwd_queue(body_line_t * body, char * from, char * to){
    int          new      = 0;
    char *       host     = fwd_send_host(to);
    fwd_mail_t * new_mail = malloc(sizeof(fwd_mail_t));
    size_t len;

    if( -1 == (new = conn_new_fwd_socket(host, new_mail)) ) {
        return FWD_FAIL;
    }
    
    new_mail->fwd_writeback_fd = new;
    new_mail->fwd_body_head    = copy_body(body);
    new_mail->fwd_body_pos     = new_mail->fwd_body_head;

    len = strlen(from) +1;
    new_mail->fwd_from = malloc(sizeof(char) * len);
    memcpy(new_mail->fwd_from, from, len);

    len = strlen(to) +1;
    new_mail->fwd_to = malloc(sizeof(char) * len);
    memcpy(new_mail->fwd_to, to, len);

    free(host);
    return FWD_OK;
}

//! extracts the replycode from the string
int extract_status(char* buff){
  int ret = 0;
  char *pos;
  if((pos = strchr(buff, ' ')) != NULL){
    *pos = '\0';
    ret = atoi(buff);
    *pos = ' ';
  }
  return ret;
}

//! write a command
static inline int fwd_write_command(int remote_fd, char *command, char * data){
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

int fwd_process_input(char * msg, ssize_t msglen, fwd_mail_t * fwd){
    switch (fwd->fwd_state) {
        case NEW:
            break;
        case HELO:
            break;
        case MAIL:
            break;
        case RCPT:
            break;
        case DATA:
            break;
        case QUIT:
            return CONN_QUIT;
            break;

    }
    return CONN_OK;
}

int fwd_free_mail(fwd_mail_t * fwd) {
    if (NULL != fwd->fwd_to)
        free(fwd->fwd_to);
    if (NULL != fwd->fwd_from)
        free(fwd->fwd_from);
    fwd_delete_body_lines(fwd->fwd_body_head);
    free(fwd);
    return FWD_OK;
}
