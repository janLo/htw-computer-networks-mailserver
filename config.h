/* config.h
 *
 * The configuration module for the "Beleg Rechnernetze/Kommunikationssysteme".
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

#define CONFIG_ERROR -1
#define CONFIG_OK     0

inline char * config_get_smtp_port();

inline char * config_get_pop_port();

inline char * config_get_pops_port();

const char* config_get_hostname();

const char* config_get_relayhost();

const char* config_get_dbfile();

inline void config_to_lower(char * str, size_t len);
inline void config_to_upper(char * str, size_t len);

int config_has_user(const char* name);

int config_user_locked(const char* name);

int config_lock_mbox(const char * name);

int config_unlock_mbox(const char * name);

int config_verify_user_passwd(const char * name, const char * passwd);

int config_init(int argc, char * argv[]);
