#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define INVALID_SOCKET ((int)(~0))

typedef int sock_t;
typedef struct stat cs_stat_t;
#define DIRSEP '/'
#define SIZE_T_FMT "lu"
#define INT64_FMT "li"
#define to64(x) strtoll(x, NULL, 10)
#define closesocket(x) close(x)