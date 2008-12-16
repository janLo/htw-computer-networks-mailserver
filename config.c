/* config.c
 *
 * The configuration module for the "Beleg Rechnernetze/Kommunikationssysteme".
 *
 * author: Jan Losinski
 * date: 16.12.08
 */


#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <netdb.h>
#include <unistd.h>

#include "config.h"

#define BUF_SIZE_HUGE 4096

typedef struct user user_t;

//! Represent a user 
/*! 
 * This represent a User with username, password and a flag to hold the state
 * of the mailbox.
 */
struct user {
   char * user_name;
   char * user_password;
   int  user_mboxlock;
};


typedef struct userlist userlist_t;

//! List of users
/*!
 * This is needed to build a linked lst of all users.
 */
struct userlist {
    user_t       userlist_user;
    userlist_t * userlist_next;
};


userlist_t * userlist_head = NULL; //! The head of the userlist

int smtp_port = 0;              //! The SMTP Port
int pop_port  = 0;              //! The POP3 Port
int pops_port = 0;              //! The PIP3S Port

char * hostname  = NULL;        //! The hostname of the server

char * relayhost = NULL;        //! The ost where all non-local mails should be relayed to.

char * dbfile    = "mailboxes.sqlite";  //! The filename of the mailbox database file.


//! Get the SMTP port
/*! 
 * \return The SMTP port.
 */
inline int config_get_smtp_port(){
    return (smtp_port ? smtp_port : 25);
}

//! Get the POP3 port
/*! 
 * \return The POP3port.
 */
inline int config_get_pop_port(){
    return (pop_port ? pop_port : 465);
}

//! Get the POP3S port
/*! 
 * \return The POP3S port.
 */
inline int config_get_pops_port(){
    return (pops_port ? pops_port : 993);
}

//! Get the Hostname
/*! 
 * \return The Hostname.
 */
const char* config_get_hostname(){
    return hostname;
}

//! Get the Relayhost
/*! 
 * \return The Relayhost.
 */
const char* config_get_relayhost(){
    return relayhost;
}

//! Get the Dbfilename
/*! 
 * \return The Relayhost.
 */
const char* config_get_dbfile(){
    return dbfile;
}

//! Converts a String to lowercase
/*!
 * Convers a char sequence to lower case for better matching with strcmp(). The
 * provided sequence will be modified, no copy will be created! 
 * The parameter len is optional to prevent the function from do a strlen() at
 * first to determine the length of the string. If it is set to 0, a strlen will
 * count the length of the string at first.
 * \param str The String to convert.
 * \param len The length of the string _without_ null terminator.
 */
inline void config_to_lower(char * str, size_t len){
    size_t i;

    if (0 == len){
        len = strlen(str);
    }
    for ( i = 0; i < len; i++ ) {
        str[i] = tolower(str[i]);
    }
}

//! Parse the User CSV File
/*! 
 * Parsing the CSV file defining the users and passwords.
 * The format is: username\tpassword\n
 * \param filename The filename of the CSV File
 * \return CONFIG_OK on successful parsing, CONFIG_ERROR else.
 */
int config_parse_csv(const char* filename){
    char line_buffer[BUF_SIZE_HUGE];
    FILE * file ;
    char * username;
    char * password;
    size_t user_len, passwd_len;
    user_t * new_user;
    userlist_t * new_listentry = NULL;
    userlist_t * last_listentry = NULL;
    
    /* Open the CSV file */
    if( NULL == (file = fopen(filename, "r")) ){
        return CONFIG_ERROR;
    }

    /* Read linewise, a buffer of 4096 is very huge, I think every line should 
     * fit. If not .. something went wrong. */
    while (NULL != fgets(line_buffer, BUF_SIZE_HUGE, file)){
 
        /* Replace newlines with null chars */
        if (strchr(line_buffer, '\n')) {
            *(strchr(line_buffer, '\n')) = '\0';
        } else {
            continue;
        }

        /* do some string magic */
        if ( ! (username = strtok(line_buffer,"\t"))) continue;
        if ( ! (password = strtok(NULL,       "\t"))) continue;
        //printf("%s: %s\n", username,password);

        /* create a new user */
        new_listentry = malloc(sizeof(userlist_t));
        new_listentry->userlist_next = NULL;

        new_user = &(new_listentry->userlist_user);
        new_user->user_mboxlock = 0;

        /* fetching space for the values */
        user_len = strlen(username)+1;
        passwd_len = strlen(password)+1;

        new_user->user_name = malloc(sizeof(char) * user_len);
        new_user->user_password = malloc(sizeof(char) * passwd_len);

        config_to_lower(username, user_len);

        /* assign the values */
        memcpy(new_user->user_name, username, user_len);
        memcpy(new_user->user_password, password, passwd_len);

        /* Build the list */
        if (NULL == last_listentry) {
            userlist_head = new_listentry;
        } else {
            last_listentry->userlist_next = new_listentry;
        }
        last_listentry = new_listentry;

        printf("%s: %s\n", new_user->user_name,new_user->user_password);

    }
    return CONFIG_OK;

}

//! Get user by name
/*! 
 * This is a helper to get a User object by its username. The comparisn is
 * caseinsensitive. The name parameter will noot be modified. If there is no
 * user with the given name, NULL will be returned.
 * \param name The name of the searched user.
 * \return The user object or NULL if none found.
 */
static inline user_t * config_get_user(const char* name){
    userlist_t * tmp = userlist_head;
    size_t len = strlen(name) + 1;
    char * lower_name = malloc(sizeof(char) * len);
    int i = 1;

    memcpy(lower_name, name, len);
    config_to_lower(lower_name, len - 1);

    while (NULL != tmp) {
        if (0 == strcmp(lower_name, tmp->userlist_user.user_name)){
            free(lower_name);
            return &(tmp->userlist_user);
        }
        tmp = tmp->userlist_next;
        i++;
    }

    free(lower_name);
    return NULL;
}

//! Test if a user is locally available
/*!
 * This test if a user exists in the local user table. If yes, 1 is returned, 
 * 0 if not.
 * The User will be searched with the helper config_get_user(), therfore the
 * search is case insensitive and the provided buffer will not be modified.
 * \param name The name of the searched user.
 * \return 0 if the user does not exist, 1 else.
 * \sa config_get_user()
 */
int config_has_user(const char* name){
    if ( NULL == config_get_user(name) ) {
        return 0;
    }
    return 1;
}

//! Chech the mailbox lock
/*!
 * This checks if the mailbox for a given user is locked. If yes 1 is returned,
 * 0 if not. If the User does not exist in the local table CONFIG_ERROR will be
 * returned.
 * The User will be searched with the helper config_get_user(), therfore the
 * search is case insensitive and the provided buffer will not be modified.
 * \param name The name of the searched user.
 * \return 0 if the users mailbox is not locked, 1 if its locked and
 *         CONFIG_ERROR if the user does not exist localy.
 * \sa config_get_user()
 */ 
int config_user_locked(const char* name){
     user_t * user;
     if ( NULL == (user = config_get_user(name)) ) {
         return CONFIG_ERROR;
     }
     return user->user_mboxlock;
}

//! Set or reset the lock flag
/*! 
 * This sets or resets the mailbox lock flag for a given user. If the user dored
 * not exist in the list, CONFIG_ERROR will be returned.
 * The User will be searched with the helper config_get_user(), therfore the
 * search is case insensitive and the provided buffer will not be modified.
 * \param name The name of the searched user.
 * \param lock Tells if the mailbox should be locked or unlocked.
 * \return CONFIG_ERROR if the user does not exist localy, CONFIG_OK else.
 * \sa config_get_user()
 */ 
static inline int config_set_user_mbox_lock(const char* name, int lock){
    user_t * user;
    if ( NULL == (user = config_get_user(name)) ) {
        return CONFIG_ERROR;
    }
    user->user_mboxlock = lock;
    return CONFIG_OK;
}

//! Set the lock flag
/*! 
 * This wrapps config_set_user_mbox_lock() to set the mailbox lock flag.
 * \param name The username to reset the lock flag
 * \return CONFIG_ERROR if the user does not exist localy, CONFIG_OK else.
 * \sa config_set_user_mbox_lock()
 */
int config_lock_mbox(const char * name){
    return config_set_user_mbox_lock(name, 1);
}

//! Reset the lock flag
/*! 
 * This wrapps config_set_user_mbox_lock() to reset the mailbox lock flag.
 * \param name The username to reset the lock flag
 * \return CONFIG_ERROR if the user does not exist localy, CONFIG_OK else.
 * \sa config_set_user_mbox_lock()
 */
int config_unlock_mbox(const char * name){
    return config_set_user_mbox_lock(name, 0);
}

//! Verify a user password
/*! 
 * This verifys a user password given in plain text.
 * The user will be searched with config_get_user() in the userlist, so the
 * search will be case insensitive and the provided buffer will not be modified.
 * 1 will be returned the passwords matches. If not, 0 will be returned and if
 * the user does not exist, CONFIG_ERROR will be returned.
 * \param name   The name of the user.
 * \param passwd The password to verify.
 * \return 0 if the password dont match, 1 if it matches and CONFIG_ERROR if the
 *         user does not exist.
 * \sa config_set_user_mbox_lock()
 */
int config_verify_user_passwd(const char * name, const char * passwd){
    user_t * user;
    if ( NULL == (user = config_get_user(name)) ) {
        return CONFIG_ERROR;
    }
    if (0 == strcmp(user->user_password, passwd)) {
        return 1;
    }
    return 0;
}


//! Parse a port option
/*!
 * Parses a single host value and ensure that its a valid value. If ist not
 * valid, CONFIG_ERROR is given back.
 * \param buf The port a char sequence, null terminated.
 * \return the port number as int or CONFIG_ERROR (see above).
 */
int config_parse_single_port(const char *buf){
    int ret = 0;
    int len = strlen(buf);
    int i;
    
    for(i = 0; i < len; i++){
        if(!isdigit(buf[i])){
            return CONFIG_ERROR;
        }
    }

    ret = atoi(buf);

    if (ret > 65535 || ret < 1) {
        return CONFIG_ERROR;
    }
    return ret;
}

//! Parse a port tuple
/*!
 * Parse tuple of 3 port values: first the SMTP port, then the POP3 port and at
 * last the POP3S Port. The ports are converted to int and assigned to the
 * global config.
 * If the format is invalid or the values are no valid port numbers,
 * CONFIG_ERROR is given back.
 * \param buf The tuple as char sequence, seperated by ',', nullterminated.
 * \return CONFIG_ERROR on failture, CONFIG_OK else.
 */
int config_parse_ports(const char* buf){
    size_t len = strlen(buf) + 1;
    char * tmp_buff = malloc(sizeof(char) * len);
    char * smtp;
    char * pop3;
    char * pop3s;

    memcpy(tmp_buff, buf, len);

    if ( (NULL != (smtp = strtok(tmp_buff,","))) && 
            (CONFIG_ERROR != (smtp_port = config_parse_single_port(smtp))) ) {
        free(tmp_buff);
        return CONFIG_ERROR;
    }
    if ( NULL != (pop3 = strtok(NULL, ","))  && 
            (CONFIG_ERROR != (pop_port = config_parse_single_port(smtp))) ) {
        free(tmp_buff);
        return CONFIG_ERROR;
    }
    if ( NULL != (pop3s = strtok(NULL, ",")) && 
            (CONFIG_ERROR != (pops_port = config_parse_single_port(smtp))) ) {
        free(tmp_buff);
        return CONFIG_ERROR;
    }

    free(tmp_buff);
    return CONFIG_OK;
}

//! Parse a host option
/*!
 * Parses a hostname. It does a gethostbyname() lookup to ensure that the given
 * parameter is a valid hostname.
 * On success the hostname will be returned in a fresh new buffer. Else NULL
 * will be returned.
 * \param buf The buffer tith the hostname.
 * \return A new buffer with the hostname or NULL on failture. 
 */
char *config_parse_host(const char *buf){
    char * new_host;
    size_t len; 

    if(gethostbyname(buf) == NULL) {
        return NULL;
    }

    len = strlen(buf) + 1;
    new_host = malloc(sizeof(char) * len);
    memcpy(new_host, buf, len);

    return new_host;
}


//! Init the config
/*! 
 * This initializes the config. It parses the commandline parameters and fills
 * the config variables.
 * This should called only once at application start.
 * On success CONFIG_OK will be returned, CONFIG_ERROR else.
 * \param argc The count of arguments given on the commandline.
 * \param argv The rguments from the commandline as array of char sequences.
 * \return CONFIG_OK on success, CONFIG_ERROR else.
 */
int config_init(int argc, char * argv[]){
    char c;
    size_t len;
    int init_ok = 0;

    while ((c = getopt (argc, argv, "d:p:u:H:R:hV")) != -1){
        switch (c) {
            case 'p':
                if (CONFIG_ERROR == config_parse_ports(optarg)) 
                    return CONFIG_ERROR;
                break;
             case 'H':
                if (NULL == (hostname = config_parse_host(optarg)))
                    return CONFIG_ERROR;
                break;
             case 'R':
                if (NULL == (relayhost = config_parse_host(optarg)))
                    return CONFIG_ERROR;
                break;
             case 'u':
                if (CONFIG_ERROR == config_parse_csv(optarg))
                    return CONFIG_ERROR;
                init_ok = 1;
                break;
             case 'd':
                len = strlen(optarg) + 1;
                dbfile = malloc(sizeof(char) * len);
                memcpy(dbfile, optarg, len);
                break;
        }
    }
    return (init_ok ? CONFIG_OK : CONFIG_ERROR);
}

// Test!
/*
int main(int argc, char * argv[]){

    config_init(argc, argv);
    if(config_has_user("Jan")){
     printf("foo\n");
    }
//    config_lock_mbox("Jan");
    if(config_user_locked("Jan")){
     printf("foo\n");
    }
    return 0;
}
*/
