/* mailbox.c
 *
 * A mailbox module for the "Beleg Rechnernetze/Kommunikationssysteme".
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

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#include "mailbox.h"
#include "config.h"
#include "fail.h"

/*!
 * \defgroup mbox Mailbox Module
 * @{
 */


#define STATEMENT_PUSH   "INSERT INTO mail (user,data,size,date) VALUES (?,?,?,?)"
#define STATEMENT_FETCH  "SELECT data FROM mail WHERE id = ?"
#define STATEMENT_COUNT  "SELECT count(id) AS num, sum(data) AS siz FROM mail WHERE user = ?"
#define STATEMENT_STAT   "SELECT id, size FROM mail WHERE user = ?"
#define STATEMENT_DELETE "DELETE FROM mail WHERE id = ?"

//! Mail structure
/*! 
 * This is a structure representing a mail in the mailbox with its id-number
 * mapping, it size and the deleted flag. 
 */
typedef struct mail {
    int    mail_session_number;
    int    mail_id;
    size_t mail_size;
    char   is_deleted;
} mail_t;

//! Mailbox structure
/*!
 * This structure represents a mailbox with its size, its username, its
 * mailcount and a list (array) of the mails.
 */
struct mailbox {
    char*        mbox_user;
    int          mbox_mailcount;
    size_t       mbox_size;
    mail_t * mbox_map;
} ;


sqlite3 * database;              //! The Database connection. Only one per Application.
sqlite3_stmt * statement_push;   //! Prepared statement for push new mails.
sqlite3_stmt * statement_fetch;  //! Prepared statement for fetching a whole mail.
sqlite3_stmt * statement_stat;   //! Prepared statement for fetching metadata of a mail.
sqlite3_stmt * statement_count;  //! Prepared statement for counting new mails;
sqlite3_stmt * statement_delete; //! Prepared statement for deleting marked mails;


//! Push a Mail in a box
/*!
 * This is the function to push a new mail in a specific user-mailbox.
 * \param user The name of the user the mail should be delivered to as
 *             nullterminated char sequence.
 * \param data The mail-blob as null terminated char sequence.
 * \param size The size of the mail blob. If size will be 0, the actual 
 *             strlen() + 1 will be used.
 */
void mbox_push_mail(char * user, char * data, size_t size){
    if (0 == size) {
        size = strlen(data);
    }
    time_t now = time(NULL);
    sqlite3_bind_text(statement_push, 1, user, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement_push, 2, data, size, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement_push, 3, size);
    sqlite3_bind_int(statement_push, 4, now);
    sqlite3_step(statement_push);
    sqlite3_reset(statement_push);
}

//! Get the error String
/*!
 * This Function returns the error string provided by the sqlite lib.
 * \return the errorstring as nullterminated char sequence.
 */
const char * mbox_get_error_msg(){
    return sqlite3_errmsg(database);
}



//! Initializion of the Mailbox Module
/*!
 * This function initialize the Mailbox Module. It create the Connection to the 
 * Database and prepare the Statements for faster execution. It must be called
 * before the first call to any other mbox_* function. The best way is to call
 * it at app initialization. There should also be _only_one_ call per
 * application!
 * \return MAILBOX_OK on success, MAILBOX_ERROR else.
 */
int mbox_init_app(){

    /* Cnnecting to DB File */
    if( SQLITE_OK != sqlite3_open_v2(config_get_dbfile(), &database, SQLITE_OPEN_READWRITE, NULL) ) {
        return MAILBOX_ERROR;
    }

    /* Preparing Statements */
    if (SQLITE_OK != sqlite3_prepare_v2(database, STATEMENT_PUSH, strlen(STATEMENT_PUSH)+1, &statement_push, NULL)) {
        return MAILBOX_ERROR;
    }
    if (SQLITE_OK != sqlite3_prepare_v2(database, STATEMENT_FETCH, strlen(STATEMENT_FETCH)+1, &statement_fetch, NULL)) {
        return MAILBOX_ERROR;
    }
    if (SQLITE_OK != sqlite3_prepare_v2(database, STATEMENT_STAT, strlen(STATEMENT_STAT)+1, &statement_stat, NULL)) {
        return MAILBOX_ERROR;
    }
    if (SQLITE_OK != sqlite3_prepare_v2(database, STATEMENT_COUNT, strlen(STATEMENT_COUNT)+1, &statement_count, NULL)) {
        return MAILBOX_ERROR;
    }
    if (SQLITE_OK != sqlite3_prepare_v2(database, STATEMENT_DELETE, strlen(STATEMENT_DELETE)+1, &statement_delete, NULL)) {
        return MAILBOX_ERROR;
    }

    INFO_MSG("mailbox init ok");
    return MAILBOX_OK;
}



//! Init a specific Mailbox
/*! 
 * This inits a specific Mailbox for fetching Data from it. This is not
 * neccesary if someone only wants to push a new Mail into a box. The best way
 * is to call it once per POP3 session.
 * \param user The name of the user the Mailbox should be initialized for.
 * \return A pointer to the new mailbox object.
 */
mailbox_t * mbox_init(char* user){
    int i = 0;

    mailbox_t * new_mbox = malloc(sizeof(mailbox_t));
    
    memset(new_mbox, '\0', sizeof(mailbox_t));    

    char * username = malloc(sizeof(char)*(strlen(user)+1));
    strcpy(username, user);
    new_mbox->mbox_user = username;

    sqlite3_bind_text(statement_count, 1, username, -1, SQLITE_TRANSIENT);

    /* get count of mails in box and overall size */
    if(SQLITE_ROW == sqlite3_step(statement_count)) {
        new_mbox->mbox_mailcount = sqlite3_column_int(statement_count, 0);
        new_mbox->mbox_size      = sqlite3_column_int(statement_count, 1);
    } else {
        /* some error handling */
    }
    sqlite3_reset(statement_count);
    
    /* Build mbox map */
    if (new_mbox->mbox_mailcount > 0) {
        sqlite3_bind_text(statement_stat,  1, username, -1, SQLITE_TRANSIENT);

        new_mbox->mbox_map = malloc( sizeof(mail_t) * new_mbox->mbox_mailcount );
        while(SQLITE_ROW == sqlite3_step(statement_stat)) {
            new_mbox->mbox_map[i].mail_session_number = i+1;
            new_mbox->mbox_map[i].mail_size           = sqlite3_column_int(statement_stat, 1);
	    new_mbox->mbox_map[i].mail_id             = sqlite3_column_int(statement_stat, 0);
	    new_mbox->mbox_map[i].is_deleted          = 0;
	    new_mbox->mbox_size                      += new_mbox->mbox_map[i].mail_size;
	    i++;
        }
        sqlite3_reset(statement_stat);
    } else {
        new_mbox->mbox_map = NULL;
    }

    INFO_MSG2("Mailbox opened for %s", user);
    return new_mbox;
}

//! Overall size of a specific mailbox
/*! 
 * This function is to fetch the summatized site of a user mailbox.
 * \return The summarized size of all mails.
 */
size_t mbox_size(mailbox_t * mbox){
    return mbox->mbox_size;
}

//! Count mails in a mailbox
/*!
 * This function returns the number of mails in the fiven mailbox.
 * \param mbox The mailbox.
 */
int mbox_count(mailbox_t * mbox){
    return mbox->mbox_mailcount;
}

//! Size of a mail or mailbox
/*! This function returns the size of a given mailbox or mail in it. 
 * If mailnum is a positive value, the size of the mail with this
 * number will be returned.
 * \param mbox    The mailbox.
 * \param mailnum The optional number of the mail in the mailbox.
 * \return The size of the mail or -1 on failture. 
 */
size_t mbox_mail_size(mailbox_t * mbox, int mailnum){
    if (mailnum > 0 && mailnum <= mbox->mbox_mailcount) {
	int offset = mailnum - 1;
	return mbox->mbox_map[offset].mail_size;
    } else {
	return -1;
    }
}

//! Uid of a Mail
/*!
 * This returns a unique id of a given mail as char sewuwnce.
 * The returned buffer must be freed manually. 
 * \param mbox    The mailbox.
 * \param mailnum The optional number of the mail in the mailbox.
 * \return NULL on faulture or a buffer with the UID.
 */
char * mbox_mail_uid(mailbox_t * mbox, int mailnum) {
    if (mailnum > 0 && mailnum <= mbox->mbox_mailcount) {
       char * buf = malloc(sizeof(char) * 20);
       int offset = mailnum - 1;
       snprintf(buf, 19, "%018d",mbox->mbox_map[offset].mail_id);
       return buf;
    } else {
        return NULL;
    }
}

//! Mark a mail as deleted
/*! 
 * Marks a specific mail in a given mailbos as deleted. As deleted marked mails
 * will ne "really" deleted if the mailbox will be closed with mbox_close() and
 * has_quit is set to non-zero.
 * \param mbox The mailbox of the mail to be marked.
 * \param mailnum The number of the mail in the mailbox.
 * \return MAILBOX_OK if operation was successful. MAILBOX_ERROR if mailnum is
 *         invalid.
 * \sa mbox_close()
 */
int mbox_mark_deleted(mailbox_t * mbox, int mailnum){
    if (mailnum > 0 && mailnum <= mbox->mbox_mailcount) {
	int offset = mailnum - 1;
	mbox->mbox_map[offset].is_deleted = 1;
	return MAILBOX_OK;
    } else {
	return MAILBOX_ERROR;
    }
}

//! Checks if a message is marked as deleted
/*!
 * This can be used to check if a message is marked to be deleted or not.
 * \param mbox The mailbox of the mail to be marked.
 * \param mailnum The number of the mail in the mailbox.
 * \return 0 if the message is not marked, 1 else.
 */
int mbox_is_msg_deleted(mailbox_t * mbox, int mailnum){
    if (mailnum > 0 && mailnum <= mbox->mbox_mailcount) {
        int offset = mailnum - 1;
        return mbox->mbox_map[offset].is_deleted;
    } else {
        return 0;
    }
}

//! Fetch the contents of a mail
/*! This fetches the blob of a stored email and return it as null terminated
 * char sequence in new allocated memory. The pointer to the memory will be
 * assigned to *buffer. It will _never_ be freed from this module, the client is
 * fully responsible to avoid resource leaks here! The size of the buffer is 
 * stored in *buffsize. If a invalod mailnum is given a MAILBOX_ERROR will be
 * returned.
 * \param mbox     The mailbox of the mail.
 * \param mailnum  The number of the mail in the mailbox.
 * \param buffer   pointer to the place where the pointervalue of the new buffer
 *                 should be stored.
 * \param buffsize Pointer to the place there the size of the new buffer should
 *                 be stored.
 * \return MAILBOX_OK if the operation was successful, MAILBOX_ERROR else.
 */
int mbox_get_mail(mailbox_t * mbox, int mailnum, char** buffer, size_t *buffsize) {
    char * newbuff;
    const char * oldbuff;
    size_t size;
    int id;

    if (mailnum <= 0 && mailnum > mbox->mbox_mailcount) {
        return MAILBOX_ERROR;
    }

    id = mbox->mbox_map[mailnum - 1].mail_id;
    size = mbox->mbox_map[mailnum - 1].mail_size;

    sqlite3_bind_int(statement_fetch, 1, id);

    if ( SQLITE_ROW == sqlite3_step(statement_fetch) ) {
        newbuff = malloc(sizeof(char)*(size+1));
        newbuff[size] = '\0';
        oldbuff = sqlite3_column_blob(statement_fetch, 0);
        memcpy(newbuff, oldbuff, size);
        *buffer = newbuff;
        *buffsize = size+1;
    } else {
        ERROR_CUSTM2("Can't open database: %s", sqlite3_errmsg(database));
        sqlite3_reset(statement_fetch);
        return MAILBOX_ERROR;
    }

    sqlite3_reset(statement_fetch);
    return MAILBOX_OK;
}

//! Reset markers
/*! 
 * Reset all delete markers of the given mailbox.
 * \param mbox The mailbox where the markers should be resetted.
 */
void mbox_reset(mailbox_t * mbox){
    int i;
    for (i = 0; i < mbox->mbox_mailcount; i++) {
	mbox->mbox_map[i].is_deleted = 0;
    }
}

//! Closes the given mailbox
/*!
 * This closes the given mailbox and frees all related resources. If has_quit is
 * set to non-zero this function assumes that you will commit the delete-markers
 * to the database by deleting the marked mails.
 * \param mbox The mbox to close.
 * \param has_quit 0 if the marked mails should not deleted, non-zero else.
 * \sa mbox_mark_deleted()
 */
void mbox_close(mailbox_t * mbox, int has_quit){
    if (has_quit) {
        INFO_MSG("Delete marked emails");
        int i;
        /* delete marked mails */
        for (i = 0; i < mbox->mbox_mailcount; i++) {
            if(mbox->mbox_map[i].is_deleted) {
               sqlite3_bind_int(statement_delete, 1, mbox->mbox_map[i].mail_id);
               sqlite3_step(statement_delete);
               sqlite3_reset(statement_delete);
            }
        }
    }
    /* Free resources */
    if (NULL != mbox->mbox_map)
        free(mbox->mbox_user);
    if (NULL != mbox->mbox_map)
        free(mbox->mbox_map);
    free(mbox);

    INFO_MSG("Mailbox closed");
}

//! Shut down the mailbox module
/*! 
 * This closes all resources of the mailbox module. This shoulb be valled
 * before exitting of the application 
 */
void mbox_close_app(){
    /* close database, etc */
    sqlite3_finalize(statement_delete);
    sqlite3_finalize(statement_stat);
    sqlite3_finalize(statement_count);
    sqlite3_finalize(statement_fetch);
    sqlite3_finalize(statement_push);
    sqlite3_close(database);
    INFO_MSG("Mailbox module closed");
}

/** @} */
