#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "smtp.h"
#include "connection.h"
#include "config.h"
#include "fail.h"

enum check_states {
    CHECK_OK,
    CHECK_ARG,
    CHECK_ARG_MSG,
    CHECK_PREF,
    CHECK_DELIM,
    CHECK_ABRT,
    CHECK_QUIT,
    CHECK_RESET
};

enum arg_states {
    ARG_BAD_MSG,
    ARG_OK,
    ARG_BAD
};

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
    char *              session_host;
    int                 session_authenticated;
    char *              session_from;
    char *              session_to;
    int                 session_writeback_fd;
};



int smtp_write_client_msg(int fd, int status, const char *msg, const char *add){
    int len, ret = SMTP_FAIL;
    char *buff;

    if(add == NULL){
        len = strlen(msg)+10;
    } else {
        len = strlen(msg) + strlen(add) + 20;
    }
    buff = malloc(sizeof(char) * len);
    if(add == NULL){
        len = snprintf(buff, len - 1, msg , status);
    } else {
        len = snprintf(buff, len - 1, msg , status, add);
    }
    if(conn_writeback(fd, buff, len+1) > 0){
        ret = SMTP_OK;
    }
    free(buff);
    return ret;
}



static int smtp_check_input(char *buff,  char *prefix, char delim, int (*check_fkt)(char *), char **val){
    char *arg;
    int check;
    int len = strlen(buff);

    if(val != NULL){
        *val = NULL;
    }
    if((arg = strchr(buff, delim)) == NULL){
        printf("Delim-check failed");
        return CHECK_DELIM;
    }
    *arg = '\0';
    if(strncmp(buff, prefix, strlen(prefix)) != 0){
        printf("Prefix-check failed");
        return CHECK_PREF;
    }

    if(delim != '\0'){
        arg++;
        if (arg > (buff + (len - 1))){
            printf("No Argument after");
            return CHECK_ARG;
        }

        if(check_fkt != NULL && (check = check_fkt(arg)) != ARG_OK){
            printf("Input-check failed");
            return (check == ARG_BAD_MSG ? CHECK_ARG_MSG : CHECK_ARG);
        }

        if(val != NULL){
            *val = malloc(sizeof(char) * (strlen(arg) + 2));
            strcpy(*val, arg);
        }
    } else {
    }
    return CHECK_OK;
}


static void smtp_clean_mail_fields(smtp_session_t * session) {
    session->session_state = HELO;
    if (NULL != session->session_user)
        free(session->session_user);
    session->session_user = NULL;
    if (NULL != session->session_from)
        free(session->session_from);
    session->session_from = NULL;
    if (NULL != session->session_to)
        free(session->session_to);
    session->session_to = NULL;

}


static inline void smtp_reset_session(smtp_session_t * session) {
    if (QUIT != session->session_state) {
        smtp_clean_mail_fields(session);
    }
}

static int smtp_process_input_line(char * buf, ssize_t buflen, char *prefix, char delim, int (*check_fkt)(char *), char **val, smtp_session_t * session){
    int check;
    int fd = session->session_writeback_fd;

    /* strip \r\n from input */
    buf[buflen - 2] = '\0';

    /* Process Prefix checks */
    check = smtp_check_input(buf, prefix, delim, check_fkt, val);
    if (check == CHECK_OK  || check == CHECK_ARG_MSG){
        return check;
    }
    if (check == CHECK_ARG){
        smtp_write_client_msg(fd, 501, SMTP_MSG_SYNTAX_ARG, NULL);
        return CHECK_ABRT;
    }
    if(smtp_check_input(buf, "RSET", '\0', NULL, val) == CHECK_OK){
        smtp_write_client_msg(fd, 250, SMTP_MSG_RESET, NULL);
        smtp_reset_session(session);
        return CHECK_RESET;
    }
    if(smtp_check_input(buf, "QUIT", '\0', NULL, val) == CHECK_OK){
        smtp_write_client_msg(fd, 250, SMTP_MSG_BYE, NULL);
        session->session_state = QUIT;
        return CHECK_QUIT;
    }
    if(smtp_check_input(buf, "NOOP", '\0', NULL, val) == CHECK_OK){
        smtp_write_client_msg(fd, 250, SMTP_MSG_NOOP, NULL);
        return CHECK_ABRT;
    }
    if(smtp_check_input(buf, "VRFY", '\0', NULL, val) == CHECK_OK){
        smtp_write_client_msg(fd, 502, SMTP_MSG_NOT_IMPL, "VRFY");
        return CHECK_ABRT;
    }
    if(smtp_check_input(buf, "EXPN", '\0', NULL, val) == CHECK_OK){
        smtp_write_client_msg(fd, 502, SMTP_MSG_NOT_IMPL, "EXPN");
        return CHECK_ABRT;
    }
    if(smtp_check_input(buf, "HELP", '\0', NULL, val) == CHECK_OK){
        smtp_write_client_msg(fd, 502, SMTP_MSG_NOT_IMPL, "HELP");
        return CHECK_ABRT;
    }
    if(smtp_check_input(buf, "HELO", '\0', NULL, val) == CHECK_OK
            || smtp_check_input(buf, "EHLO", '\0', NULL, val) == CHECK_OK
            || smtp_check_input(buf, "MAIL FROM", '\0', NULL, val) == CHECK_OK
            || smtp_check_input(buf, "DATA", '\0', NULL, val) == CHECK_OK
            || smtp_check_input(buf, "RCPT TO", '\0', NULL, val) == CHECK_OK){
        smtp_write_client_msg(fd, 503, SMTP_MSG_SEQ, NULL);
        return CHECK_ABRT;
    }
    smtp_write_client_msg(fd, 500, SMTP_MSG_SYNTAX, NULL);
    return CHECK_ABRT;
}

static int check_addr(char * addr){
      return ((strlen(addr) > 1) ? ARG_OK : ARG_BAD);
}



smtp_session_t * smtp_create_session(int writeback_fd) {
    smtp_session_t * new;
    const char * hostname = "localhost";

    if (NULL != config_get_hostname()) {
        hostname = config_get_hostname();
    }
    printf("Greet Client");
    if(smtp_write_client_msg(writeback_fd, 220, SMTP_MSG_GREET, hostname) == SMTP_FAIL){
        printf("Write Failed, Abort Session");
        ERROR_SYS("Wrie to Client");
        return NULL;
    }

    new = malloc(sizeof(smtp_session_t));
    new->session_writeback_fd = writeback_fd;
    new->session_type = SMTP;
    new->session_state = NEW;
    new->session_user = NULL;
    new->session_authenticated = 0;
    new->session_from = 0;
    new->session_to = NULL;

    return new;
}

int smtp_destroy_session(smtp_session_t * session) {
    if (NULL != session) {
        smtp_clean_mail_fields(session);
        free(session);
    }
    return 0;
}
int smtp_process_input(char * msg, int msglen, smtp_session_t * session) {

    int result;

    switch (session->session_state) {

        /* wait for HELO */
        case NEW:
            result = smtp_process_input_line(msg, msglen, "HELO", ' ', check_addr, &(session->session_host), session);
            if ( CHECK_QUIT == result ) {
                printf("QUIIIT!!");
                return SMTP_QUIT;
            }
            if ( CHECK_OK == result ) {
                session->session_state = HELO;
                if(smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_HELLO, session->session_host) == SMTP_FAIL){
                    printf("Write Failed, Abort Session");
                    ERROR_SYS("Wrie to Client");
                    return SMTP_QUIT;
                }
            }
        case HELO:
            printf("helo");
            break;
        case FROM:
            break;
        case RCPT:
            break;
        case DATA:
            break;
        case SEND:
            break;
        case QUIT:
            break;
        case AUTH:
            break;
        case USER: 
            break;
    }
    printf("%s\n",msg);


    return SMTP_CONT;
}
