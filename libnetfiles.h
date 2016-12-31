#ifndef LIBNETFILES_h
#define LIBNETFILES_h

#define UNRE	0
#define EXCL 	1
#define TRAN	2

#define INVALID_FILE_MODE 	-5

int netserverinit(char *hostname, int filemode);
int netopen(const char *pathname, int flags);
int netclose(int fd);
ssize_t netread(int fildes, void *buf, size_t nbyte);
ssize_t netwrite(int fildes, const void *buf, size_t nbyte);

#endif