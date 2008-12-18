#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


#include "fail.h"
#include "connection.h"
#include "config.h"

#define BUF_SIZE 4096 


//! Socket and assigned data
typedef struct mysocket {
    int    socket_fd;
    void * socket_data;
    int (* socket_handler)(int socket, void * data);
    int (* socket_data_deleter)(void * data);
} mysocket_t;

//! Socket lisz
typedef struct mysocket_list {
    mysocket_t             list_socket;
    struct mysocket_list * list_next;
} mysocket_list_t;

typedef struct readbuf {
    ssize_t readbuf_len;
    char *  readbuf_data;
} readbuf_t;

mysocket_list_t * socketlist_head = NULL; //! Head of the socket list
char  readbuf[BUF_SIZE];


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
static inline mysocket_list_t * conn_build_socket_elem(int fd, void * data, 
	int (* handler)(int, void *), int (* data_deleter)(void *)){
    mysocket_list_t * elem;

    elem = malloc(sizeof(mysocket_list_t));
    
    elem->list_next = NULL;
    elem->list_socket.socket_data = data;
    elem->list_socket.socket_handler = handler;
    elem->list_socket.socket_data_deleter = data_deleter;
    
    if ( -1 == (elem->list_socket.socket_fd = fd) ) {
        free(elem);
        return NULL;
    }
    return elem;
}

//! Append a socket list element
static inline int conn_append_socket_elem(mysocket_list_t * new){
    mysocket_list_t * elem;

    if (NULL == new) {
        return CONN_FAIL;
    }

    elem = socketlist_head;
    if (NULL == elem) {
        socketlist_head = elem;
    } else {
        while (NULL != elem) {
            if (NULL == elem->list_next) {
                elem->list_next = new;
                break;
            } else {
                elem = elem->list_next;
            }
        }
    }
    return CONN_OK;
}


//! Delete element from socket list
static inline int conn_delete_socket_elem(int fd){
    mysocket_list_t * elem;
    mysocket_list_t * prev;
    mysocket_list_t * next;

    elem = socketlist_head;
    prev = NULL;
    next = NULL;

    while (NULL != elem){
	if (fd == elem->list_socket.socket_fd){
	    next = elem->list_next;
	    if(NULL != prev){
	        prev->list_next = next;
	    } else {
		socketlist_head = next;
	    }
	    if (NULL != elem->list_socket.socket_data_deleter) {
		(elem->list_socket.socket_data_deleter)(elem->list_socket.socket_data);
	    }
	    close(fd);
	    free(elem);
	    return CONN_OK;
	}
        prev = elem;
	elem = prev->list_next;
    }

    printf("close socket");
    return CONN_FAIL;
}

//! Read some normal data
static inline readbuf_t * conn_read_normal_buff(int socket){
    ssize_t len;
    char * buf;
    readbuf_t * ret = malloc(sizeof(readbuf_t));

    ret->readbuf_len  = 0;
    ret->readbuf_data = NULL;

    len = read(socket,readbuf,BUF_SIZE);
    
    ret->readbuf_len  = len;

    if (len < 1){
        return ret;
    }

    buf = malloc(sizeof(char) * (len +1));
    buf[len] = '\0';
    memcpy(buf, readbuf, len);
    ret->readbuf_data = buf;

    return ret;
}

//! Read some smtp data
int conn_read_smtp(int socket, void *data){
    readbuf_t * buf = conn_read_normal_buff(socket);

    if (0 < buf->readbuf_len){
        /* TODO: process smtp data */
        printf("%s\n", buf->readbuf_data);
        free(buf->readbuf_data);
    } else {
        /* TODO: Close session and connection */
        printf("%i\n", buf->readbuf_len);
        conn_delete_socket_elem(socket);
    }
    free(buf);
    return 0;
}

//! Read some pop3 data
int conn_read_pop3(int socket, void *data){
    return 0;
}

//! Read some pop3s data
int conn_read_pop3s(int socket, void *data){
    return 0;
}

//! Accept a smtp connection
int conn_accept_smtp_client(int socket, void *data){
    int new;
    struct sockaddr sa;
    size_t len = sizeof(sa);
    mysocket_list_t * elem;
    
    printf("smtp\n");
    if ( -1 == (new = accept(socket, &sa, &len)) ) {
        return CONN_FAIL;
    }

    elem = conn_build_socket_elem(new, NULL, conn_read_smtp, NULL);
    if ( CONN_FAIL == conn_append_socket_elem(elem) ) {
        return CONN_FAIL;
    }
    /* TODO: create smtp session */

    return CONN_OK;
}

//! Accept a pop3 connection
int conn_accept_pop3_client(int socket, void *data){
    printf("pop3\n");
    return 0;
}

//! Accept a Pop3S connection
int conn_accept_pop3s_client(int socket, void *data){
    printf("pop3s\n");
    return 0;
}

//! Init listening connections
int conn_init(){
    mysocket_list_t * elem;
    int fd;

    /* Setup SMTP */
    fd = conn_setup_listen(config_get_smtp_port());
    elem = conn_build_socket_elem(fd, NULL, conn_accept_smtp_client, NULL);
    if (NULL == elem) 
        return CONN_FAIL;
    socketlist_head = elem;

    /* Setup POP3 */
    fd = conn_setup_listen(config_get_pop_port());
    elem->list_next = conn_build_socket_elem(fd, NULL, conn_accept_pop3_client, NULL);
    elem = elem->list_next;
    if (NULL == elem) 
        return CONN_FAIL;

    /* Setup POP3S */
    fd = conn_setup_listen(config_get_pops_port());
    elem->list_next = conn_build_socket_elem(fd, NULL, conn_accept_pop3s_client, NULL);
    elem = elem->list_next;
    if (NULL == elem) 
        return CONN_FAIL;
    
    return CONN_OK;
}

//! Do the connection wait loop
int conn_wait_loop(){
    mysocket_list_t * elem;
    fd_set rfds;
    int fd;
    int num;
    int max = 0;
    int i;
    
    while (1) {
        FD_ZERO(&rfds);

        elem = socketlist_head;
        while (elem != NULL){
            fd = elem->list_socket.socket_fd;
            FD_SET(fd, &rfds);
            if(max < fd)
                max = fd;
            elem = elem->list_next;
        }

        num = select(max + 1, &rfds, NULL, NULL, NULL);

        elem = socketlist_head;
        for (i = 0; (i < num) && elem != NULL; ){
            fd = elem->list_socket.socket_fd;
            if (FD_ISSET(fd, &rfds)) {
                printf("fd: %i\n", fd);
                (elem->list_socket.socket_handler)(fd, elem->list_socket.socket_data);
            }
            elem = elem->list_next;
        }
    }

    return 0;
}

//! Close all connections
int conn_close() {
    mysocket_list_t * elem = socketlist_head;
    int fd;

    while (elem != NULL){
	fd = elem->list_socket.socket_fd;
	elem = elem->list_next;
        conn_delete_socket_elem(fd);
    }
    return CONN_OK;
}
