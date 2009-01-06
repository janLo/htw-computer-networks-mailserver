#include <signal.h>
#include <sys/socket.h>

#include <openssl/err.h>

#include "ssl.h"

#define CA_LIST "cacert.pem"
#define PASSWD  "1234"
#define KEYFILE "comb.pem"
#define DHFILE  "dh1024.pem"

BIO *bio_err=0;
static char *pass;
static int ssl_password_cb(char *buf,int num, int rwflag,void *userdata);
static void ssl_sigpipe_handle(int x);
SSL_CTX * ssl_ctx;

/* A simple error and exit routine*/
int ssl_err_exit(char *string){
    fprintf(stderr,"%s\n",string);
    exit(0);
}

/* Print SSL errors and exit*/
int ssl_berr_exit(char *string)  {
    BIO_printf(bio_err,"%s\n",string);
    ERR_print_errors(bio_err);
    exit(0);
}
    static int ssl_password_cb(char *buf,int num, int rwflag,void *userdata){
	if(num<strlen(pass)+1)
	    return(0);
	strcpy(buf,pass);
	return(strlen(pass));
    }

static void ssl_sigpipe_handle(int x){
}

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

void ssl_destroy_ctx(SSL_CTX* ctx){
    SSL_CTX_free(ctx);
}

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

    int ssl_app_init(){
	if(NULL != (ssl_ctx = ssl_initialize_ctx(KEYFILE, PASSWD)))
	    return 1;
	load_dh_params(ssl_ctx,DHFILE);
	return 0;
    }

void ssl_app_destroy(){
    ssl_destroy_ctx(ssl_ctx);
    load_dh_params(ssl_ctx, DHFILE);
}

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

void ssl_quit_client(SSL * ssl, int socket){
    int r;

    r = SSL_shutdown(ssl);
    if(!r){
	shutdown(socket, 1);
	r = SSL_shutdown(ssl);
    }

    switch(r){
	case 1:
	    break; /* Success */
	case 0:
	case -1:
	default:
	    ssl_berr_exit("Shutdown failed");
    }

    SSL_free(ssl);
}

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
