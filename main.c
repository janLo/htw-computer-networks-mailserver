/* main.c
 *
 * The main module for the "Beleg Rechnernetze/Kommunikationssysteme".
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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "config.h"
#include "mailbox.h"
#include "connection.h"
#include "ssl.h"
#include "fail.h"

/*!
 * \defgroup main Main Module
 * @{
 */

#define VERSION      "0.1 ALPHA"
#define AUTHOR	     "Jan Losinski (g08s29) <losinski@wh2.tu-dresden.de>"
#ifndef REVISION_HTW
    #define REVISION_HTW  "1"
#endif
#ifndef REVISION_MAIN
    #define REVISION_MAIN "180"
#endif

//! Print a help message
/*! 
 * This pints a help message for the priogram.
 * \param bin_name The name of the binary (argv[0]).
 */
void print_help(const char * bin_name) {
   printf("Usage: %s [OPTIONS]\n", bin_name);
   printf("\n");
   printf("The OPTIONS are:\n");
   printf("\t-h                   Print this help and exit.\n");
   printf("\t-V                   Print version informations and exit.\n");
   printf("\t-p <smtp,pop3,pop3s> Specify the ports for the services.\n");
   printf("\t-u <filename>        Specify the filename of the CSV file.\n");
   printf("\t-H <hostname>        Specify the hostname of the server.\n");
   printf("\t-R <hostname>        Specify the hostname of the relay server.\n");
   printf("\t-d <dbfile>          Specify the database file of the mailbox.\n");
   printf("\n");
}

//! Print version information
/*! 
 * Print the version informations of the programm.
 * \param bin_name The name of the binary (argv[0]).
 */
void print_version(const char * bin_name) {
   printf("Version information for %s:\n", bin_name);
   printf("Autor:               " AUTHOR "\n");
   printf("Version:             " VERSION "\n");
   printf("Revision (studsvn):  " REVISION_HTW "\n");
   printf("Revision (losinski): " REVISION_MAIN "\n");
   printf("\n");
}

//! Preprocess the cmdline options
/*!
 * This preprocesses the cmdline options of the programm. It processes the
 * options -h and -V. If one is given, the programm prints the requestet
 * information out and returns 1. If none of both are givenit will return 0. The
 * argv arrax is copyed at start and the stae of optind, opterr and optopt is
 * preserved so that a second getopt will perform correctly.
 * \param argc The argcount given at main().
 * \param argv The argv array given at main.
 * \return 0 if no -h or -v is given, 1 else.
 */
int preprocess_options(int argc, char * argv[]){
   char ** tmp_buf = malloc(sizeof(char*) * (argc));
   char c;
   int ret = 0;
   int i;
   int my_optind = optind;
   int my_opterr = opterr;
   int my_optopt = optopt;

   for (i = 0; i < argc; i++) {
       tmp_buf[i]=argv[i];
   }

   while ((c = getopt (argc, tmp_buf, "d:p:u:H:R:Vh")) != -1){
       switch (c) {
           case 'V':
               print_version(argv[0]);
               ret = 1;
               break;
           case 'h':
               print_help(argv[0]);
               ret = 1;
               break;
       }
   }

   optind = my_optind;
   opterr = my_opterr;
   optopt = my_optopt;

   free(tmp_buf);
   return ret;
}

static void exit_sig_handler(int signr){
  INFO_MSG("Signal recived, exit!");
  conn_close();
  ssl_app_destroy();
  mbox_close_app();
  exit(0);
}


//! The main function
int main(int argc, char* argv[]) {
    if (preprocess_options(argc, argv)) {
        return 0;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGABRT, exit_sig_handler);
    signal(SIGINT, exit_sig_handler);
    signal(SIGQUIT, exit_sig_handler);
    signal(SIGTERM, exit_sig_handler);

    config_init(argc, argv);
    mbox_init_app(config_get_dbfile()); 
    
    ssl_app_init();
    
    conn_init();
    conn_wait_loop();
    conn_close();

    ssl_app_destroy();

    mbox_close_app();

    return 0;
}

/** @} */
