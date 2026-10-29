#ifndef DROPBEAR_AERA_AUTH_H_
#define DROPBEAR_AERA_AUTH_H_

#include "includes.h"

#if DROPBEAR_AERA_PASSWORD_AUTH
/* Resolve the recovery login account. Returns DROPBEAR_FAILURE when the
 * username is not handled by AERA, in which case the caller should fall back
 * to getpwnam(). */
int aera_fill_passwd(const char *username);

/* Returns DROPBEAR_SUCCESS when the password matches AERA_PASSWORD_FILE. */
int aera_password_check(const char *password, unsigned int passwordlen);
#endif

#endif /* DROPBEAR_AERA_AUTH_H_ */
