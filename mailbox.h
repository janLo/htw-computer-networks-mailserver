/* mailbox.h
 *
 * A mailbox module for the "Beleg Rechnernetze/Kommunikationssysteme".
 *
 * author: Jan Losinski
 * date: 13.12.08
 */

#include <stdlib.h>

#define MAILBOX_ERROR -1
#define MAILBOX_OK 0


typedef struct mailbox  mailbox_t;


void push_mail(char * user, char * data, size_t size);

const char * mbox_get_error_msg();

int mbox_init_app();

mailbox_t * mbox_init(char* user);

size_t mbox_size(mailbox_t * mbox);

int mbox_count(mailbox_t * mbox);

size_t mbox_mail_size(mailbox_t * mbox, int mailnum);

int mbox_mark_deleted(mailbox_t * mbox, int mailnum);

int mbox_get_mail(mailbox_t * mbox, int mailnum, char** buffer, size_t *buffsize) ;

void mbox_reset(mailbox_t * mbox);

void mbox_close(mailbox_t * mbox, int has_quit);

void mbox_close_app();
