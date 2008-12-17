#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


#include "fail.h"
#include "connection.h"
#include "config.h"


//! Socket and assigned data
typedef struct mysocket {
    int    socket_fd;
    void * socket_data;
    int (* socket_handler)(int socket, void * data);
} mysocket_t;

//! Socket lisz
typedef struct mysocket_list {
    mysocket_t             list_socket;
    struct mysocket_list * list_next;
} mysocket_list_t;


mysocket_list_t * socketlist_head = NULL; //! Head of the socket list




//! Helper for addrinfo
static inline struct addrinfo * conn_build_addrinfo(const char * port){
    struct addrinfo hints;
    struct addrinfo* res = NULL;
    const char * hostname = config_get_hostname();

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(hostname, port, &hints, &res);

    return res;
}

//! Setup a listening socket
int conn_setup_listen(const char * port) {
    int new_sock = 0;

    if((new_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1){
	ERROR_SYS("socket creation");
	return -1;
    }

    struct addrinfo* info = conn_build_addrinfo(port);

    int so_opt = 1;
    setsockopt(new_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&so_opt, sizeof(so_opt));

    if(bind(new_sock, info->ai_addr, info->ai_addrlen) == -1){
	ERROR_SYS("socket binding");
	return -1;
    }

    freeaddrinfo(info);

    if(listen(new_sock, 2) == -1){
	ERROR_SYS("socket listening");
	return -1;
    }

    return new_sock;
}

//! Helper for socket list elements
static inline mysocket_list_t * conn_build_socket_elem(int fd, void * data, int (* handler)(int, void *)){
    mysocket_list_t * elem;

    elem = malloc(sizeof(mysocket_list_t));
    
    elem->list_next = NULL;
    elem->list_socket.socket_data = data;
    elem->list_socket.socket_handler = handler;
    
    if ( -1 == (elem->list_socket.socket_fd = fd) ) {
        free(elem);
        return NULL;
    }
    return elem;
}

//! Accept a smtp connection
int accept_smtp_client(int socket, void *data){
    return 0;
}

//! Accept a pop3 connection
int accept_pop3_client(int socket, void *data){
    return 0;
}

//! Accept a Pop3S connection
int accept_pop3s_client(int socket, void *data){
    return 0;
}

//! Init listening connections
int conn_init(){
    mysocket_list_t * elem;
    int fd;

    /* Setup SMTP */
    fd = conn_setup_listen(config_get_smtp_port());
    elem = conn_build_socket_elem(fd, NULL, accept_smtp_client);
    if (NULL == elem) 
        return CONN_FAIL;
    socketlist_head = elem;

    /* Setup POP3 */
    fd = conn_setup_listen(config_get_pop_port());
    elem->list_next = conn_build_socket_elem(fd, NULL, accept_pop3_client);
    elem = elem->list_next;
    if (NULL == elem) 
        return CONN_FAIL;

    /* Setup POP3S */
    fd = conn_setup_listen(config_get_pops_port());
    elem->list_next = conn_build_socket_elem(fd, NULL, accept_pop3s_client);
    elem = elem->list_next;
    if (NULL == elem) 
        return CONN_FAIL;
    
    return CONN_OK;
}

//! Do the connection wait loop
int conn_wait_loop(){
    return 0;
}
