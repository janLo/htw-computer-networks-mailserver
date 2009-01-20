/* fail.c
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


#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <stdarg.h>

#include "fail.h"


/*!
 * \defgroup fail Error Module
 * @{
 */

static char msg_buf[2048];
static char loc_buf[2048];

inline int gen_err_msg(const char * pref, const char * msg, const char * file, int line) {
    snprintf(loc_buf, 1023, "%s:%d", file, line);
    if(NULL == msg){
        snprintf(msg_buf, 2047, "%s", pref);
        return 0;
    } else {
        snprintf(msg_buf, 2047, "%s%s", pref, msg);
        return 1;
    }
}

inline char * build_msg(const char * fmt, ...){
    va_list arglist;
    va_start(arglist, fmt);
    static char buf[2048];
    vsnprintf(buf, 2047, fmt, arglist);
    return buf;
}

// Puts a error to stderr
void put_err(int has_src){
  if (has_src){
    fprintf(stderr, "%s \tAt %s: ", msg_buf, loc_buf);
  } else {
    fprintf(stderr, "%s \tAt %s:\n", msg_buf, loc_buf);
  }
  perror("");
}

// also puts a error to stderr
void put_err_str(int has_err){
  if(has_err){
    fprintf(stderr, "%s \tAt: %s\n", msg_buf, loc_buf);
  } else {
    fprintf(stderr, "%s (Unspecified) \tAt %s\n", msg_buf, loc_buf);
  }
}

// drops a info to stdout
void put_info(int has_msg){
  if(has_msg){
    fprintf(stdout, "%s \tAt: %s\n", msg_buf, loc_buf);
  } else {
    fprintf(stdout, "%s (Unspecified) \tAt %s\n", msg_buf, loc_buf);
  }
}
/** @} */
