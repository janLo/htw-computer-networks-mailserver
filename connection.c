/* connection.c
 *
 * The connection module for the "Beleg Rechnernetze/Kommunikationssysteme".
 *
 * author: Jan Losinski
 * date: 28.12.08
 */


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
#include "smtp.h"
#include "pop3.h"
#include "forward.h"
#include "ssl.h"

/*!
 * \defgroup connection Connection Module
 * @{
 */

#define BUF_SIZE 4096*4

typedef struct mysocket mysocket_t;
typedef void* (* data_init_t   )(int socket);
typedef int   (* data_deleter_t)(void * data);
typedef int   (* data_handler_t)(char * msg, ssize_t msglen, void * data);
typedef int   (* read_handler_t)(mysocket_t*);

//! Socket and assigned data
struct mysocket {
    int            socket_fd;
    void *         socket_data;
    read_handler_t socket_read_handler;
    data_handler_t socket_data_handler;
    data_deleter_t socket_data_deleter;
    int            socket_is_ssl;
};

//! Socket lisz
typedef struct mysocket_list {
    mysocket_t             list_socket;
    struct mysocket_list * list_next;
} mysocket_list_t;

//! The readed data
/*!
 * Former a own struct, now a alias for body_line_t.
 */
typedef  body_line_t readbuf_t;

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
static inline mysocket_list_t * conn_build_socket_elem(int fd, void * data, int is_ssl,
	read_handler_t read_handler, 
        data_handler_t data_handler, 
        data_deleter_t data_deleter){
    mysocket_list_t * elem;

    elem = malloc(sizeof(mysocket_list_t));
    
    elem->list_next                       = NULL;
    elem->list_socket.socket_data         = data;
    elem->list_socket.socket_read_handler = read_handler;
    elem->list_socket.socket_data_handler = data_handler;
    elem->list_socket.socket_data_deleter = data_deleter;
    elem->list_socket.socket_is_ssl       = is_ssl;
    
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
    void            * data;

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
                if(elem->list_socket.socket_is_ssl) {
                    data = ((ssl_data_t*)elem->list_socket.socket_data)->ssl_data;
		} else {
		    data = elem->list_socket.socket_data;
		}
		(elem->list_socket.socket_data_deleter)(data);
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

static inline readbuf_t * conn_tokenize_output(char * readbuf, ssize_t l) {
    ssize_t len;
    char * next;
    char * buf;
    readbuf_t * ret = NULL;
    readbuf_t * tmp = NULL;

    if (len < 1){
        ret = malloc(sizeof(readbuf_t));
        ret->line_data = NULL;
        ret->line_len  = len;
        ret->line_next = NULL;
        return ret;
    }

    readbuf[len] = '\0';

    next = strtok(readbuf, "\n");
    while (NULL != next){
        len = strlen(next) +1;
        buf = malloc(sizeof(char) * (len + 1));
        memcpy(buf, next, len);
        buf[len] = '\0';
        buf[len-1] = '\n';
        if(NULL == tmp){
            tmp = malloc(sizeof(readbuf_t));
            ret = tmp;
        } else {
            tmp->line_next = malloc(sizeof(readbuf_t));
            tmp = tmp->line_next;
        }

        tmp->line_len  = len;
        tmp->line_next = NULL;
        tmp->line_data = buf;

        printf("%s\n", tmp->line_data);
        next = strtok(NULL, "\n");
    }
        
    return ret;
}

//! Read some normal data
static inline readbuf_t * conn_read_normal_buff(int socket){
    ssize_t len;
    
    len = read(socket,readbuf,BUF_SIZE-1);
    return conn_tokenize_output(readbuf, len);
}

static inline readbuf_t * conn_read_ssl_buff(int socket, ssl_data_t * ssl){
    ssize_t len;

    len = ssl_read(socket, ssl->ssl_ssl, readbuf, BUF_SIZE-1);
    return conn_tokenize_output(readbuf, len);
}

//! Read some smtp data
int conn_read_normal(mysocket_t * socket){
    readbuf_t * buf = conn_read_normal_buff(socket->socket_fd);
    readbuf_t * tmp = NULL;
    int status      = CONN_CONT;;

    if (0 < buf->line_len){

        while (NULL != buf) {
            tmp = buf;
            buf = buf->line_next;


            if (CONN_CONT == status) {
                printf("process: %s\n", tmp->line_data);
                status = socket->socket_data_handler(tmp->line_data, 
                        tmp->line_len, socket->socket_data);
            }

            if (CONN_QUIT == status) {
                printf("QUIT\n");
                conn_delete_socket_elem(socket->socket_fd);
            }

            free(tmp->line_data);
            free(tmp);
        }
    } else {
        printf("%i\n", buf->line_len);
        conn_delete_socket_elem(socket->socket_fd);
        free(buf);
    }
    return 0;
}

static inline ssl_data_t * conn_find_ssl_data(int socket){
    mysocket_list_t * elem = socketlist_head;

    while (elem != NULL) {
	if (socket == elem->list_socket.socket_fd) {
	    return (ssl_data_t *)elem->list_socket.socket_data;
	}

	elem = elem->list_next;
    }
    return NULL;
}


//! Read some pop3s data
int conn_read_ssl(mysocket_t * socket){
    ssl_data_t * data = socket->socket_data;
    readbuf_t * buf   = conn_read_ssl_buff(socket->socket_fd, data);
    readbuf_t * tmp   = NULL;
    int status        = CONN_CONT;

    if (0 < buf->line_len){

        while (NULL != buf) {
            tmp = buf;
            buf = buf->line_next;


            if (CONN_CONT == status) {
                printf("process: %s\n", tmp->line_data);
                status = socket->socket_data_handler(tmp->line_data, 
                        tmp->line_len, data->ssl_data);
            }

            if (CONN_QUIT == status) {
                printf("QUIT\n");
		ssl_quit_client(data->ssl_ssl, socket->socket_fd);
                conn_delete_socket_elem(socket->socket_fd);
            }

            free(tmp->line_data);
            free(tmp);
        }
    } else {
        printf("%i\n", buf->line_len);
        conn_delete_socket_elem(socket->socket_fd);
        free(buf);
    }
    return 0;
}

//! Accept a smtp connection
int conn_accept_normal_client(mysocket_t * socket){
    int               new;
    struct            sockaddr sa;
    size_t            len = sizeof(sa);
    mysocket_list_t * elem;
    void *            data;
    data_init_t       init_handler = (data_init_t)socket->socket_data;

    printf("smtp\n");
    if ( -1 == (new = accept(socket->socket_fd, &sa, (uint32_t*)&len)) ) {
        return CONN_FAIL;
    }

    if( NULL == (data = init_handler(new)) ) {
        close(new);
        return CONN_FAIL;
    }

    elem = conn_build_socket_elem(new, data, 0,
            conn_read_normal, 
            (data_handler_t)socket->socket_data_handler,
            (data_deleter_t)socket->socket_data_deleter);
    if (NULL == elem) {
        socket->socket_data_deleter(data);
        close(new);
    }
    if ( CONN_FAIL == conn_append_socket_elem(elem) ) {
        socket->socket_data_deleter(data);
        free(elem);
        close(new);
        return CONN_FAIL;
    }

    return CONN_OK;
}

//! Accept a Pop3S connection
int conn_accept_ssl_client(mysocket_t * socket){
    int               new;
    struct            sockaddr sa;
    size_t            len = sizeof(sa);
    ssl_data_t      * data;
    data_init_t       init_handler = (data_init_t)socket->socket_data;
    mysocket_list_t * elem;



    printf("pop3s\n");
    if ( -1 == (new = accept(socket->socket_fd, &sa, (uint32_t*)&len)) ) {
	return CONN_FAIL;
    }

    data = malloc(sizeof(ssl_data_t));

    data->ssl_ssl = ssl_accept_client(new);

    elem = conn_build_socket_elem(new, data, 1,
	    conn_read_ssl,
	    (data_handler_t)socket->socket_data_handler,
	    (data_deleter_t)socket->socket_data_deleter);

    if (NULL == elem) {
	socket->socket_data_deleter(data);
	ssl_quit_client(data->ssl_ssl, new);
	close(new);
	return CONN_FAIL;
    }

    if ( CONN_FAIL == conn_append_socket_elem(elem) ) {
	socket->socket_data_deleter(data);
	free(elem);
	ssl_quit_client(data->ssl_ssl, new);
	close(new);
	return CONN_FAIL;
    }
    if( NULL == (data->ssl_data = init_handler(new)) ) {
	ssl_quit_client(data->ssl_ssl, new);
	close(new);
	return CONN_FAIL;
    }

    return CONN_OK;
}

//! Init listening connections
int conn_init(){
    mysocket_list_t * elem;
    int fd;

    /* Setup SMTP */
    fd = conn_setup_listen(config_get_smtp_port());
    elem = conn_build_socket_elem(fd, smtp_create_session, 0, conn_accept_normal_client, 
            (data_handler_t)smtp_process_input, (data_deleter_t)smtp_destroy_session);
    if (NULL == elem) 
        return CONN_FAIL;
    socketlist_head = elem;

    /* Setup POP3 */
    fd = conn_setup_listen(config_get_pop_port());
    elem->list_next = conn_build_socket_elem(fd, pop3_create_normal_session, 0, conn_accept_normal_client, 
	(data_handler_t)pop3_process_input, (data_deleter_t)pop3_destroy_session);
    elem = elem->list_next;
    if (NULL == elem) 
        return CONN_FAIL;

    /* Setup POP3S */
    fd = conn_setup_listen(config_get_pops_port());
    elem->list_next = conn_build_socket_elem(fd, pop3_create_ssl_session, 0, conn_accept_ssl_client, 
	(data_handler_t)pop3_process_input, (data_deleter_t)pop3_destroy_session);
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
                (elem->list_socket.socket_read_handler)(&(elem->list_socket));
                i++;
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

ssize_t conn_writeback_ssl(int fd, char * buf, ssize_t len) {
    ssl_data_t * data = conn_find_ssl_data(fd);

    printf("writeback (ssl): %s\n", buf);
    return ssl_write(fd, data->ssl_ssl, buf, len);
}

ssize_t conn_writeback(int fd, char * buf, ssize_t len) {
    printf("writeback: %s\n", buf);
    return write(fd, buf, len);
}

static inline int conn_queue_forward_socket(int fd, fwd_mail_t * data){
    mysocket_list_t * elem;

    elem = conn_build_socket_elem(fd, data, 0,
            (read_handler_t)conn_read_normal,
            (data_handler_t)fwd_process_input,
            (data_deleter_t)fwd_free_mail);

    if (NULL == elem) {
        fwd_free_mail(data);
        close(fd);
    }

    if ( CONN_FAIL == conn_append_socket_elem(elem) ) {
        fwd_free_mail(data);
        free(elem);
        close(fd);
        return CONN_FAIL;
    }
    return CONN_OK;
}

static inline int conn_connect_socket(char * host, char * port) {
    int new = 0;
    struct addrinfo hints;
    struct addrinfo* res;

    printf("connecting to host: %s:%s\n", host, port);

    if((new = socket(PF_INET, SOCK_STREAM, 0)) == -1){
        ERROR_SYS("socket creation");
        return CONN_FAIL;
    }

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    getaddrinfo(host, port, &hints, &res);

    if (connect(new, res->ai_addr, res->ai_addrlen) == -1) {
        ERROR_SYS("socket creation");
        return CONN_FAIL;
    }

    freeaddrinfo(res);
    return new;
}



int conn_new_fwd_socket(char * host,  void * data){
    int new = 0;
    fwd_mail_t * data_ = (fwd_mail_t*) data;

    if ( CONN_FAIL == (new = conn_connect_socket(host, "25")) ) {
        fwd_free_mail(data);
        return CONN_FAIL;
    }
    if (CONN_FAIL == conn_queue_forward_socket(new, data_)) {
        return CONN_FAIL;
    }
    return new;
}

/** @} */
