#include <stdlib.h>

#include "smtp.h"

enum session_states {
    NEW,
    HELO,
    FROM,
    RCPT,
    DATA,
    SEND,
    QUIT,
    AUTH,
    USER
};

enum smtp_types {
    SMTP,
    ESMTP
};

struct smtp_session {
    enum smtp_types     session_type;
    enum session_states session_state;
    char *              session_user;
    int                 session_authenticated;
    char *              session_from;
    char *              session_to;
};

smtp_session_t * smtp_create_session() {
    smtp_session_t * new = malloc(sizeof(smtp_session_t));
    return new;
}

int smtp_destroy_session(smtp_session_t * session) {
    free(session);
    return 0;
}
int smtp_process_input(char * msg, int msglen, smtp_session_t * session) {
    return 0;
}
