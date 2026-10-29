/*
 * AERA recovery SSH authentication helpers.
 *
 * Recovery has no passwd/shadow database and Android's libc does not provide
 * crypt(3), so Dropbear is taught to resolve the single "root" account itself
 * and to verify the password against a file maintained by the recovery UI.
 *
 * Password file format (/etc/aera_ssh_password, mode 0600):
 *     sha256:<salt_hex>:<hash_hex>
 * where hash = SHA256(salt_bytes || password). Both salt and hash are
 * lowercase hex encoded, salt is 16 bytes and hash is 32 bytes.
 */

#include "includes.h"
#include "dbutil.h"
#include "session.h"
#include "aera_auth.h"

#if DROPBEAR_AERA_PASSWORD_AUTH

#define AERA_PASSWORD_FILE "/etc/aera_ssh_password"
#define AERA_LOGIN_USER "root"
#define AERA_LOGIN_DIR "/"
#define AERA_LOGIN_SHELL "/system/bin/sh"
/* Non-empty placeholder, the real password lives in AERA_PASSWORD_FILE. */
#define AERA_PASSWORD_PLACEHOLDER "aera-unix"

#define AERA_SALT_HEX_LEN 32
#define AERA_HASH_HEX_LEN 64

static int hexval(int c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int hex_decode(const char *hex, unsigned char *out, unsigned int outlen) {
	unsigned int i;
	for (i = 0; i < outlen; i++) {
		int hi = hexval(hex[i * 2]);
		int lo = hexval(hex[i * 2 + 1]);
		if (hi < 0 || lo < 0) {
			return DROPBEAR_FAILURE;
		}
		out[i] = (unsigned char)((hi << 4) | lo);
	}
	return DROPBEAR_SUCCESS;
}

/* Resolve the recovery login account without consulting getpwnam(). */
int aera_fill_passwd(const char *username) {
	if (username == NULL || strcmp(username, AERA_LOGIN_USER) != 0) {
		return DROPBEAR_FAILURE;
	}

	ses.authstate.pw_uid = 0;
	ses.authstate.pw_gid = 0;
	ses.authstate.pw_name = m_strdup(AERA_LOGIN_USER);
	ses.authstate.pw_dir = m_strdup(AERA_LOGIN_DIR);
	ses.authstate.pw_shell = m_strdup(AERA_LOGIN_SHELL);
	ses.authstate.pw_passwd = m_strdup(AERA_PASSWORD_PLACEHOLDER);
	return DROPBEAR_SUCCESS;
}

/* Verify a cleartext password against AERA_PASSWORD_FILE. */
int aera_password_check(const char *password, unsigned int passwordlen) {
	FILE *f = NULL;
	char buf[256];
	char *salt_hex = NULL;
	char *hash_hex = NULL;
	unsigned char salt[16];
	unsigned char expected[32];
	unsigned char computed[32];
	hash_state md;
	size_t n;
	int ret = DROPBEAR_FAILURE;

	if (password == NULL) {
		return DROPBEAR_FAILURE;
	}

	f = fopen(AERA_PASSWORD_FILE, "rb");
	if (f == NULL) {
		dropbear_log(LOG_WARNING, "aera: cannot open %s: %s",
				AERA_PASSWORD_FILE, strerror(errno));
		return DROPBEAR_FAILURE;
	}
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';

	/* trim trailing whitespace/newline */
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'
				|| buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
		buf[--n] = '\0';
	}

	if (strncmp(buf, "sha256:", 7) != 0) {
		dropbear_log(LOG_WARNING, "aera: malformed password file");
		return DROPBEAR_FAILURE;
	}
	salt_hex = buf + 7;
	hash_hex = strchr(salt_hex, ':');
	if (hash_hex == NULL) {
		dropbear_log(LOG_WARNING, "aera: malformed password file");
		return DROPBEAR_FAILURE;
	}
	*hash_hex++ = '\0';
	if (strlen(salt_hex) != AERA_SALT_HEX_LEN
			|| strlen(hash_hex) != AERA_HASH_HEX_LEN) {
		dropbear_log(LOG_WARNING, "aera: malformed password file");
		return DROPBEAR_FAILURE;
	}
	if (hex_decode(salt_hex, salt, sizeof(salt)) != DROPBEAR_SUCCESS
			|| hex_decode(hash_hex, expected, sizeof(expected)) != DROPBEAR_SUCCESS) {
		dropbear_log(LOG_WARNING, "aera: malformed password file");
		return DROPBEAR_FAILURE;
	}

	if (sha256_init(&md) != CRYPT_OK
			|| sha256_process(&md, salt, sizeof(salt)) != CRYPT_OK
			|| sha256_process(&md, (const unsigned char *)password, passwordlen) != CRYPT_OK
			|| sha256_done(&md, computed) != CRYPT_OK) {
		dropbear_log(LOG_WARNING, "aera: password hash failed");
		return DROPBEAR_FAILURE;
	}

	if (constant_time_memcmp(computed, expected, sizeof(computed)) == 0) {
		ret = DROPBEAR_SUCCESS;
	}

	m_burn(salt, sizeof(salt));
	m_burn(expected, sizeof(expected));
	m_burn(computed, sizeof(computed));
	m_burn(buf, sizeof(buf));
	return ret;
}

#endif /* DROPBEAR_AERA_PASSWORD_AUTH */
