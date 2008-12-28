

#define POP3_MSG_GREET		"+OK %s POP3-Server, Enter user\r\n" 	// Hostname
#define POP3_MSG_USER_OK	"+OK Please enter passwd\r\n"
#define POP3_MSG_USER_ERR	"-ERR Username not found\r\n"
#define POP3_MSG_PASS_OK	"+OK Mailbox locked\r\n"
#define POP3_MSG_PASS_ERR_PASS	"-ERR Invalid passwd\r\n"
#define POP3_MSG_PASS_ERR_LOCK	"-ERR Cannot lock mailbox\r\n"
#define POP3_MSG_STAT		"+OK %d %d\r\n"				// Count, Size
#define POP3_MSG_LIST		"+OK %d %d\r\n"				// Count, Size
#define POP3_MSG_LIST_OK	"+OK %d messages (%d Octets)\r\n"   	// Count, Size
#define POP3_MSG_LIST_ERR	"-ERR No such message\r\n"
#define POP3_MSG_RETR_OK	"+OK %d Octets\r\n"			// Msg size
#define POP3_MSG_RETR_ERR	"-ERR No such message\r\n"
#define POP3_MSG_DELE_OK	"+OK Message %d deleted\r\n"		// Msg num
#define POP3_MSG_DELE_ERR	"-ERR No such message\r\n"
#define POP3_MSG_NOOP		"+OK\r\n"
#define POP3_MSG_RSET		"+OK\r\n"
#define POP3_MSG_QUIT		"+OK Bye\r\n"

#define POP3_OK    0
#define POP3_FAIL -1


typedef struct pop3_session pop3_session_t;

pop3_session_t * pop3_create_normal_session(int writeback_socket);
pop3_session_t * pop3_create_ssl_session(int writeback_soket);
int pop3_process_input(char * msg, ssize_t msglen, pop3_session_t * data);
int pop3_destroy_session(pop3_session_t * session);

