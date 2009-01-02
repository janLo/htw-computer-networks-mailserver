/* fail.c
 *
 * The error module for the "Beleg Rechnernetze/Kommunikationssysteme".
 *
 * author: Jan Losinski
 * date: 28.12.08
 */


#include <stdio.h>
#include <stdlib.h>

#include "fail.h"


/*!
 * \defgroup fail Error Module
 * @{
 */


// Puts a error to stderr
void put_err(const char *src){
  if (src == NULL){
    fprintf(stderr, ERROR_PREF);
  } else {
    fprintf(stderr, ERROR_PREF "%s - ", src);
  }
  perror("");
}

// also puts a error to stderr
void put_err_str(const char *err){
  if(err != NULL){
    fprintf(stderr, ERROR_PREF "%s \n", err);
  } else {
    fprintf(stderr, ERROR_PREF " Unspecified\n");
  }
}

/** @} */
