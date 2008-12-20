#include <stdlib.h>

#define FWD_QUIT -1
#define FWD_CONT 0

typedef struct body_line body_line_t;
typedef struct fdw_mail  fwd_mail_t;

struct body_line {
    char * line_data;
    int    line_len;
    body_line_t * line_next;
};


int fwd_queue(body_line_t * body, char * from, char * to);
int fwd_process_input(char * msg, ssize_t msglen, fwd_mail_t * fwd);
int fwd_free_mail(fwd_mail_t * fwd);

