#pragma once
#define close(a) closesocket(a)

/* Values for the second argument to `fcntl'.  */
#define F_DUPFD         0       /* Duplicate file descriptor.  */
#define F_GETFD         1       /* Get file descriptor flags.  */
#define F_SETFD         2       /* Set file descriptor flags.  */
#define F_GETFL         3       /* Get file status flags.  */
#define F_SETFL         4       /* Set file status flags.  */
#ifndef O_NONBLOCK
# define O_NONBLOCK       04000
#endif
#undef HAVE_RECVMMSG
#define NAME_MAX         255        /* # chars in a file name */

int fcntl(int __fd, int __cmd, ...);
#define EVP_sha256() "SHA256"
unsigned char *HMAC(/*const EVP_MD *evp_md,*/const char *, const void *, int, const unsigned char *, size_t, unsigned char *, unsigned int *);
int kdf_hmac_sha256(char *, uint32_t, unsigned char *, unsigned char *);