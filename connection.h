#include <stdlib.h>

#define CONN_FAIL  -1
#define CONN_OK  0
#define CONN_QUIT -1
#define CONN_CONT 0



int conn_init();
int conn_close();
int conn_wait_loop();
ssize_t conn_writeback(int fd, char * buf, ssize_t len) ;
int conn_new_fwd_socket(char * host,  void * data);
