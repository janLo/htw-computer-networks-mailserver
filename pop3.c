/* pop3.c
 *
 * The pop3 module for the "Beleg Rechnernetze/Kommunikationssysteme".
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
#include <stdarg.h>

#include "connection.h"
#include "config.h"
#include "pop3.h"
#include "mailbox.h"
#include "fail.h"

/*!
 * \defgroup pop3 POP3 Module
 * @{
 */

//! The states of a pop3 session
/*!
 * Used to track the state of a pop3 sesseion.
 */
enum pop3_states {
   AUTH,                //!< Auth state, not authorized jet.
   START,               //!< Transaction state.
   QUIT                 //!< Invalid session
};

//! The teturn values of the checks
/*!
 * Any check returns one of this enum values.
 */
enum pop3_checks {
    CHECK_OK,           //!< Check successed.
    CHECK_FAIL,         //!< Check failed.
    CHECK_QUIT          //!< Check indicated the end of the session.
};


//! Representation of a POP3 session
/*!
 * This struct represents a pop3 session. It holds all the assigned values.
 * It will be created after the \p accept() and destroyed before \p close().
 */
struct pop3_session {
   int              session_writeback_fd;       //!< The fd to write data back to the client.
   conn_writeback_t session_writeback_fkt;      //!< The function used by the connection module to write data to the client.
   enum pop3_states session_state;              //!< The state of the session.
   int              session_authorized;         //!< Flag indicates if a session is authoized.
   char *           session_user;               //!< Authorized user.
   mailbox_t *      session_mailbox;            //!< Mailbox object of the session.
};

//! Type of command processing functions
typedef  int (* pop3_command_fkt_t)(pop3_session_t*, char *);

static int pop3_check_user(pop3_session_t *, char *);
static int pop3_check_passwd(pop3_session_t *, char *);
static int pop3_quit_session(pop3_session_t * session, char * foo);

static int pop3_stat_mailbox(pop3_session_t * session, char * arg);
static int pop3_list_mailbox(pop3_session_t * session, char * arg);
static int pop3_uidl_mailbox(pop3_session_t * session, char * arg);
static int pop3_retr_mailbox(pop3_session_t * session, char * arg);
static int pop3_dele_mailbox(pop3_session_t * session, char * arg);
static int pop3_rset_mailbox(pop3_session_t * session, char * arg);
static int pop3_noop_mailbox(pop3_session_t * session, char * arg);

//! A Pop3 command
/*!
 * This structure holds a pop3 command, the assigned callback and a flag if a
 * argument should be extracted.
 */
typedef struct pop3_command {
    char *             command_string;          //!< The command string.
    pop3_command_fkt_t command_fkt;             //!< The function to procett the command.
    int                command_has_arg;         //!< Flag if command has arguments.
} pop3_command_t;

//! Commands of the transaction state.
/*! 
 * This are all commands available in the transaction state.
 */
static const pop3_command_t transact_commands[] = {
    {"STAT", pop3_stat_mailbox, 0},
    {"LIST", pop3_list_mailbox ,1},
    {"UIDL", pop3_uidl_mailbox ,1},
    {"RETR", pop3_retr_mailbox ,1},
    {"DELE", pop3_dele_mailbox ,1},
    {"NOOP", pop3_noop_mailbox ,0},
    {"RSET", pop3_rset_mailbox ,0},
    {"QUIT", pop3_quit_session ,0},
    {NULL,   NULL,0}
};

//! Commands of the Auth state.
/*!
 * This are all commands available in the auth state.
 */
static const pop3_command_t auth_commands[] = {
    {"USER", pop3_check_user,   1},
    {"PASS", pop3_check_passwd, 1},
    {"QUIT", pop3_quit_session, 0},
    {NULL,   NULL,              0}
};

//! Write a message to a client
/*! 
 * This writes a message to the client assigned with the \p write_fd. The message
 * itself is a \p printf format string.
 * The values for the placeholders can be given as a arglist after the format
 * string.
 * The message is written with the \p write_fkt to enable transparent plain/ssl
 * writing.
 * \param write_fkt The function to write data to the client.
 * \param write_fd  The filedescriptor to the client.
 * \param msg       The format string of the message.
 * \param ...       The values of the msg placeholders.
 * \return POP3_OK on success, POP3_FAIL else.
 */
static int pop3_write_client_msg(conn_writeback_t write_fkt, int write_fd, char * msg, ...){
    static char buf[2048];
    va_list arglist;
    va_start(arglist, msg);
    size_t len;

    len = vsnprintf(buf, 2048, msg, arglist);

    if(write_fkt(write_fd, buf, len) <= 0)
	return POP3_FAIL;

    return POP3_OK;
}

//! Write message terminator to the client
/*! 
 * This writes a message terminator:
 * \code<cr><lf>.<cr><lf>\endcode 
 * to the client. The data is written with the write_fkt to enable transparent
 * plain/ssl writing. If the \p start_nl flag is set to 0, the \p \<cr>\<lf> at the
 * start will not be written.
 *
 */
static inline int pop3_write_client_term(conn_writeback_t write_fkt, int write_fd, int start_nl) {
    char * term = (start_nl ? "\r\n.\r\n" : ".\r\n");
    int    len  = (start_nl ? 5 : 3);

    if(write_fkt(write_fd, term, len) <= 0)
        return POP3_FAIL;

    return POP3_OK;
}

//! Check if a line starts with the given command.
/*!
 * This checks if the given \p line starts with the \p expected command. The
 * strings are compared case insensitive (\p line will be converted to
 * uppercase).
 * \param line     The line from the client.
 * \param expected The expected command.
 * \return POP3_OK if the command is the expected, POP3_FAIL else.
 */
static inline int pop3_test_cmd(char * line, const char * expected) {
    size_t len = strlen(expected);

    config_to_upper(line, len);

    if ( 0 == strncmp(line, expected, len) )
        return POP3_OK;
    return POP3_FAIL;
}

//! Strips the tailing newlines
/*!
 * This strips the tailing \p \<cr>\<lf> from a fiven message by changig the
 * chars to \p \\0.
 * \param orig    The buffer with the line.
 * \param origlen The length of the line.
 * \return the new length of the line.
 */
static inline int pop3_strip_newlines(char * orig, int origlen) {
    int l = origlen;

    if (orig[origlen - 1] == '\n'){ 
        orig[origlen - 1] = '\0';
        l--;
    }

    if (orig[origlen - 2] == '\r'){
        orig[origlen - 2] = '\0';
        l--;
    }

    return l;
}

//! Init a session mailbox
/*! 
 * This initialize a user mailbox for session usage. The mailbox for the user
 * will also be locked. If another session referes to the users mailbox, the
 * operation will fail.
 * \param session The session struct.
 * \return CHECK_OK on success, CHECK_FAIL else.
 */
static inline int pop3_init_mbox(pop3_session_t * session){
    if (config_user_locked(session->session_user)) {
        return CHECK_FAIL;
    }
    if (NULL == (session->session_mailbox = mbox_init(session->session_user))) {
        return CHECK_FAIL;
    }
    config_lock_mbox(session->session_user);
    return CHECK_OK;
}


/** \name Functions: Auth callbacks
 * @{ */

//! Check a password
/*!
 * This checks a user password if it matches with the password for the session
 * user in the user table read on app start.
 * It also trys to open and lock the users mailbox, set the auth flag of the
 * session and switch it to the transaction state.
 * \param session The current session.
 * \param passwd  The password arg of the PASS command
 * \return CHECK_OK on success, CHECK_FAIL on failture, CHECK_QUIT if the
 *         mailbox cannot be locked.
 */
static int pop3_check_passwd(pop3_session_t * session, char * passwd){

    if (NULL == session->session_user) {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_ERR_USER);
        return CHECK_FAIL;
    }

    if( config_verify_user_passwd(session->session_user, passwd) ){
        session->session_authorized = 1;
        if ( CHECK_OK != pop3_init_mbox(session) ) {
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_ERR_LOCK);
            return CHECK_QUIT;
        }
        session->session_state = START;
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_OK);
        INFO_MSG2("POP3 User %s Authenticated", session->session_user);
        return CHECK_OK;
    }

    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_PASS_ERR_PASS);
    return CHECK_FAIL;
}

//! Check a user
/*!
 * This checks if a given user exist in the user table. If it exist, the user
 * will be added to the session struct.
 * \param session The current session.
 * \param usr     The user argument of the USER command.
 * \return CHECK_OK on success, CHECK_FAIL else.
 */
static int pop3_check_user(pop3_session_t * session, char * usr){
    char * buf;
    int l;

    l = strlen(usr) + 1;

    if (config_has_user(usr)) {
        buf = malloc(sizeof(char) * (l));
        memcpy(buf, usr, l);
        session->session_user = buf;
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_USER_OK);
        return CHECK_OK;
    }

    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_USER_ERR);
    return CHECK_FAIL;
}

/** @} */


/** \name Functions: Transtaction callbacks
 * @{ */

//! Quit a user session
/*!
 * This is used to process the quit command. It closes the users mailbox with
 * the close flag set to one to delete all marked mails.
 * It also set the state to the quit state and releases the mailbox lock.
 * \param session The current session.
 * \param foo     Not used.
 * \return CHECK_QUIT in any case.
 */
static int pop3_quit_session(pop3_session_t * session, char * foo) {
    if (START == session->session_state) {
        if ( NULL != session->session_mailbox ) {
            mbox_close(session->session_mailbox, 1);
            session->session_mailbox = NULL;
            config_unlock_mbox(session->session_user);
        }
    }
    session->session_state = QUIT;
    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_QUIT);
    return CHECK_QUIT;
}

//! Stat a mailbox
/*!
 * This is used to process the stat command in a session. The stat of the
 * mailbox will be printed.
 * \param session The current session.
 * \param arg     Not used.
 * \return CHECK_OK on success, CHECK_FAIL else.
 */
static int pop3_stat_mailbox(pop3_session_t * session, char * arg) {
    if (NULL == session->session_mailbox) {
        return CHECK_FAIL;
    }
    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_STAT, mbox_count(session->session_mailbox), mbox_size(session->session_mailbox));
    return CHECK_OK;
}

//! List the details of a mail or mailbox
/*! 
 * This is used to process the LIST pop3 command. Without any argument it lists
 * all contents of the mailbox with number and size. If a positive number is
 * given which refers to a not delete marked mail in the mailbox, its number and
 * size will be printed.
 * \param session The current session.
 * \param arg     Optional the number of the requestet mail as char sequence.
 * \return CHECK_OK on success, CHECK_FAIL else.
 */
static int pop3_list_mailbox(pop3_session_t * session, char * arg) {
    int i;
    int l = strlen(arg);
   
    if (NULL == session->session_mailbox) {
         return CHECK_FAIL;
    }

    if (0 < l) {
        i = atoi(arg);
        if (mbox_count(session->session_mailbox) >= i &&
                0 < i &&
                ! mbox_is_msg_deleted(session->session_mailbox, i)) {
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_LIST, i, mbox_mail_size(session->session_mailbox, i));
        } else {
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_LIST_ERR);
            return CHECK_FAIL;
        }
    } else {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_LIST_OK, mbox_count(session->session_mailbox), mbox_size(session->session_mailbox)); 
        for (i = 1; i <= mbox_count(session->session_mailbox); i++){
            if (! mbox_is_msg_deleted(session->session_mailbox, i)) {
                pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_LIST_LINE, i, mbox_mail_size(session->session_mailbox, i));
            }
        }
        pop3_write_client_term(session->session_writeback_fkt, session->session_writeback_fd, 0);
    }
    return CHECK_OK;
}

//! List uid of mails in the mailbox
/*! 
 * This lists the uids of the mails in the mailbox. It works similar to
 * pop3_list_mailbox(). 
 * \param session The current session.
 * \param arg     Optional the number of the requestet mail as char sequence.
 * \return CHECK_OK on success, CHECK_FAIL else.
 * \sa pop3_list_mailbox()
 */
static int pop3_uidl_mailbox(pop3_session_t * session, char * arg) {
    int    i;
    int    l = strlen(arg);
    char * buf;
   
    if (NULL == session->session_mailbox) {
         return CHECK_FAIL;
    }

    if (0 < l) {
        i = atoi(arg);
        if (mbox_count(session->session_mailbox) >= i &&
                0 < i &&
                ! mbox_is_msg_deleted(session->session_mailbox, i)) {
            buf = mbox_mail_uid(session->session_mailbox, i);
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_UIDL, i, buf);
            free(buf);
        } else {
            pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_UIDL_ERR);
            return CHECK_FAIL;
        }
    } else {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_UIDL_OK); 
        for (i = 1; i <= mbox_count(session->session_mailbox); i++){
            if (! mbox_is_msg_deleted(session->session_mailbox, i)) {
                buf = mbox_mail_uid(session->session_mailbox, i);
                pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_UIDL_LINE, i, buf);
                free(buf);
            }
        }
        pop3_write_client_term(session->session_writeback_fkt, session->session_writeback_fd, 0);
    }
    return CHECK_OK;
}

//! Deliver a mail.
/*!
 * This delivers a Mail to the client. The client must give a valid mail number.
 * \param session The current session.
 * \param arg     The number of the requestet mail as char sequence.
 * \return CHECK_OK on success, CHECK_FAIL else.
 */
static int pop3_retr_mailbox(pop3_session_t * session, char * arg) {
    size_t l = strlen(arg);
    int i;
    char * buf;

    if (NULL == session->session_mailbox) {
        return CHECK_FAIL;
    }

    if (0 == l) {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_DELE_ERR);
        return CHECK_FAIL;
    }
    i = atoi(arg);

    if (mbox_count(session->session_mailbox) >= i &&
            0 < i &&
            ! mbox_is_msg_deleted(session->session_mailbox, i)) {

        mbox_get_mail(session->session_mailbox, i, &buf, &l);
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_RETR_OK, mbox_mail_size(session->session_mailbox, i));
        if (session->session_writeback_fkt(session->session_writeback_fd, buf, l) <= 0)
            return CHECK_FAIL;
        free(buf);
        pop3_write_client_term(session->session_writeback_fkt, session->session_writeback_fd, 1);
        return CHECK_OK;

    } else {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_RETR_ERR);
        return CHECK_FAIL;
    }
}

//! Mark a mail as deleted.
/*!
 * This marks a mail as deleted if the given number is a valid mail number.
 * \param session The current session.
 * \param arg     The number of the mail as char sequence.
 * \return CHECK_OK on success, CHECK_FAIL else.
 */
static int pop3_dele_mailbox(pop3_session_t * session, char * arg) {
    int l = strlen(arg);
    int i;

    if (NULL == session->session_mailbox) {
        return CHECK_FAIL;
    }

    if (0 == l) {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_DELE_ERR);
        return CHECK_FAIL;
    }
    i = atoi(arg);

    if (mbox_count(session->session_mailbox) >= i &&
            0 < i &&
            ! mbox_is_msg_deleted(session->session_mailbox, i)) {
        mbox_mark_deleted(session->session_mailbox, i);
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_DELE_OK, i);
        return CHECK_OK;
    } else {
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_DELE_ERR);
        return CHECK_FAIL;
    }
}

//! Reset a mailbox
/*! 
 * This resets a mailbox session. It resets all deletion marks.
 * \param session The current session.
 * \param arg     Not used.
 * \return CHECK_OK on success, CHECK_FAIL else.
 */
static int pop3_rset_mailbox(pop3_session_t * session, char * arg) {

    if (NULL == session->session_mailbox) {
        return CHECK_FAIL;
    }

    mbox_reset(session->session_mailbox);
    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_RSET);

    return CHECK_OK;
}

//! Process NOOP command
/*!
 * This simply send a \p +OK to the client if it sends a NOOP command.
 * \param session The current session.
 * \param arg     Not used.
 * \return CHECK_OK in any case.
 */
static int pop3_noop_mailbox(pop3_session_t * session, char * arg) {
    pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, POP3_MSG_NOOP);
    return CHECK_OK;
}
/** @} */

//! Prepare the argument
/*!
 * This prepare the arguments of a command line from the client by stripping
 * newlines on the end and whitespaces on the begin.
 * the pointer to the arg is given as return value. The convertions are done on
 * the original buffer, so no extra free is neccesary.
 * \param msg     The message from the client.
 * \param msglen  The length of the message.
 * \param command The command from the client.
 * \return The argument as char sequence.
 */
static inline char * pop3_prepare_arg(char * msg, ssize_t msglen, char * command){
    int l, c;
    char * pos;

    l = pop3_strip_newlines(msg, msglen);
    c = strlen(command);

    if (c >= l){
        return &(msg[l]);
    }

    pos = msg + c;
    l -= c;

    while (' ' == *pos && l > 0){
        l--;
        pos++;
    }

    return pos;
}

//! Process the client commands
/*! 
 * This iterate over the given command list and checks if the given line starts
 * with the current commanf. It a match was found, it looks if the command has a
 * arg and prepares it.
 * Then the callback of the command will be called and the return value
 * determined from it.
 * \param msg     The message from the client.
 * \param msglen  The length of the message.
 * \param session The current session.
 * \param cmds    The command list.
 * \return CONN_CONT if the session should be alive after this call, CONN_QUIT
 *         else.
 */
static inline int pop3_process_cmd_list(char * msg, ssize_t msglen, pop3_session_t * session, const pop3_command_t * cmds) {
    int    i    = 0;
    char * arg  = NULL;;
    int    t    = 0;

    for (i = 0; NULL != cmds[i].command_string; i++){
        if ( CHECK_OK == pop3_test_cmd(msg, cmds[i].command_string)) {
            if (NULL != cmds[i].command_fkt) {
                if (cmds[i].command_has_arg) {
                    arg = pop3_prepare_arg(msg, msglen, cmds[i].command_string);
                }

                if (CHECK_QUIT == cmds[i].command_fkt(session, arg)) {
                    return CONN_QUIT;
                }
            }
            t = 1;
            break;
        }
    }
    if (0 == t)
        pop3_write_client_msg(session->session_writeback_fkt, session->session_writeback_fd, "-ERR\r\n");
    return CONN_CONT;
}

/** \name Session creation
 * @{ */

//! Create a pop3 session
/*!
 * Creates a new session and initialize all data. It also sets the correct
 * writeback function for the connection type.
 * \param writeback_socket The filedescriptor to the client.
 * \param ssl              Flag if it is a plain or a ssl session.
 * \return a new session or NULL on failture.
 */
pop3_session_t * pop3_create_session(int writeback_socket, int ssl){
    pop3_session_t * new = NULL;
    conn_writeback_t fkt = ( ssl ? conn_writeback_ssl : conn_writeback);
    const char * myhost  = "localhost";

    if (NULL != config_get_hostname()) {
	myhost = config_get_hostname();
    }

    if (POP3_FAIL == pop3_write_client_msg(fkt, writeback_socket, POP3_MSG_GREET, myhost)){
	ERROR_SYS("Writeback fail on pop3 init");
        return new;
    }

    new = malloc(sizeof(pop3_session_t)); 

    new->session_writeback_fd  = writeback_socket;
    new->session_writeback_fkt = fkt;
    new->session_state         = AUTH;
    new->session_authorized    = 0;
    new->session_user          = NULL;
    new->session_mailbox       = NULL;

    INFO_MSG("POP3 Session created");

    return new;
}

//! Create a plain POP3 session
/*!
 * Simply calls pop3_create_session with ssl = 0.
 * \param writeback_socket The filedescriptor to the client.
 * \return a new session or NULL on failture.
 * \sa pop3_create_session()
 */
pop3_session_t * pop3_create_normal_session(int writeback_socket){
    return pop3_create_session(writeback_socket, 0);
}

//! Create a ssl POP3 Session
/*!
 * Simply calls pop3_create_session with ssl = 1.
 * \param writeback_soket The filedescriptor to the client.
 * \return a new session or NULL on failture.
 * \sa pop3_create_session()
 */
pop3_session_t * pop3_create_ssl_session(int writeback_soket) {
    return pop3_create_session(writeback_soket, 1);
}

/** @} */

//! Process the input
/*! 
 * This is the callback for the connection module to process the messages from
 * the pop3 client.
 * \param msg     The message from the client.
 * \param msglen  The length of the message.
 * \param session The current session.
 * \return CONN_CONT if the session should be alive after this call, CONN_QUIT
 *         else.
 * \sa pop3_process_cmd_list()
 */
int pop3_process_input(char * msg, ssize_t msglen, pop3_session_t * session){
    int ret = CONN_CONT;
    switch (session->session_state) {

        case AUTH:
            ret = pop3_process_cmd_list(msg, msglen, session, auth_commands);
            break;
        case START:
            ret = pop3_process_cmd_list(msg, msglen, session, transact_commands);
            break;
        case QUIT:
            return CONN_QUIT;
            break;
    }
    return ret;
}

//! Destroys a pop3 session
/*!
 * This destroys a pop3 session, quits the mailbox, releases the lock and frees
 * all memory.
 * \param session The session to quit.
 * \return 0 in any case.
 */
int pop3_destroy_session(pop3_session_t * session){
    if (NULL != session) {
        if (NULL != session->session_mailbox) {
            mbox_close(session->session_mailbox,0);        
            config_unlock_mbox(session->session_user);
        }
        if (NULL != session->session_user) {  
            free(session->session_user);
        }
        free(session);
    }
    INFO_MSG("POP3 Session destroyed");

    return 0;
}

/* @} */
