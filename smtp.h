/* smtp.h
 *
 * The smtp module for the "Beleg Rechnernetze/Kommunikationssysteme".
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


typedef struct smtp_session smtp_session_t;

#define SMTP_OK 0
#define SMTP_FAIL -1


char * smtp_resolve_mx(const char * host);
smtp_session_t * smtp_create_session(int writeback_fd);
int smtp_destroy_session(smtp_session_t * session);

int smtp_process_input(char * msg, int msglen, smtp_session_t *);


#define SMTP_MSG_GREET          "%d %s SMTP Relay by Jan Losinski\r\n"
#define SMTP_MSG_AUTH           "%d \r\n"
#define SMTP_MSG_AUTH_OK        "%d Authentication successful\r\n"
#define SMTP_MSG_AUTH_NOK       "%d Error: authentication failed\r\n"
#define SMTP_MSG_RELAY_DENIED1  "%d-%s: Relay access denied\r\n"
#define SMTP_MSG_RELAY_DENIED2  "%d You must me authenticated to relay!\r\n"
#define SMTP_MSG_RESET          "%d RESET Accepted, Resetted\r\n"
#define SMTP_MSG_NOOP           "%d NOOP Ok, I'm here\r\n"
#define SMTP_MSG_BYE            "%d Bye Bye.\r\n"
#define SMTP_MSG_QUIT           "%d Ok, I try to forward the Message:\r\n"
#define SMTP_MSG_NOT_IMPL       "%d %s Command not implemented\r\n"
#define SMTP_MSG_SYNTAX         "%d Syntax error or command unrecognized\r\n"
#define SMTP_MSG_SYNTAX_ARG     "%d syntax error in parameters or arguments\r\n"
#define SMTP_MSG_SENDER         "%d Sender %s OK\r\n"
#define SMTP_MSG_HELLO          "%d Hello %s!\r\n"
#define SMTP_MSG_EHLLO          "%d-Hello %s!\r\n"
#define SMTP_MSG_EHLLO_EXT1     "%d AUTH PLAIN\r\n"
#define SMTP_MSG_RCPT           "%d RCPT %s seems to be OK\r\n"
#define SMTP_MSG_DATA           "%d Waiting for Data, End with <CR><LF>.<CR><LF>\r\n"
#define SMTP_MSG_DATA_ACK       "%d Message Accepted and forwarded\r\n"
#define SMTP_MSG_DATA_ACK_LOCAL "%d Message Accepted and delivered\r\n"
#define SMTP_MSG_DATA_FAIL      "%d Message Accepted but forward failed\r\n"
#define SMTP_MSG_MEM            "%d Requested mail action aborted: exceeded storage allocation\r\n"
#define SMTP_MSG_SEQ            "%d Bad Sequence of Commands\r\n"
#define SMTP_MSG_PROTO          "%d-Poto: %s\r\n"
