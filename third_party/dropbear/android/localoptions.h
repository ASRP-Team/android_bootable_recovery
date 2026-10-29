#ifndef DROPBEAR_LOCALOPTIONS_H
#define DROPBEAR_LOCALOPTIONS_H

/*
 * AERA recovery build configuration.
 *
 * Recovery runs as uid 0 without a real passwd/shadow database, so password
 * authentication is handled by AERA itself (see src/aera_auth.c and
 * src/svr-authpasswd.c). Public key authentication uses the standard Dropbear
 * code path with an authorized_keys file maintained by the recovery UI.
 */

/* Server only, the recovery image does not ship a SSH client binary. */
#define DROPBEAR_SERVER 1
#define DROPBEAR_CLIENT 0

/* Password auth through our own verifier instead of crypt(3). */
#define DROPBEAR_SVR_PASSWORD_AUTH 1
#define DROPBEAR_AERA_PASSWORD_AUTH 1

/* Public key auth, keys are imported by the user from the UI. */
#define DROPBEAR_SVR_PUBKEY_AUTH 1
#define DROPBEAR_SVR_PUBKEY_OPTIONS 1

/* Recovery is a single privileged process, keep multiuser semantics. */
#define DROPBEAR_SVR_MULTIUSER 1

/* SFTP subsystem */
#define DROPBEAR_SFTPSERVER 1
#define SFTPSERVER_PATH "/system/bin/aera_sftp_server"

/* Android recovery has no /usr/bin, everything lives in /system/bin. */
#define DEFAULT_PATH "/system/bin:/sbin"
#define DEFAULT_ROOT_PATH "/system/bin:/sbin:/system/xbin"

/* Accepted login shells. /etc/shells is generated at runtime as well. */
#define COMPAT_USER_SHELLS "/system/bin/sh","/sbin/sh","/bin/sh"

#endif /* DROPBEAR_LOCALOPTIONS_H */
