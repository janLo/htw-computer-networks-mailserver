/* connection.c
 *
 * The connection module for the "Beleg Rechnernetze/Kommunikationssysteme".
 *
 * (c) 2008, 2009 by Jan Losinski
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
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

//! The size of the read buffer
#define BUF_SIZE 4096*4

//! Typedef for the mysocket struct
typedef struct mysocket mysocket_t;

//! Function prototype for session init functions
typedef void* (* data_init_t   )(int socket);

//! Function prototype for session deleter functions
typedef int   (* data_deleter_t)(void * data);

//! Function prototype for data handling functions
typedef int   (* data_handler_t)(char * msg, ssize_t msglen, void * data);

//! Function prototype for data reading functions
typedef int   (* read_handler_t)(mysocket_t*);


//! Socket and assigned data
/*! 
 * This covers all socket assigned data like callbacks, session data, ...
 */
struct mysocket {
    int            socket_fd;		//!< The file descriptor.
    void *         socket_data;         //!< The assigned data or (in the special case of listening sockets the accept callback).
    read_handler_t socket_read_handler; //!< The callback to read data from the socket.
    data_handler_t socket_data_handler; //!< The callback to deal with readed data.
    data_deleter_t socket_data_deleter; //!< The callback to destroy session assigned data.
    int            socket_is_ssl;       //!< Flag that indicate if we have ssl or not.
};


//! Socket list
/*!
 * This is a linked list, holding all sockets of the app.
 */
typedef struct mysocket_list {
    mysocket_t             list_socket; //!< The sochet.
    struct mysocket_list * list_next;   //!< The pointer to the next element.
} mysocket_list_t;

//! The readed data
/*!
 * Former a own struct, now a alias for body_line_t.
 */
typedef  body_line_t readbuf_t;

mysocket_list_t * socketlist_head = NULL; //! Head of the socket list

//! A static buffer to read data.
char  readbuf[BUF_SIZE];


//! Helper for addrinfo
/*!
 * This is a simple helper to build a addrinfo struct with the host of the app
 * and a given port.
 * \param port The port to bind.
 * \return The addrinfo struct.
 */
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
/*!
 * This creates a listening socket on the given port. It will be bound to the
 * hostname of the app or INET ANY if none specified.
 * \param port The port to bind.
 * \return The new created listening socket or -1 on failture.
 */
int conn_setup_listen(const char * port) {
    int new_sock = 0;

    if((new_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1){
	ERROR_SYS2("socket creation, Port: %s", port);
	return -1;
    }

    struct addrinfo* info = conn_build_addrinfo(port);

    int so_opt = 1;
    setsockopt(new_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&so_opt, sizeof(so_opt));

    if(bind(new_sock, info->ai_addr, info->ai_addrlen) == -1){
	ERROR_SYS2("socket binding, Port: %s", port);
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
/*!
 * This creates a new mysocket struct and pack it in a mysocket_list struct. On
 * failture, NULL will be returned.
 * \param fd           The file descriptor of the new socket.
 * \param data         The data of the new session.
 * \param is_ssl       Flag to indicate if a session is ssl or not.
 * \param read_handler The callback to read data.
 * \param data_handler The callback to deal with data.
 * \param data_deleter The callback to delete data.
 * \return The new element or NULL on failture.
 */
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
/*!
 * Append a mysocket_list_t element to the list of sockets.
 * \param new The new socket element to append.#
 * \return CONN_OK on success, CONN_FAIL else.
 */
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
/*! 
 * Delete the socket element with the given file descriptor from the socket
 * list and frees all resources. The data delete callback of the socket object
 * to free all the session assigned data.
 * \param fd The file descriptor of the element to delete.
 * \return CONN_OK on success, CONN_FAIL else.
 */
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
	    if (elem->list_socket.socket_is_ssl >= 0 && NULL != elem->list_socket.socket_data_deleter) {
                if(1 == elem->list_socket.socket_is_ssl) {
                    data = ((ssl_data_t*)elem->list_socket.socket_data)->ssl_data;
		} else {
		    data = elem->list_socket.socket_data;
		}
		(elem->list_socket.socket_data_deleter)(data);
	    }
	    close(fd);
            prev = elem;
	    free(elem);
	    return CONN_OK;
	} else {
            prev = elem;
        }
	elem = prev->list_next;
    }

    INFO_MSG("Socket closed");
    return CONN_FAIL;
}

//! Cut the readed data at the newline char
/*! 
 * This cuts the readed buffer in tokens on the newline char and puts the token
 * in a readbuf_t list.
 * If the buffer is empty a fingle readbuf_t element will be returned with
 * line_len set to <1.
 * \param readbuf The readed buffer.
 * \param l       The length of the data.
 * \return the readbuf_t list or NULL.
 */
static inline readbuf_t * conn_tokenize_output(char * readbuf, ssize_t l) {
    ssize_t len = l;
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

        next = strtok(NULL, "\n");
    }
        
    return ret;
}

//! Read some normal data
/*!
 * Reads some data from a normal socket, cut them in a readbuf_t list and
 * returns this.
 * \param socket The socket to read from.
 * \return The readbuf_t list.
 */
static inline readbuf_t * conn_read_normal_buff(int socket){
    ssize_t len;
    len = read(socket,readbuf,BUF_SIZE-1);
    return conn_tokenize_output(readbuf, len);
}

//! Read some ssl data
/*!
 * This reads some data from a ssl enabled socket, cut them in a readbuf_t list
 * and returns it.
 * \param socket The socket to read from.
 * \param ssl    The ssl_data struct of the socket.
 * \return The readbuf_t list.
 */
static inline readbuf_t * conn_read_ssl_buff(int socket, ssl_data_t * ssl){
    ssize_t len;

    len = ssl_read(socket, ssl->ssl_ssl, readbuf, BUF_SIZE-1);
    return conn_tokenize_output(readbuf, len);
}

//! Read some normal data
/*!
 * This reads and processes normal data from a given socket. This function will
 * be called if the main event loop think there is some data to read from a
 * specific not-ssl socket.
 * It also calls the callback for processing the data at the right module. If
 * the size of the data is 0, the socketd element will be removed from the list, 
 * destroyed and the socked closed.
 * \param socket The socket element to read data from.
 * \return 0 in every case.
 */
int conn_read_normal(mysocket_t * socket){
    readbuf_t * buf = conn_read_normal_buff(socket->socket_fd);
    readbuf_t * tmp = NULL;
    int status      = CONN_CONT;;

    if (0 < buf->line_len){

        while (NULL != buf) {
            tmp = buf;
            buf = buf->line_next;


            if (CONN_CONT == status) {
                status = socket->socket_data_handler(tmp->line_data, 
                        tmp->line_len, socket->socket_data);
            }

            if (CONN_QUIT == status) {
                INFO_MSG("End connection");
                conn_delete_socket_elem(socket->socket_fd);
            }

            free(tmp->line_data);
            free(tmp);
        }
    } else {
        conn_delete_socket_elem(socket->socket_fd);
        free(buf);
    }
    return 0;
}

//! Find ssl data
/*!
 * Searches for the ssl data of a given fd in the socket list. The data will be
 * returned.
 * \param socket The fd to search.
 * \return The ssl data or NULL on failture.
 */
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


//! Read some ssl data
/*!
 * This reads and processes normal data from a given socket. This function will
 * be called if the main event loop think there is some data to read from a
 * specific ssl socket.
 * It also calls the callback for processing the data at the right module. If
 * the size of the data is 0, the socketd element will be removed from the list, 
 * destroyed and the socked closed.
 * \param socket The socket element to read data from.
 * \return 0 in every case.
 */
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
                status = socket->socket_data_handler(tmp->line_data, 
                        tmp->line_len, data->ssl_data);
            }

            if (CONN_QUIT == status) {
                INFO_MSG("Close connection");
		ssl_quit_client(data->ssl_ssl, socket->socket_fd);
                conn_delete_socket_elem(socket->socket_fd);
            }

            free(tmp->line_data);
            free(tmp);
        }
    } else {
        conn_delete_socket_elem(socket->socket_fd);
        free(buf);
    }
    return 0;
}

//! Accept a normal connection
/*!
 * This accepts a normal client connection on a listening socket. The socket
 * data will be initialized with the right callback and with the socket queued
 * to the socket list.
 * \param socket The socket struct to accept.
 * \return CONN_OK on succes, CONN_FAIL else.
 */
int conn_accept_normal_client(mysocket_t * socket){
    int               new;
    struct            sockaddr sa;
    size_t            len = sizeof(sa);
    mysocket_list_t * elem;
    void *            data;
    data_init_t       init_handler = (data_init_t)socket->socket_data;

    INFO_MSG("Accept new Client");
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

//! Accept a normal connection
/*!
 * This accepts a ssl client connection on a listening socket. It also performs
 * the ssl handshake. The socket data will be initialized with the right 
 * callback and with the socket queued
 * to the socket list.
 * \param socket The socket struct to accept.
 * \return CONN_OK on succes, CONN_FAIL else.
 */
int conn_accept_ssl_client(mysocket_t * socket){
    int               new;
    struct            sockaddr sa;
    size_t            len = sizeof(sa);
    ssl_data_t      * data;
    data_init_t       init_handler = (data_init_t)socket->socket_data;
    mysocket_list_t * elem;



    INFO_MSG("Accept new SSL Client");
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
/*!
 * Initialize the listening sockets for SMTP, POP3 and POP3S. The sokets will
 * also be queued to the socket list.
 * This should be called only once on app start.
 * \return CONN_OK on success, CONN_FAIL else.
 */
int conn_init(){
    mysocket_list_t * elem;
    int fd;

    /* Setup SMTP */
    INFO_MSG("Init SMTP socket");
    fd = conn_setup_listen(config_get_smtp_port());
    elem = conn_build_socket_elem(fd, smtp_create_session, -1, conn_accept_normal_client, 
            (data_handler_t)smtp_process_input, (data_deleter_t)smtp_destroy_session);
    if (NULL == elem) 
        return CONN_FAIL;
    socketlist_head = elem;

    /* Setup POP3 */
    INFO_MSG("Init POP3 socket");
    fd = conn_setup_listen(config_get_pop_port());
    elem->list_next = conn_build_socket_elem(fd, pop3_create_normal_session, -1, conn_accept_normal_client, 
	(data_handler_t)pop3_process_input, (data_deleter_t)pop3_destroy_session);
    elem = elem->list_next;
    if (NULL == elem) 
        return CONN_FAIL;

    /* Setup POP3S */
    INFO_MSG("Init POP3S socket");
    fd = conn_setup_listen(config_get_pops_port());
    elem->list_next = conn_build_socket_elem(fd, pop3_create_ssl_session, -1, conn_accept_ssl_client, 
	(data_handler_t)pop3_process_input, (data_deleter_t)pop3_destroy_session);
    elem = elem->list_next;
    if (NULL == elem) 
        return CONN_FAIL;
    
    INFO_MSG("Connection init ok");

    return CONN_OK;
}

//! Do the connection wait loop
/*! 
 * This is the main event loop. It performs a select() on all sockets in the
 * socket list to watch them for input. If the select returns it iterate trough
 * the socket list and calls the read_handler callback for each active socket.
 * This should be called once in the app to perform the client handling. If it
 * returns, the app should be quit.
 * \return 0 in any case.
 */
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
                (elem->list_socket.socket_read_handler)(&(elem->list_socket));
                i++;
            }
            elem = elem->list_next;
        }
    }

    return 0;
}

//! Close all connections
/*! This claoses all elements in the socket list. It can be used to cleanup
 * after SIGTERM etc.
 * \return CONN_OK.
 */
int conn_close() {
    mysocket_list_t * elem = socketlist_head;
    int fd;

    while (elem != NULL){
	fd = elem->list_socket.socket_fd;
	elem = elem->list_next;
        conn_delete_socket_elem(fd);
    }

    INFO_MSG("All connections closed");

    return CONN_OK;
}

//! Write data to the ssl client
/*!
 * This is a function to write data back to a ssl client.
 * \param fd  The socket to the client.
 * \param buf The data to write.
 * \param len The length of the data.
 * \return >0 on success.
 */
ssize_t conn_writeback_ssl(int fd, char * buf, ssize_t len) {
    ssl_data_t * data = conn_find_ssl_data(fd);

    return ssl_write(fd, data->ssl_ssl, buf, len);
}

//! Write data to the client
/*!
 * This is a function to write data back to a client.
 * \param fd  The socket to the client.
 * \param buf The data to write.
 * \param len The length of the data.
 * \return >0 on success.
 */
ssize_t conn_writeback(int fd, char * buf, ssize_t len) {
    return write(fd, buf, len);
}

//! Queue a socket of a forward
/*! This is used by the mail forward module to queu the socket to the relay host
 * in the socket list. 
 * If it is in the list it can be watched for input in the main loop to reduce
 * blocking.
 * \param fd   The fd to the relay host.
 * \param data The data of the forward session.
 * \return CONN_OK on success, CONN_FAIL else.
 */
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

//! Connect to relay host
/*! This is used to connect to a relayhost. 
 * \param host The hostname of the relayhost.
 * \param port The port of the connection.
 * \return The new socket to the relayhost on success or CONN_FAIL.
 */
static inline int conn_connect_socket(char * host, char * port) {
    int new = 0;
    struct addrinfo hints;
    struct addrinfo* res;

    INFO_MSG3("connecting to host: %s:%s", host, port);

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

//! Create a new forward socket and queue it 
/*!
 * This create a new forward socket by connecting to the relayhost and queues 
 * it to the socket list. 
 * \param host The hostname of the relayhost.
 * \param data The data for the new connection.
 * \return The new socket to the relayhost on success or CONN_FAIL.
 */
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
