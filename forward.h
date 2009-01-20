/* forward.h
 *
 * The forward module for the "Beleg Rechnernetze/Kommunikationssysteme".
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

#define FWD_FAIL -1
#define FWD_OK   0

#define FWD_POSTMASTER      "postmaster"
#define FWD_ERROR_REPLY1    "An error has occured while sending your mail:"
#define FWD_ERROR_REPLY2    "Your mail was:"
#define FWD_ERROR_HEAD_FROM "From: \"Mail Delivery System\" " FWD_POSTMASTER "@"
#define FWD_ERROR_HEAD_TO   "To: "
#define FWD_ERROR_HEAD_SUBJ "Subject: Undelivered Mail Returned to Sender"

typedef struct body_line body_line_t;
typedef struct fwd_mail fwd_mail_t;

struct body_line {
    char * line_data;
    int    line_len;
    body_line_t * line_next;
};


int fwd_queue(body_line_t * body, char * from, char * to, int failable);
int fwd_process_input(char * msg, ssize_t msglen, fwd_mail_t * fwd);
int fwd_free_mail(fwd_mail_t * fwd);
inline void fwd_delete_body_lines(body_line_t * start);

