#include <stdlib.h>

#define CONN_FAIL  -1
#define CONN_OK  0



int conn_init();
int conn_close();
int conn_wait_loop();
ssize_t conn_writeback(int fd, char * buf, ssize_t len) ;
int conn_connect_socket(char * host, char * port);
