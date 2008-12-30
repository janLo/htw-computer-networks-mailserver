/* pop3.c
 *
 * The pop3 module for the "Beleg Rechnernetze/Kommunikationssysteme".
 *
 * author: Jan Losinski
 * date: 16.12.08
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "connection.h"
#include "config.h"
#include "pop3.h"
#include "mailbox.h"

enum pop3_states {
   NEW,
   AUTH,
   START,
   QUIT
};

enum pop3_checks {
    CHECK_OK,
    CHECK_FAIL
};


struct pop3_session {
   int              session_writeback_fd;
   conn_writeback_t session_writeback_fkt;
   enum pop3_states session_state;
   int              session_authorized;
   char *           session_user;
   mailbox_t *      session_mailbox;
};











static int pop3_write_client_msg(conn_writeback_t write_fkt, int write_fd, char * msg, ...){
    static char buf[2048];
    va_list arglist;
    va_start(arglist, msg);
    size_t len;

    /* TODO macht seltsamme sachen */
    len = snprintf(buf, 2048, msg, arglist);

    if(write_fkt(write_fd, buf, len) <= 0)
	return POP3_FAIL;

    return POP3_OK;
}

static inline int pop3_write_client_term(conn_writeback_t write_fkt, int write_fd) {
    static char * term = ".\r\n";
    if(write_fkt(write_fd, term, 3) <= 0)
        return POP3_FAIL;

    return POP3_OK;
}

static inline int pop3_write_client_list(){
    return 0;
}
static inline int pop3_write_client_mail(conn_writeback_t write_fkt, int write_fd, char * mail, size_t len){
    return 0;
}



static inline int pop3_test_cmd(const char * line, const char * expected) {
    size_t len = strlen(expected);
    if ( 0 == strncmp(line, expected, len) )
        return POP3_OK;
    return POP3_FAIL;
}



pop3_session_t * pop3_create_session(int writeback_socket, int ssl){
    pop3_session_t * new = NULL;
    conn_writeback_t fkt = ( ssl ? conn_writeback_ssl : conn_writeback);
    const char * myhost = "localhost";

    if (NULL != config_get_hostname()) {
	myhost = config_get_hostname();
    }

    //GREET
    if (POP3_FAIL == pop3_write_client_msg(fkt, writeback_socket, POP3_MSG_GREET, myhost)){
	printf("Writeback fail on pop3 init");
        return new;
    }

    new = malloc(sizeof(pop3_session_t)); 

    new->session_writeback_fd  = writeback_socket;
    new->session_writeback_fkt = fkt;
    new->session_state         = NEW;
    new->session_authorized    = 0;
    new->session_user          = NULL;
    new->session_mailbox       = NULL;

    return new;
}

pop3_session_t * pop3_create_normal_session(int writeback_socket){
    return pop3_create_session(writeback_socket, 0);
}

pop3_session_t * pop3_create_ssl_session(int writeback_soket) {
    return NULL;
}

static inline int pop3_strip_newlines(char * orig, int origlen) {
    if (orig[origlen - 1] == '\n') 
        orig[origlen - 1] = '\0';
    if (orig[origlen - 2] == '\r') 
        orig[origlen - 2] = '\0';

    return strlen(orig);
}

static inline int pop3_check_passwd(char * msg, int msglen, pop3_session_t * session){
    int l;

    l = pop3_strip_newlines(msg, msglen);

    if( config_verify_user_passwd(session->session_user, msg) ){
        session->session_authorized = 1;
        return CHECK_OK;
    }

    return CHECK_FAIL;
}

static inline int pop3_check_user(char * msg, int msglen, pop3_session_t * session){
    char * buf;
    int l;

    l = pop3_strip_newlines(msg, msglen);

    if (config_has_user(msg)) {
        buf = malloc(sizeof(char) * (l + 1));
        memcpy(buf, msg, l+1);
        session->session_user = buf;
        return CHECK_OK;
    }
    return CHECK_FAIL;
}

static inline int pop3_init_mbox(pop3_session_t * session){
    if (config_user_locked(session->session_user)) {
        return CHECK_FAIL;
    }
    if (NULL == (session->session_mailbox = mbox_init(session->session_user))) {
        return CHECK_FAIL;
    }
    config_lock_mbox(session->session_user);
    return CHECK_OK;
}



int pop3_process_input(char * msg, ssize_t msglen, pop3_session_t * session){
    switch (session->session_state) {
        case NEW:
            if ( CHECK_OK == pop3_check_user(msg, msglen, session) ) {
                session->session_state = AUTH;
                pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_USER_OK);
            } else {
                pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_USER_ERR);
            }
            break;
        case AUTH:
            if ( CHECK_OK == pop3_check_passwd(msg, msglen, session) ) {
                session->session_state = START;
                if ( CHECK_OK == pop3_init_mbox(session) ) {
                    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_OK);
                } else {
                    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_ERR_LOCK);
                    return CONN_QUIT;
                }
            } else {
                pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_ERR_PASS);
            }
            break;
        case START:
            break;
        case QUIT:
            return CONN_QUIT;
            break;
    }
    return 0;
}

int pop3_destroy_session(pop3_session_t * session){
    return 0;
}
