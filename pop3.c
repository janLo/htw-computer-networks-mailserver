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
   USER,
   PASS,
   QUIT
};

struct pop3_session {
   int              session_writeback_fd;
   conn_writeback_t session_writeback_fkt;
   enum pop3_states session_states;
   int              session_authorized;
   char *           session_user;
   mailbox_t *      session_mailbox;
};











static int pop3_write_client_msg(conn_writeback_t write_fkt, int write_fd, char * msg, ...){
    static char buf[2048];
    va_list arglist;
    va_start(arglist, msg);
    size_t len;

    len = snprintf(buf, 2048, msg, arglist);

    if(write_fkt(write_fd, buf, len) <= 0)
	return POP3_FAIL;

    return POP3_OK;
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
    new->session_states        = NEW;
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

int pop3_process_input(char * msg, ssize_t msglen, pop3_session_t * data){
    return 0;
}

int pop3_destroy_session(pop3_session_t * session){
    return 0;
}
