#include "smtp.h"

typedef struct fdw_mail fwd_mail_t;

int fwd_queue(body_line_t * body, char * from, char * to);
int fwd_process_input(fwd_mail_t * fwd);
int fwd_free_mail(fwd_mail_t * fwd);

