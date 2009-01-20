/* pop3.h
 *
 * The pop3 module for the "Beleg Rechnernetze/Kommunikationssysteme".
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

#define POP3_MSG_GREET		"+OK %s POP3-Server, Enter user\r\n" 	// Hostname

#define POP3_MSG_USER_OK	"+OK Please enter passwd\r\n"
#define POP3_MSG_USER_ERR	"-ERR Username not found\r\n"

#define POP3_MSG_PASS_OK	"+OK Mailbox locked\r\n"
#define POP3_MSG_PASS_ERR_PASS	"-ERR Invalid passwd\r\n"
#define POP3_MSG_PASS_ERR_LOCK	"-ERR Cannot lock mailbox\r\n"
#define POP3_MSG_PASS_ERR_USER	"-ERR No username entered\r\n"

#define POP3_MSG_STAT		"+OK %d %d\r\n"				// Count, Size

#define POP3_MSG_LIST		"+OK %d %d\r\n"				// Count, Size
#define POP3_MSG_LIST_OK	"+OK %d messages (%d Octets)\r\n"   	// Count, Size
#define POP3_MSG_LIST_LINE      "%d %d\r\n"                             // Count, Size
#define POP3_MSG_LIST_ERR	"-ERR No such message\r\n"

#define POP3_MSG_UIDL		"+OK %d %s\r\n"				// Count, UID
#define POP3_MSG_UIDL_OK	"+OK\r\n"
#define POP3_MSG_UIDL_LINE      "%d %s\r\n"                             // Count, UID
#define POP3_MSG_UIDL_ERR	"-ERR No such message\r\n"

#define POP3_MSG_RETR_OK	"+OK %d Octets\r\n"			// Msg size
#define POP3_MSG_RETR_ERR	"-ERR No such message\r\n"

#define POP3_MSG_DELE_OK	"+OK Message %d deleted\r\n"		// Msg num
#define POP3_MSG_DELE_ERR	"-ERR No such message\r\n"

#define POP3_MSG_NOOP		"+OK\r\n"

#define POP3_MSG_RSET		"+OK\r\n"

#define POP3_MSG_QUIT		"+OK Bye\r\n"

#define POP3_OK    0
#define POP3_FAIL -1


typedef struct pop3_session pop3_session_t;

pop3_session_t * pop3_create_normal_session(int writeback_socket);
pop3_session_t * pop3_create_ssl_session(int writeback_soket);
int pop3_process_input(char * msg, ssize_t msglen, pop3_session_t * data);
int pop3_destroy_session(pop3_session_t * session);

