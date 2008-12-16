#include <stdio.h>
#include <string.h>
#include <ctype.h>

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

int smtp_port;                  //! The SMTP Port
int pop_port;                   //! The POP3 Port
int pops_port;                  //! The PIP3S Port

char * hostname= NULL;          //! The hostname of the server

char * relayhost= NULL;         //! The ost where all non-local mails should be relayed to.




//! Get the SMTP port
/*! 
 * \return The SMTP port.
 */
inline int config_get_smtp_port(){
    return smtp_port;
}

//! Get the POP3 port
/*! 
 * \return The POP3port.
 */
inline int config_get_pop_port(){
    return pop_port;
}

//! Get the POP3S port
/*! 
 * \return The POP3S port.
 */
inline int config_get_pops_port(){
    return pops_port;
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
        config_to_lower(password, 0);
        //printf("%s: %s\n", username,password);

        /* create a new user */
        new_listentry = malloc(sizeof(userlist_t));
        new_listentry->userlist_next = NULL;

        new_user = &(new_listentry->userlist_user);
        new_user->user_mboxlock = 0;

        /* fetching space for the values */
        new_user->user_name = malloc(sizeof(char) * (strlen(username)+1));
        new_user->user_password = malloc(sizeof(char) * (strlen(password)+1));

        /* assign the values */
        strcpy(new_user->user_name, username);
        strcpy(new_user->user_password, password);

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

int config_has_user(const char* name){
    if ( NULL == config_get_user(name) ) {
        return 0;
    }
    return 1;
}

int config_user_locked(const char* name){
     user_t * user;
     if ( NULL == (user = config_get_user(name)) ) {
         return CONFIG_ERROR;
     }
     return user->user_mboxlock;
}

static inline int config_set_user_mbox_lock(const char* name, int lock){
    user_t * user;
    if ( NULL == (user = config_get_user(name)) ) {
        return CONFIG_ERROR;
    }
    user->user_mboxlock = lock;
    return 0;
}

int config_lock_mbox(const char * name){
    return config_set_user_mbox_lock(name, 1);
}

int config_unlock_mbox(const char * name){
    return config_set_user_mbox_lock(name, 0);
}

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



int main(){

    config_parse_csv("user.csv");
    if(config_has_user("Jan")){
     printf("foo\n");
    }
//    config_lock_mbox("Jan");
    if(config_user_locked("Jan")){
     printf("foo\n");
    }
    return 0;
}
