/* mailbox.h
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

#include <stdlib.h>

#define MAILBOX_ERROR -1
#define MAILBOX_OK 0


typedef struct mailbox  mailbox_t;


void mbox_push_mail(char * user, char * data, size_t size);

const char * mbox_get_error_msg();

int mbox_init_app();

mailbox_t * mbox_init(char* user);

size_t mbox_size(mailbox_t * mbox);

int mbox_count(mailbox_t * mbox);

size_t mbox_mail_size(mailbox_t * mbox, int mailnum);

char * mbox_mail_uid(mailbox_t * mbox, int mailnum);

int mbox_mark_deleted(mailbox_t * mbox, int mailnum);

int mbox_is_msg_deleted(mailbox_t * mbox, int mailnum);

int mbox_get_mail(mailbox_t * mbox, int mailnum, char** buffer, size_t *buffsize) ;

void mbox_reset(mailbox_t * mbox);

void mbox_close(mailbox_t * mbox, int has_quit);

void mbox_close_app();
