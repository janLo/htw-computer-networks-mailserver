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
   AUTH,
   START,
   QUIT
};

enum pop3_checks {
    CHECK_OK,
    CHECK_FAIL,
    CHECK_QUIT
};


struct pop3_session {
   int              session_writeback_fd;
   conn_writeback_t session_writeback_fkt;
   enum pop3_states session_state;
   int              session_authorized;
   char *           session_user;
   mailbox_t *      session_mailbox;
};

typedef  int (* pop3_command_fkt_t)(pop3_session_t*, char *);

static int pop3_check_user(pop3_session_t *, char *);
static int pop3_check_passwd(pop3_session_t *, char *);
static int pop3_quit_session(pop3_session_t * session, char * foo);

static int pop3_stat_mailbox(pop3_session_t * session, char * arg);
static int pop3_list_mailbox(pop3_session_t * session, char * arg);
static int pop3_uidl_mailbox(pop3_session_t * session, char * arg);
static int pop3_retr_mailbox(pop3_session_t * session, char * arg);
static int pop3_dele_mailbox(pop3_session_t * session, char * arg);
static int pop3_rset_mailbox(pop3_session_t * session, char * arg);
static int pop3_noop_mailbox(pop3_session_t * session, char * arg);

typedef struct pop3_command {
    char *             command_string;
    pop3_command_fkt_t command_fkt;
    int                command_has_arg;
} pop3_command_t;

static const pop3_command_t transact_commands[] = {
    {"STAT", pop3_stat_mailbox, 0},
    {"LIST", pop3_list_mailbox ,1},
    {"UIDL", pop3_uidl_mailbox ,1},
    {"RETR", pop3_retr_mailbox ,1},
    {"DELE", pop3_dele_mailbox ,1},
    {"NOOP", pop3_noop_mailbox ,0},
    {"RSET", pop3_rset_mailbox ,0},
    {"QUIT", pop3_quit_session ,0},
    {NULL,   NULL,0}
};

static const pop3_command_t auth_commands[] = {
    {"USER", pop3_check_user,   1},
    {"PASS", pop3_check_passwd, 1},
    {"QUIT", pop3_quit_session, 0},
    {NULL,   NULL,                                   0}
};


static int pop3_write_client_msg(conn_writeback_t write_fkt, int write_fd, char * msg, ...){
    static char buf[2048];
    va_list arglist;
    va_start(arglist, msg);
    size_t len;

    len = vsnprintf(buf, 2048, msg, arglist);

    if(write_fkt(write_fd, buf, len) <= 0)
	return POP3_FAIL;

    return POP3_OK;
}

static inline int pop3_write_client_term(conn_writeback_t write_fkt, int write_fd, int start_nl) {
    char * term = (start_nl ? "\r\n.\r\n" : ".\r\n");
    int    len  = (start_nl ? 5 : 3);

    if(write_fkt(write_fd, term, len) <= 0)
        return POP3_FAIL;

    return POP3_OK;
}


static inline int pop3_test_cmd(char * line, const char * expected) {
    size_t len = strlen(expected);

    config_to_upper(line, len);

    if ( 0 == strncmp(line, expected, len) )
        return POP3_OK;
    return POP3_FAIL;
}

static inline int pop3_strip_newlines(char * orig, int origlen) {
    int l = origlen;

    if (orig[origlen - 1] == '\n'){ 
        orig[origlen - 1] = '\0';
        l--;
    }

    if (orig[origlen - 2] == '\r'){
        orig[origlen - 2] = '\0';
        l--;
    }

    return l;
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



static int pop3_check_passwd(pop3_session_t * session, char * passwd){

    if (NULL == session->session_user) {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_ERR_USER);
        return CHECK_FAIL;
    }

    if( config_verify_user_passwd(session->session_user, passwd) ){
        session->session_authorized = 1;
        if ( CHECK_OK != pop3_init_mbox(session) ) {
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_ERR_LOCK);
            return CHECK_QUIT;
        }
        session->session_state = START;
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_OK);
        return CHECK_OK;
    }

    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_ERR_PASS);
    return CHECK_FAIL;
}

static int pop3_check_user(pop3_session_t * session, char * usr){
    char * buf;
    int l;

    l = strlen(usr) + 1;

    if (config_has_user(usr)) {
        buf = malloc(sizeof(char) * (l));
        memcpy(buf, usr, l);
        session->session_user = buf;
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_USER_OK);
        return CHECK_OK;
    }

    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_USER_ERR);
    return CHECK_FAIL;
}

static int pop3_quit_session(pop3_session_t * session, char * foo) {
    if (START == session->session_state) {
        if ( NULL != session->session_mailbox ) {
            mbox_close(session->session_mailbox, 1);
            session->session_mailbox = NULL;
            config_unlock_mbox(session->session_user);
        }
    }
    session->session_state = QUIT;
    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_QUIT);
    return CHECK_QUIT;
}

static int pop3_stat_mailbox(pop3_session_t * session, char * arg) {
    if (NULL == session->session_mailbox) {
        return CHECK_FAIL;
    }
    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_STAT, mbox_count(session->session_mailbox), mbox_size(session->session_mailbox));
    return CHECK_OK;
}

static int pop3_list_mailbox(pop3_session_t * session, char * arg) {
    int i;
    int l = strlen(arg);
   
    if (NULL == session->session_mailbox) {
         return CHECK_FAIL;
    }

    if (0 < l) {
        i = atoi(arg);
        if (mbox_count(session->session_mailbox) >= i &&
                0 < i &&
                ! mbox_is_msg_deleted(session->session_mailbox, i)) {
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_LIST, i, mbox_mail_size(session->session_mailbox, i));
        } else {
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_LIST_ERR);
            return CHECK_FAIL;
        }
    } else {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_LIST_OK, mbox_count(session->session_mailbox), mbox_size(session->session_mailbox)); 
        for (i = 1; i <= mbox_count(session->session_mailbox); i++){
            if (! mbox_is_msg_deleted(session->session_mailbox, i)) {
                pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_LIST_LINE, i, mbox_mail_size(session->session_mailbox, i));
            }
        }
        pop3_write_client_term(session->session_writeback_fkt, session->session_writeback_fd, 0);
    }
    return CHECK_OK;
}

static int pop3_uidl_mailbox(pop3_session_t * session, char * arg) {
    int    i;
    int    l = strlen(arg);
    char * buf;
   
    if (NULL == session->session_mailbox) {
         return CHECK_FAIL;
    }

    if (0 < l) {
        i = atoi(arg);
        if (mbox_count(session->session_mailbox) >= i &&
                0 < i &&
                ! mbox_is_msg_deleted(session->session_mailbox, i)) {
            buf = mbox_mail_uid(session->session_mailbox, i);
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_UIDL, i, buf);
            free(buf);
        } else {
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_UIDL_ERR);
            return CHECK_FAIL;
        }
    } else {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_UIDL_OK); 
        for (i = 1; i <= mbox_count(session->session_mailbox); i++){
            if (! mbox_is_msg_deleted(session->session_mailbox, i)) {
                buf = mbox_mail_uid(session->session_mailbox, i);
                pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_UIDL_LINE, i, buf);
                free(buf);
            }
        }
        pop3_write_client_term(session->session_writeback_fkt, session->session_writeback_fd, 0);
    }
    return CHECK_OK;
}

static int pop3_retr_mailbox(pop3_session_t * session, char * arg) {
    size_t l = strlen(arg);
    int i;
    char * buf;

    if (NULL == session->session_mailbox) {
        return CHECK_FAIL;
    }

    if (0 == l) {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_DELE_ERR);
        return CHECK_FAIL;
    }
    i = atoi(arg);

    if (mbox_count(session->session_mailbox) >= i &&
            0 < i &&
            ! mbox_is_msg_deleted(session->session_mailbox, i)) {

        mbox_get_mail(session->session_mailbox, i, &buf, &l);
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_RETR_OK, mbox_mail_size(session->session_mailbox, i));
        if (session->session_writeback_fkt(session->session_writeback_fd, buf, l) <= 0)
            return CHECK_FAIL;
        free(buf);
        pop3_write_client_term(session->session_writeback_fkt, session->session_writeback_fd, 1);
        return CHECK_OK;

    } else {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_RETR_ERR);
        return CHECK_FAIL;
    }
}

static int pop3_dele_mailbox(pop3_session_t * session, char * arg) {
    int l = strlen(arg);
    int i;

    if (NULL == session->session_mailbox) {
                 return CHECK_FAIL;
                     }

    if (0 == l) {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_DELE_ERR);
        return CHECK_FAIL;
    }
    i = atoi(arg);

    if (mbox_count(session->session_mailbox) >= i &&
            0 < i &&
            ! mbox_is_msg_deleted(session->session_mailbox, i)) {
        mbox_mark_deleted(session->session_mailbox, i);
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_DELE_OK, i);
        return CHECK_OK;
    } else {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_DELE_ERR);
        return CHECK_FAIL;
    }
}

static int pop3_rset_mailbox(pop3_session_t * session, char * arg) {

    if (NULL == session->session_mailbox) {
        return CHECK_FAIL;
    }

    mbox_reset(session->session_mailbox);
    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_RSET);

    return CHECK_OK;
}

static int pop3_noop_mailbox(pop3_session_t * session, char * arg) {
    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_NOOP);
    return CHECK_OK;
}

static inline char * pop3_prepare_arg(char * msg, ssize_t msglen, char * command){
    int l, c;
    char * pos;

    l = pop3_strip_newlines(msg, msglen);
    c = strlen(command);


    if (c >= l){
        return &(msg[l]);
    }

    pos = msg + c;
    l -= c;

    while (' ' == *pos && l > 0){
        l--;
        pos++;
    }

    return pos;
}

static inline int pop3_process_cmd_list(char * msg, ssize_t msglen, pop3_session_t * session, const pop3_command_t * cmds) {
    int    i    = 0;
    char * arg  = NULL;;
    int    t    = 0;

    for (i = 0; NULL != cmds[i].command_string; i++){
        if ( CHECK_OK == pop3_test_cmd(msg, cmds[i].command_string)) {
            if (NULL != cmds[i].command_fkt) {
                if (cmds[i].command_has_arg) {
                    arg = pop3_prepare_arg(msg, msglen, cmds[i].command_string);
                }

                if (CHECK_QUIT == cmds[i].command_fkt(session, arg)) {
                    return CONN_QUIT;
                }
            }
            t = 1;
            break;
        }
    }
    if (0 == t)
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, "-ERR\r\n");
    return CONN_CONT;
}


pop3_session_t * pop3_create_session(int writeback_socket, int ssl){
    pop3_session_t * new = NULL;
    conn_writeback_t fkt = ( ssl ? conn_writeback_ssl : conn_writeback);
    const char * myhost  = "localhost";

    if (NULL != config_get_hostname()) {
	myhost = config_get_hostname();
    }

    if (POP3_FAIL == pop3_write_client_msg(fkt, writeback_socket, POP3_MSG_GREET, myhost)){
	printf("Writeback fail on pop3 init");
        return new;
    }

    new = malloc(sizeof(pop3_session_t)); 

    new->session_writeback_fd  = writeback_socket;
    new->session_writeback_fkt = fkt;
    new->session_state         = AUTH;
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

int pop3_process_input(char * msg, ssize_t msglen, pop3_session_t * session){
    int ret = CONN_CONT;
    switch (session->session_state) {

        case AUTH:
            ret = pop3_process_cmd_list(msg, msglen, session, auth_commands);
            break;
        case START:
            ret = pop3_process_cmd_list(msg, msglen, session, transact_commands);
            break;
        case QUIT:
            return CONN_QUIT;
            break;
    }
    return ret;
}

int pop3_destroy_session(pop3_session_t * session){
    if (NULL != session) {
        if (NULL != session->session_mailbox) {
            mbox_close(session->session_mailbox,0);        
            config_unlock_mbox(session->session_user);
        }
        if (NULL != session->session_user) {  
            free(session->session_user);
        }
        free(session);
    }

    return 0;
}
