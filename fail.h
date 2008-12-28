/* fail.h
 *
 * The error module for the "Beleg Rechnernetze/Kommunikationssysteme".
 *
 * author: Jan Losinski
 * date: 28.12.08
 */


#define ERROR_PREF              "ERROR: "
#define ERROR_SWITCH_TEST(x)    if (!0) x
#define ERROR_SYS(source)       ERROR_SWITCH_TEST( put_err(source))
#define ERROR_CUSTM(error)      ERROR_SWITCH_TEST( put_err_str(error))

void put_err(const char*);
void put_err_str(const char*);
