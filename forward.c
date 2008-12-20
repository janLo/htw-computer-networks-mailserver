#include <string.h>

#include "forward.h"
#include "config.h"
#include "connection.h"


struct fwd_mail {
    int           fwd_writeback_fd;
    char *        fwd_from;
    char *        fwd_to;
//    body_line_t * fwd_body_head;
//    body_line_t * fwd_body_pos;
}; 

char * fwd_send_host(char * addr) {
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

int fwd_queue(body_line_t * body, char * from, char * to){
    int          new      = 0;
    char *       host     = fwd_send_host(to);
    fwd_mail_t * new_mail = malloc(sizeof(fwd_mail_t));

    new = conn_new_fwd_socket(host, new_mail);
new_mail->fwd_writeback_fd = new;
    free(host);
    return 0;
}

int fwd_process_input(char * msg, ssize_t msglen, fwd_mail_t * fwd){
    return 0;
}

int fwd_free_mail(fwd_mail_t * fwd) {
    return 0;
}
