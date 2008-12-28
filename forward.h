/* forward.h
 *
 * The forward module for the "Beleg Rechnernetze/Kommunikationssysteme".
 *
 * author: Jan Losinski
 * date: 28.12.08
 */


#include <stdlib.h>

#define FWD_FAIL -1
#define FWD_OK   0

#define FWD_POSTMASTER      "postmaster"
#define FWD_ERROR_REPLY1    "An error has occured while sending your mail:"
#define FWD_ERROR_REPLY2    "Your mail was:"
#define FWD_ERROR_HEAD_FROM "From: \"Mail Delivery System\" " FWD_POSTMASTER "@"
#define FWD_ERROR_HEAD_TO   "To: "
#define FWD_ERROR_HEAD_SUBJ "Subject: Undelivered Mail Returned to Sender"

typedef struct body_line body_line_t;
typedef struct fwd_mail fwd_mail_t;

struct body_line {
    char * line_data;
    int    line_len;
    body_line_t * line_next;
};


int fwd_queue(body_line_t * body, char * from, char * to, int failable);
int fwd_process_input(char * msg, ssize_t msglen, fwd_mail_t * fwd);
int fwd_free_mail(fwd_mail_t * fwd);
inline void fwd_delete_body_lines(body_line_t * start);

