#include <stdlib.h>

#define CONFIG_ERROR -1
#define CONFIG_OK     0

inline int config_get_smtp_port();

inline int config_get_pop_port();

inline int config_get_pops_port();

const char* config_get_hostname();

const char* config_get_relayhost();

inline void config_to_lower(char * str, size_t len);

int config_has_user(const char* name);

int config_user_locked(const char* name);

int config_lock_mbox(const char * name);

int config_unlock_mbox(const char * name);

int config_verify_user_passwd(const char * name, const char * passwd);

int config_init(int argc, char * argv[]);
