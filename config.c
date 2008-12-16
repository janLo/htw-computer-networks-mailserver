#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CONFIG_ERROR -1
#define CONFIG_OK     0

#define BUF_SIZE_HUGE 4096

typedef struct user user_t;

struct user {
   char * user_name;
   char * user_password;
   int  user_mboxlock;
};

typedef struct userlist userlist_t;

struct userlist {
    user_t       userlist_user;
    userlist_t * userlist_next;
};


userlist_t * userlist_head = NULL;


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
