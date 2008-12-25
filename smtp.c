#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>

#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include "smtp.h"
#include "forward.h"
#include "connection.h"
#include "config.h"
#include "fail.h"
#include "mailbox.h"

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

//! States of a SMTP session
/*!
 * This enum contain all states a smtp session can have. It is used to
 * coordinate thr actions with the input.
 */
enum session_states {
    NEW,        //!< A new session, no (valid) input recived, greet sended.
    HELO,       //!< A session after HELO (if it is a ESMTP session, a valid auth has already happened here), waiting for MAIL FROM.
    EHLO,       //!< A session after EHLO (waiting for AUTH).
    FROM,       //!< A session after MAIL FROM, waiting for RCPT TO.
    RCPT,       //!< A session after RCPT TO, waiting for DATA.
    DATA,       //!< A session after DATA, waiting for the data block, terminated with '<cr><lf>.<cr><lf>'.
    AUTH,       //!< A session waiting for auth credentials.
    QUIT,       //!< A terminated session.
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
    int                 session_rcpt_local;
    body_line_t *       session_data;
};




char * smtp_resolve_mx(const char * host){
    u_char *p, *end;
    char name[MAXHOSTNAMELEN + 1];
    char buf[1024];
    char * ret = NULL;
    union {
        HEADER h;
        u_char u[PACKETSZ];
    } q;
    const HEADER *hp;
    int len, i;

    strncpy(name,host,MAXHOSTNAMELEN);

    len = res_query(name, C_IN, T_MX, (u_char *)&q, sizeof(q));
    if(len < 0) {
        return ret; /* Host has no MX records */
    }

    if((unsigned int)len > sizeof(q)) 
    {
        return ret;
    }

    hp = &(q.h);
    p = q.u + HFIXEDSZ;
    end = q.u + len;

    for(i = ntohs(hp->qdcount); i--; p += len + QFIXEDSZ)
        if((len = dn_skipname(p, end)) < 0) {
            return ret;
        }

    i = ntohs(hp->ancount);

    while((--i >= 0) && (p < end)) 
    {
        u_short type, pref;
        u_long ttl;     /* unused */

        if((len = dn_expand(q.u, end, p, buf, sizeof(buf) - 1)) < 0)
        {
            return ret;
        }
        p += len;
        GETSHORT(type, p);
        p += INT16SZ;
        GETLONG(ttl, p);
        GETSHORT(len, p);
        if(type != T_MX) {
            p += len;
            continue;
        }
        GETSHORT(pref, p);
        if((len = dn_expand(q.u, end, p, buf, sizeof(buf) - 1)) < 0)
        {
            return ret;
        }

        len = strlen(buf) + 1;        
        ret = malloc(sizeof(char) * len);
        memcpy(ret, buf, len);
        return ret;
    }
    return ret;
}


char * smtp_unbase64(unsigned char *input, int length)
{
    BIO *b64, *bmem;

    char * buffer = malloc(sizeof(char) * length);
    memset(buffer, 0, length);

    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new_mem_buf(input, length);
    bmem = BIO_push(b64, bmem);

    BIO_read(bmem, buffer, length);

    BIO_free_all(bmem);

    return buffer;
}

//! Converts a String to uppercase
/*!
 * Convers a char sequence to upper case for better matching with strcmp(). The
 * provided sequence will be modified, no copy will be created! 
 * The parameter len is optional to prevent the function from do a strlen() at
 * first to determine the length of the string. If it is set to 0, a strlen will
 * count the length of the string at first.
 * \param str The String to convert.
 * \param len The length of the string _without_ null terminator.
 */
inline void config_to_upper(char * str, size_t len){
    size_t i;

    if (0 == len){
        len = strlen(str);
    }
    for ( i = 0; i < len; i++ ) {
        str[i] = toupper(str[i]);
    }
}

//! Decode mase64 encoded string
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

//! Check if a Prefix is correct
static inline int smtp_check_prefix(char *buf,  char *prefix) {
    int    len = strlen(prefix);
    int    ret = CHECK_ABRT;

    config_to_upper(buf, len);

    if (0 == strncmp(buf, prefix, len)){
        ret = CHECK_OK;
    }

    return ret;
}

//! Checks if a input lise looks as expected
static int smtp_check_input(char *buff,  char *prefix, char delim, int (*check_fkt)(char *), char **val){
    char *arg;
    int check;
    int len = strlen(buff);
    int plen = strlen(prefix);

    if(val != NULL){
        *val = NULL;
    }
    if((arg = strchr(buff+plen, delim)) == NULL){
        printf("Delim-check failed: '%s' has no '%c'\n", buff, delim);
        return CHECK_DELIM;
    }
    *arg = '\0';
    if(CHECK_OK != smtp_check_prefix(buff, prefix)){
        printf("Prefix-check failed\n");
        return CHECK_PREF;
    }

    if(delim != '\0'){
        arg++;
        
        /* eat tabs and spaces */
        while( (*arg == ' ' || *arg == '\t') && arg < (buff + (len - 1)) ) {
            arg++;
        }

        if (arg > (buff + (len - 1))){
            printf("No Argument after\n");
            return CHECK_ARG;
        }

        if(check_fkt != NULL && (check = check_fkt(arg)) != ARG_OK){
            printf("Input-check failed: %s\n", arg);
            return (check == ARG_BAD_MSG ? CHECK_ARG_MSG : CHECK_ARG);
        }

        if(val != NULL){
            len = strlen(arg) + 1;
            *val = malloc(sizeof(char) * len);
            memcpy(*val, arg, len);
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
    session->session_rcpt_local = 0;
}


static inline void smtp_reset_session(smtp_session_t * session) {
    if (QUIT != session->session_state) {
        smtp_clean_mail_fields(session);
    }
}

static int smtp_process_auth_line(char * buf, ssize_t buflen,  smtp_session_t * session){
    char * plain = smtp_unbase64((unsigned char *)buf, buflen);
    char * id = plain;
    char * auth = strchr(plain, '\0') + 1;
    char * pass = strchr(auth, '\0') + 1;
    int    ret = CHECK_ABRT;
    int    len = 0;

    printf("BASE64: '%s' (%d)\n", buf, buflen);
    printf("PLAIN:  %s .. %s .. %s\n", id, auth, pass);

    if (config_has_user(auth) && config_verify_user_passwd(auth, pass)) {
        session->session_authenticated = 1;
        len = strlen(auth) + 1;
        session->session_user = malloc(sizeof(char) * len);
        memcpy(session->session_user, auth, len);
        printf("Authenticated\n");
        ret = CHECK_OK;
    }

    free(plain);
    return ret;
}


static int smtp_process_input_line(char * buf, ssize_t buflen, char *prefix, char delim, int (*check_fkt)(char *), char **val, smtp_session_t * session){
    int check;
    int fd = session->session_writeback_fd;

    /* strip \r\n from input */
    if ('\r' == buf[buflen - 2])
        buf[buflen - 2] = '\0';
    if ('\n' == buf[buflen - 1])
        buf[buflen - 1] = '\0';

    /* Process Prefix checks */
    check = smtp_check_input(buf, prefix, delim, check_fkt, val);
    if (check == CHECK_OK  || check == CHECK_ARG_MSG){
        return check;
    }
    if (check == CHECK_ARG){
        smtp_write_client_msg(fd, 501, SMTP_MSG_SYNTAX_ARG, NULL);
        return CHECK_ARG;
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
            || smtp_check_input(buf, "AUTH", '\0', NULL, val) == CHECK_OK
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

//! Extracts the mbox user from a mail adress
static inline char * smtp_extraxt_mbox_user(const char * addr) {
    int len = strchr(addr, '@') - addr;
    char * buf = malloc(sizeof(char) * len +1);
    memcpy(buf, addr, len);
    buf[len ] = '\0';
    return buf;
}

//! Check if a sequence is a valid hostname
static int smtp_check_addr(char * addr){
    if (gethostbyname(addr) == NULL) {
        return ARG_BAD;
    }

    return  ARG_OK;
}

//! check basical if a given sequence is a mail address
static int smtp_check_mail(char * addr){
    char *pos;
    int len;
    int i;

    /* strip < > */
    if (addr[0] == '<') {
        len = strlen(addr);
        for (i = 0; i < len - 2; i++) {
            addr[i] = addr[i+1];
        }
        addr[len-2] = '\0';
    }

        
    pos = strchr(addr, '@');
    if(pos != NULL){
        if (smtp_check_addr(pos+1) == ARG_OK){
            if ((pos - addr) > 2){
                return ARG_OK;
            }
        } 
        char * mx;
        if (NULL != (mx = smtp_resolve_mx(pos+1))){
            perror("");
            free(mx);
            return ARG_OK;
        }
    }
    return ARG_BAD;
}

//! Checks if a host is the local host or not
static inline int smtp_check_mail_host_local(char * addr) {
    const char * myhost = "localhost";
    char *pos = strchr(addr, '@') + 1;

    if (NULL != config_get_hostname()) {
        myhost = config_get_hostname();
    }

    if (0 == strcmp(myhost, pos)){
        printf("smtp_check_mail_host_local: TRUE\n");
        return ARG_OK;
    } 
    return ARG_BAD;
}

//! Check if the user of a mail address exists locally
static inline int smtp_check_mail_user_local(char * addr) {
    char * buf = smtp_extraxt_mbox_user(addr);

    if (config_has_user(buf)) {
        printf("smtp_check_mail_user_local: TRUE\n");
        free(buf);
        return ARG_OK;
    }
    free(buf);
    return ARG_BAD;
}

//! Deletes all body lines od a session
static void smtp_delete_body_lines(smtp_session_t * session){
    body_line_t * tmp1 = session->session_data;
    body_line_t * tmp2 = session->session_data;

    while (NULL != tmp1){
        tmp2 = tmp1;
        tmp1 = tmp1->line_next;
        if (NULL != tmp2->line_data && 0 != tmp2->line_len)
            free(tmp2->line_data);
        free(tmp2);
    }
    session->session_data = NULL;
}

//! Build a new mail-body-line
static inline body_line_t * smtp_create_body_line(char * line, int line_len){
    body_line_t * new = NULL;

    new = malloc(sizeof(body_line_t));
    new->line_next = NULL;
    new->line_len  = line_len;
    new->line_data = malloc(sizeof(char) * (line_len+1));
    memcpy(new->line_data, line, line_len);
    new->line_data[line_len] = '\0';

    return new;
}

//! Append a mailbody line to the body list
static inline body_line_t * smtp_append_body_line(body_line_t * list, char * line, int line_len) {
    body_line_t * new = NULL;
    body_line_t * tmp = list;

    while(NULL != tmp){
        if(NULL == tmp->line_next) {
            new = smtp_create_body_line(line, line_len);
            tmp->line_next = new;
            break;
        }
        tmp = tmp->line_next;
    }
    return new;
}

//! Pack all lines of a message in one piece of mem
static char * smtp_collapse_body_lines(smtp_session_t * session){
    ssize_t size  = 0;
    char *  buf   = NULL;
    char *  pos   = NULL;
    body_line_t * line = session->session_data;

    while (NULL != line){
        size += line->line_len;
        line = line->line_next;
    }

    line = session->session_data;
    buf = malloc(sizeof(char) * size);
    pos = buf;

    while (NULL != line){
        memcpy(pos, line->line_data, line->line_len);
        pos += line->line_len;
        line = line->line_next;
    }

    return buf;
}


//! Reads the body data of a email 
static int smtp_process_body_data(char * buf, int buflen, smtp_session_t * session){
    body_line_t * new = NULL;
   
    if (strncmp(buf, ".\r\n", 3) == 0){
        return CHECK_QUIT;
    }

    if (NULL == session->session_data) {
        /* TODO add recive headers here! */
        new = session->session_data = smtp_create_body_line(buf, buflen);
    } else {
        new = smtp_append_body_line(session->session_data, buf, buflen);
    }
   return (NULL == new ? CHECK_ABRT : CHECK_OK);
}

//! Build a new SMTP session
smtp_session_t * smtp_create_session(int writeback_fd) {
    smtp_session_t * new;
    const char * hostname = "localhost";

    if (NULL != config_get_hostname()) {
        hostname = config_get_hostname();
    }
    printf("Greet Client");
    if(smtp_write_client_msg(writeback_fd, 220, SMTP_MSG_GREET, hostname) == SMTP_FAIL){
        printf("Write Failed, Abort Session\n");
        ERROR_SYS("Wrie to Client");
        return NULL;
    }

    new = malloc(sizeof(smtp_session_t));
    new->session_writeback_fd  = writeback_fd;
    new->session_type          = SMTP;
    new->session_state         = NEW;
    new->session_user          = NULL;
    new->session_data          = NULL;
    new->session_authenticated = 0;
    new->session_from          = 0;
    new->session_to            = NULL;
    new->session_rcpt_local    = 0;

    return new;
}

//! Free all resources of a smtp session
int smtp_destroy_session(smtp_session_t * session) {
    if (NULL != session) {
        smtp_clean_mail_fields(session);
        smtp_delete_body_lines(session);
        free(session);
    }
    return 0;
}

//! Process the input of a SMTP connection
int smtp_process_input(char * msg, int msglen, smtp_session_t * session) {
    int ehlo;
    int result;

    switch (session->session_state) {

        /* wait for HELO or EHLO */
        case NEW:
            ehlo = smtp_check_prefix(msg, "EHLO"); 
            if (CHECK_OK == ehlo) {
                result = smtp_process_input_line(msg, msglen, "EHLO", ' ', NULL, &(session->session_host), session);
                if ( CHECK_OK == result ) {
                    session->session_state = EHLO;
                    session->session_type  = ESMTP;
                    if( smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_EHLLO, session->session_host) == SMTP_FAIL
                            || smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_EHLLO_EXT1, session->session_host) == SMTP_FAIL ) {
                        printf("Write Failed, Abort Session\n");
                        ERROR_SYS("Wrie to Client");
                        return CONN_QUIT;
                    }
                }
            } else {
                result = smtp_process_input_line(msg, msglen, "HELO", ' ', NULL, &(session->session_host), session);
                if ( CHECK_OK == result ) {
                    session->session_state = HELO;
                    if( smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_HELLO, session->session_host) == SMTP_FAIL){
                        printf("Write Failed, Abort Session\n");
                        ERROR_SYS("Wrie to Client");
                        return CONN_QUIT;
                    }
                }
            }
            break;

        /* wait for AUTH */
        case EHLO:
            if ((strlen("AUTH PLAIN") + 2) < msglen) {
                char * tmp = NULL;
                result = smtp_process_input_line(msg, msglen, "AUTH PLAIN", ' ', NULL, &tmp, session);
                if ( CHECK_OK == result ) {
                    int len = strlen(tmp);
                    char * tmp2 = malloc(sizeof(char) * (len + 2));

                    memcpy(tmp2, tmp, len);
                    tmp2[len]   = '\r';
                    tmp2[len+1] = '\n';
                    tmp2[len+2] = '\0';

                    if(CHECK_OK == smtp_process_auth_line(tmp2, strlen(tmp2), session)) {

                        if (smtp_write_client_msg(session->session_writeback_fd, 235, SMTP_MSG_AUTH_OK, NULL) == SMTP_FAIL){
                            printf("Write Failed, Abort Session\n");
                            ERROR_SYS("Wrie to Client");
                            return CONN_QUIT;
                        }
                        session->session_state = HELO;
                    } else {
                        if (smtp_write_client_msg(session->session_writeback_fd, 535, SMTP_MSG_AUTH_NOK, NULL) == SMTP_FAIL){
                            printf("Write Failed, Abort Session\n");
                            ERROR_SYS("Wrie to Client");
                            return CONN_QUIT;
                        }
                    }

                    free(tmp2);
                }
                if (NULL != tmp)
                    free(tmp);
            } else {
                result = smtp_process_input_line(msg, msglen, "AUTH PLAIN", '\0', NULL, NULL, session);
                if ( CHECK_OK == result ) {
                    session->session_state = AUTH;
                    if(smtp_write_client_msg(session->session_writeback_fd, 334, SMTP_MSG_AUTH, session->session_host) == SMTP_FAIL){
                        printf("Write Failed, Abort Session\n");
                        ERROR_SYS("Wrie to Client");
                        return CONN_QUIT;
                    }
                }
            }
            break;

        /* wait for MAIL FROM */
        case HELO:
            result = smtp_process_input_line(msg, msglen, "MAIL FROM", ':', smtp_check_mail, &(session->session_from), session);
            if ( CHECK_OK ==result ) {
                printf("Sender Mail: %s\n", session->session_from);
                session->session_state = FROM;
                if(smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_SENDER, session->session_from) == SMTP_FAIL){
                    printf("Write Failed, Abort Session\n");
                    ERROR_SYS("Wrie to Client");
                    return CONN_QUIT;
                }
            } 
            break;

        /* wait for RCPT TO */
        case FROM:

            result = smtp_process_input_line(msg, msglen, "RCPT TO", ':', smtp_check_mail, &(session->session_to), session);
            if ( CHECK_OK == result ) {
                printf("Rcpt Mail: %s\n", session->session_to);
                if (ARG_OK == smtp_check_mail_host_local(session->session_to) && ARG_OK == smtp_check_mail_user_local(session->session_to)) {
                    session->session_rcpt_local = 1;
                }
                if ( (! session->session_authenticated) && (! session->session_rcpt_local) ) {
                    if(smtp_write_client_msg(session->session_writeback_fd, 554, SMTP_MSG_RELAY_DENIED1, session->session_to) == SMTP_FAIL
                            || smtp_write_client_msg(session->session_writeback_fd, 554, SMTP_MSG_RELAY_DENIED2, NULL) == SMTP_FAIL){
                        printf("Write Failed, Abort Session\n");
                        ERROR_SYS("Wrie to Client");
                        return CONN_QUIT;
                    }
                } else {
                    session->session_state = RCPT;
                    if(smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_RCPT, session->session_to) == SMTP_FAIL){
                        printf("Write Failed, Abort Session\n");
                        ERROR_SYS("Wrie to Client");
                        return CONN_QUIT;
                    }
                }
            } 
            break;

        /* wait for DATA */
        case RCPT:
            result = smtp_process_input_line(msg, msglen, "DATA", '\0', NULL, NULL, session);
            if ( CHECK_OK ==result ) {
                session->session_state = DATA;
                if(smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_DATA, NULL) == SMTP_FAIL){
                    printf("Write Failed, Abort Session\n");
                    ERROR_SYS("Wrie to Client");
                    return CONN_QUIT;
                }
            } 
            break;

        /* Eating data lines and waiting for <cr><lf> */
        case DATA:
            result = smtp_process_body_data(msg, msglen, session);
            if (CHECK_QUIT == result) {
                char * full_msg = smtp_collapse_body_lines(session);

                printf("MSG:\n%s\n", full_msg);

                if (session->session_rcpt_local){
                    char * user = smtp_extraxt_mbox_user(session->session_to);
                    mbox_push_mail(user, full_msg, 0);
                    if(smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_DATA_ACK_LOCAL, NULL) == SMTP_FAIL){
                        printf("Write Failed, Abort Session\n");
                        ERROR_SYS("Wrie to Client");
                        free(full_msg);
                        return CONN_QUIT;
                    }
                } else {
                    if (FWD_OK == fwd_queue(session->session_data, session->session_from, session->session_to, 1)) {

                        if(smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_DATA_ACK, NULL) == SMTP_FAIL){
                            printf("Write Failed, Abort Session\n");
                            ERROR_SYS("Wrie to Client");
                            free(full_msg);
                            return CONN_QUIT;
                        }
                    } else {
                        if(smtp_write_client_msg(session->session_writeback_fd, 250, SMTP_MSG_DATA_FAIL, NULL) == SMTP_FAIL){
                            printf("Write Failed, Abort Session\n");
                            ERROR_SYS("Wrie to Client");
                            free(full_msg);
                            return CONN_QUIT;
                        }
                    }

                }

                free(full_msg);
                result = CHECK_RESET;
            } 
            if (CHECK_ABRT == result) {
                /* EOM */
                result = CHECK_QUIT;
            }
            break;

        /* quit state, session should never reach this */
        case QUIT:
            return CONN_QUIT;
            break;

        /* reads the auth data */
        case AUTH:
            result = smtp_process_auth_line(msg, msglen, session);
            if ( CHECK_OK == result ) {
                if (smtp_write_client_msg(session->session_writeback_fd, 235, SMTP_MSG_AUTH_OK, NULL) == SMTP_FAIL){
                    printf("Write Failed, Abort Session\n");
                    ERROR_SYS("Wrie to Client");
                    return CONN_QUIT;
                }
                session->session_state = HELO;
            } else {
                if (smtp_write_client_msg(session->session_writeback_fd, 535, SMTP_MSG_AUTH_NOK, NULL) == SMTP_FAIL){
                    printf("Write Failed, Abort Session\n");
                    ERROR_SYS("Wrie to Client");
                    return CONN_QUIT;
                }
                session->session_state = EHLO;
            }
            break;
    }

    /* Reset a session for new delivery */
    if ( CHECK_RESET == result ) {
        if (NEW != session->session_state) {
            session->session_state = HELO;
        }
        printf("RESET!!\n");
    }

    /* Quit a connection */
    if ( CHECK_QUIT == result ) {
        session->session_state = QUIT;
        printf("QUIT!!\n");
        return CONN_QUIT;
    }
    printf("%s\n",msg);


    return CONN_CONT;
}
