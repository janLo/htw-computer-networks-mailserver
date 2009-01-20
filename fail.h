/* fail.h
 *
 * The error module for the "Beleg Rechnernetze/Kommunikationssysteme".
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


#define ERROR_PREF                      "[1;31mERROR:[0m "
#define ERROR_SWITCH_TEST(x)            if (!0) x
#define ERROR_GEN_MSG(msg)              gen_err_msg(ERROR_PREF, msg, __FILE__, __LINE__)
#define ERROR_SYS(source)               ERROR_SWITCH_TEST( put_err(ERROR_GEN_MSG(source)))
#define ERROR_SYS2(error_fmt, arg1)     ERROR_SYS(build_msg(error_fmt, arg1))
#define ERROR_CUSTM(error)              ERROR_SWITCH_TEST( put_err_str(ERROR_GEN_MSG(error)))
#define ERROR_CUSTM2(error_fmt, arg1)   ERROR_CUSTM(build_msg(error_fmt, arg1))

#define INFO_PREF                       "[1;32mINFO:[0m  "
#define INFO_GEN_MSG(msg)               gen_err_msg(INFO_PREF, msg, __FILE__, __LINE__)
#define INFO_MSG(msg)                   put_info(INFO_GEN_MSG(msg))
#define INFO_MSG2(msg_fmt, arg1)        put_info(INFO_GEN_MSG(build_msg(msg_fmt,arg1)))
#define INFO_MSG3(msg_fmt, arg1, arg2)  put_info(INFO_GEN_MSG(build_msg(msg_fmt,arg1,arg2)))


//DO NOT USE THIS DIRECT
inline int gen_err_msg(const char * pref, const char * msg, const char * file, int line);
void put_err(int);
void put_err_str(int);
void put_info(int);
inline char * build_msg(const char *, ...);
