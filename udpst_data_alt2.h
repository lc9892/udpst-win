#pragma once
#ifdef UDPST_DATA
#define STDOUT_FILENO   1       /* Standard output.  */
#define LOGFILE_FLAGS (_O_WRONLY | _O_CREAT | _O_APPEND)
#define LOGFILE_MODE  (_S_IREAD | _S_IWRITE)

#define read(a, b, c) _read(a, b, c)
#define write(a, b, c) write_alt(a, b, c)
#define RECVMMSG_SIZE 1 /* Set to minimum (not used) */
#endif
#define HAVE_SENDMMSG

size_t write_alt(int, const char *, size_t);
int socket_error(int, int, char *);
int receive_trunc(int, int, int);
int sendmmsg(int, struct mmsghdr *, unsigned int, int);
long int random(void);
struct iovec {
    void *iov_base;     /* Pointer to data.  */
    size_t iov_len;     /* Length of data.  */
};
struct msghdr {
    void *msg_name;             /* Address to send to/receive from.  */
    socklen_t msg_namelen;      /* Length of address data.  */

    struct iovec *msg_iov;      /* Vector of data to send/receive into.  */
    size_t msg_iovlen;          /* Number of elements in the vector.  */

    void *msg_control;          /* Ancillary data (eg BSD filedesc passing). */
    size_t msg_controllen;      /* Ancillary data buffer length.
                                   !! The type should be socklen_t but the
                                   definition of the kernel is incompatible
                                   with this.  */

    int msg_flags;              /* Flags on received message.  */
};
struct mmsghdr {
    struct msghdr msg_hdr;      /* Actual message header.  */
    unsigned int msg_len;       /* Number of received or sent bytes for the
                                   entry.  */
};
