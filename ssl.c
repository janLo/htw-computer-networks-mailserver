/* ssl.c
 *
 * The connection-ssl module for the "Beleg Rechnernetze/Kommunikationssysteme".
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


#include <signal.h>
#include <sys/socket.h>

#include <openssl/err.h>

#include "ssl.h"


/*!
 * \defgroup ssl Connection-SSL Module
 * @{
 */

//! The CA File
#define CA_LIST "cacert.pem"

//! The passphrase for the private key
#define PASSWD  "1234"

//! The cert and the private key
#define KEYFILE "comb.pem"

//! The DH File
#define DHFILE  "dh1024.pem"

//! Static place to store the errors
BIO *bio_err=0;

//! Static buffer for the passwd of the private key
static char *pass;

//! The Ssl context
SSL_CTX * ssl_ctx;

//! A simple error and exit routine
/*!
 * \param string A error to print.
 * \rerurn Nothing.
 */
int ssl_err_exit(char *string){
    fprintf(stderr,"%s\n",string);
    exit(0);
}

//!* Print SSL errors and exit
/*!
 * \param string A error to print.
 * \rerurn Nothing.
 */
int ssl_berr_exit(char *string)  {
    BIO_printf(bio_err,"%s\n",string);
    ERR_print_errors(bio_err);
    exit(0);
}

//! Callback for the password
/*! 
 * A simple callback to copy the password of the private key in a given buffer.
 * \param buf      Buffer to copy the passwd.
 * \param num      Length of the buffer.
 * \param rwflag   Not used.
 * \param userdata Not used.
 * \return The length of the password.
 */
static int ssl_password_cb(char *buf,int num, int rwflag,void *userdata){
    if(num<strlen(pass)+1)
        return(0);
    strcpy(buf,pass);
    return(strlen(pass));
}

//! A empty sigpipe handler
/*! 
 * Used to do nothing on SIGPIPE.
 * \param x Not used.
 */
static void ssl_sigpipe_handle(int x){
}

//! Initialize the SSL context
/*!
 * This is used to initialize the global ssl context used with all SSL actions.
 * \param keyfile  The file with the Certificate and the key.
 * \param password The password of the private key.
 * \return The new global SSL context.
 */
SSL_CTX * ssl_initialize_ctx(char *keyfile, char *password){
    SSL_METHOD *meth;
    SSL_CTX *ctx;

    if(!bio_err){
	/* Global system initialization*/
	SSL_library_init();
	SSL_load_error_strings();

	/* An error write context */
	bio_err=BIO_new_fp(stderr,BIO_NOCLOSE);
    }

    /* Set up a SIGPIPE handler */
    signal(SIGPIPE,ssl_sigpipe_handle);

    /* Create our context*/
    meth=SSLv23_method();
    ctx=SSL_CTX_new(meth);

    /* Load our keys and certificates*/
    if(!(SSL_CTX_use_certificate_chain_file(ctx,
		    keyfile)))
	ssl_berr_exit("Can't read certificate file");

    pass=password;
    SSL_CTX_set_default_passwd_cb(ctx,
	    ssl_password_cb);
    if(!(SSL_CTX_use_PrivateKey_file(ctx,
		    keyfile,SSL_FILETYPE_PEM)))
	ssl_berr_exit("Can't read key file");

    /* Load the CAs we trust*/
    if(!(SSL_CTX_load_verify_locations(ctx,
		    CA_LIST,0)))
	ssl_berr_exit("Can't read CA list");
#if (OPENSSL_VERSION_NUMBER < 0x00905100L)
    SSL_CTX_set_verify_depth(ctx,1);
#endif

    return ctx;
}

//! Destroys a SSL context
/*!
 * \param ctx The SSL context to destroy.
 */
void ssl_destroy_ctx(SSL_CTX* ctx){
    SSL_CTX_free(ctx);
}

//! Loads the dh params
/*! 
 * \param ctx  The SSL context.
 * \param file The DH file.
 */
void load_dh_params(SSL_CTX *ctx, char *file) {
    DH *ret=0;
    BIO *bio;

    if ((bio=BIO_new_file(file,"r")) == NULL)
	ssl_berr_exit("Couldn't open DH file");

    ret=PEM_read_bio_DHparams(bio,NULL,NULL,
	    NULL);
    BIO_free(bio);
    if(SSL_CTX_set_tmp_dh(ctx,ret)<0)
	ssl_berr_exit("Couldn't set DH parameters");
}

//! Init the SSL module
/*! 
 * This initialize the SSL module on app atart. It shold only called once!
 * \return 0 on success, -1 else.
 */
int ssl_app_init(){
    if(NULL == (ssl_ctx = ssl_initialize_ctx(KEYFILE, PASSWD)))
	    return -1;
    load_dh_params(ssl_ctx,DHFILE);
    return 0;
}

//! Deinit the module on app close.
/*! 
 * Destroys the context on app close.
 */
void ssl_app_destroy(){
    ssl_destroy_ctx(ssl_ctx);
    load_dh_params(ssl_ctx, DHFILE);
}

//! Do the SSL handshake
/*! This performs the SSL handshake on a new connection and returns the SSL 
 * data of the new created session.
 * \param socket The socket of the new session.
 * \return The SSL data of the new session.
 */
SSL * ssl_accept_client(int socket){
    BIO *sbio;
    SSL *ssl;
    int r;

    sbio=BIO_new_socket(socket,BIO_NOCLOSE);
    ssl=SSL_new(ssl_ctx);
    SSL_set_bio(ssl,sbio,sbio);

    if((r = SSL_accept(ssl)<=0))
	ssl_berr_exit("SSL accept error");

    return ssl;
}

//! Quit a SSL client
/*!
 * \param ssl    The SSL data of the session.
 * \param socket The socket of the session.
 */
void ssl_quit_client(SSL * ssl, int socket){
    int r;

    r = SSL_shutdown(ssl);
    if(!r){
	shutdown(socket, 1);
	r = SSL_shutdown(ssl);
    }

    SSL_free(ssl);
}

//! Read data from a SSL connection
/*!
 * \param socket The socket of the connection.
 * \param ssl    The SSL data of the session.
 * \param buf    The buffer for reading.
 * \param buflen The max. length of the buffer.
 * \return >0 on success.
 */
int ssl_read(int socket, SSL * ssl, char * buf, int buflen){
    int r, e;
    BIO *io,*ssl_bio;

    io=BIO_new(BIO_f_buffer());
    ssl_bio=BIO_new(BIO_f_ssl());
    BIO_set_ssl(ssl_bio,ssl,BIO_CLOSE);
    BIO_push(io,ssl_bio);

    r=BIO_gets(io, buf ,buflen - 1);

    e = SSL_get_error(ssl,r);

    switch (e) {
	case SSL_ERROR_NONE:
	    return r;
	case SSL_ERROR_ZERO_RETURN:
	case SSL_ERROR_WANT_READ:
	case SSL_ERROR_SYSCALL:
	    return 0;
	default:
	    ssl_berr_exit("SSL read problem");
    } 


    ssl_berr_exit("SSL read problem");

    return -1;
}

//! Write data to a SSL connection
/*!
 * \param socket The socket of the connection.
 * \param ssl    The SSL data of the session.
 * \param buf    The buffer with the data.
 * \param buflen The length of the buffer.
 * \return >0 on success.
 */
int ssl_write(int socket, SSL * ssl, char * buf, int buflen){
    int r;
    BIO *io,*ssl_bio;
    static char tmp[4096*4];

    io=BIO_new(BIO_f_buffer());
    ssl_bio=BIO_new(BIO_f_ssl());
    BIO_set_ssl(ssl_bio,ssl,BIO_CLOSE);
    BIO_push(io,ssl_bio);

    if (buflen+1 > 4096*4)
	return -1;

    memcpy(tmp, buf, buflen);
    tmp[buflen] = '\0';

    if ((r=BIO_puts(io, tmp))<=0)
	ssl_err_exit("Write error");

    if (BIO_flush(io)<0)
	ssl_err_exit("Error flushing BIO");

    return r;
}

/** @} */
