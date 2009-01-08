/* ssl.h
 *
 * The connection-ssl module for the "Beleg Rechnernetze/Kommunikationssysteme".
 *
 * author: Jan Losinski
 * date: 08.01.08
 */


#include <openssl/ssl.h>

typedef struct ssl_data {
    void * ssl_data;
    SSL  * ssl_ssl;
} ssl_data_t;


int ssl_app_init();
void ssl_app_destroy();
SSL * ssl_accept_client(int socket);
void ssl_quit_client(SSL * ssl, int socket);
int ssl_read(int socket, SSL * ssl, char * buf, int buflen);
int ssl_write(int socket, SSL * ssl, char * buf, int buflen);
