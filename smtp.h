typedef struct smtp_session smtp_session_t;

#define SMTP_CONT 0
#define SMTP_QUIT -1


smtp_session_t * smtp_create_session();
int smtp_destroy_session(smtp_session_t * session);

int smtp_process_input(char * msg, int msglen, smtp_session_t *);
