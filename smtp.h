typedef struct smtp_session smtp_session_t;

#define SMTP_CONT 0
#define SMTP_QUIT -1
#define SMTP_OK 0
#define SMTP_FAIL -1


smtp_session_t * smtp_create_session(int writeback_fd);
int smtp_destroy_session(smtp_session_t * session);

int smtp_process_input(char * msg, int msglen, smtp_session_t *);


#define SMTP_MSG_GREET          "%d %s SMTP Relay by Jan Losinski\r\n"
#define SMTP_MSG_AUTH           "%d \r\n"
#define SMTP_MSG_AUTH_OK        "%d Authentication successful\r\n"
#define SMTP_MSG_AUTH_NOK       "%d Error: authentication failed\r\n"
#define SMTP_MSG_RESET          "%d RESET Accepted, Resetted\r\n"
#define SMTP_MSG_NOOP           "%d NOOP Ok, I'm here\r\n"
#define SMTP_MSG_BYE            "%d Bye Bye.\r\n"
#define SMTP_MSG_QUIT           "%d Ok, I try to forward the Message:\r\n"
#define SMTP_MSG_NOT_IMPL       "%d %s Command not implemented\r\n"
#define SMTP_MSG_SYNTAX         "%d Syntax error or command unrecognized\r\n"
#define SMTP_MSG_SYNTAX_ARG     "%d syntax error in parameters or arguments\r\n"
#define SMTP_MSG_SENDER         "%d Sender %s OK\r\n"
#define SMTP_MSG_HELLO          "%d Hello %s!\r\n"
#define SMTP_MSG_EHLLO          "%d-Hello %s!\r\n"
#define SMTP_MSG_EHLLO_EXT1     "%d AUTH PLAIN\r\n"
#define SMTP_MSG_RCPT           "%d RCPT %s seems to be OK\r\n"
#define SMTP_MSG_DATA           "%d Waiting for Data, End with <CR><LF>.<CR><LF>\r\n"
#define SMTP_MSG_DATA_ACK       "%d Message Accepted and forwarded\r\n"
#define SMTP_MSG_DATA_FAIL      "%d Message Accepted but forward failed\r\n"
#define SMTP_MSG_MEM            "%d Requested mail action aborted: exceeded storage allocation\r\n"
#define SMTP_MSG_SEQ            "%d Bad Sequence of Commands\r\n"
#define SMTP_MSG_PROTO          "%d-Poto: %s\r\n"
