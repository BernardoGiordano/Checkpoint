#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "ftp.h"

#define lstat stat
#define POLL_UNKNOWN    (~(POLLIN|POLLPRI|POLLOUT))
#define XFER_BUFFERSIZE 65536
#define SOCK_BUFFERSIZE 65536
#define FILE_BUFFERSIZE 1048576
#define CMD_BUFFERSIZE  4096
#define LISTEN_PORT     5000
#define DATA_PORT       0 /* ephemeral port */

typedef struct ftp_session_t ftp_session_t;

#define FTP_DECLARE(x) static void x(ftp_session_t *session, const char *args)
FTP_DECLARE(ABOR);
FTP_DECLARE(ALLO);
FTP_DECLARE(APPE);
FTP_DECLARE(CDUP);
FTP_DECLARE(CWD);
FTP_DECLARE(DELE);
FTP_DECLARE(FEAT);
FTP_DECLARE(HELP);
FTP_DECLARE(LIST);
FTP_DECLARE(MDTM);
FTP_DECLARE(MKD);
FTP_DECLARE(MLSD);
FTP_DECLARE(MLST);
FTP_DECLARE(MODE);
FTP_DECLARE(NLST);
FTP_DECLARE(NOOP);
FTP_DECLARE(OPTS);
FTP_DECLARE(PASS);
FTP_DECLARE(PASV);
FTP_DECLARE(PORT);
FTP_DECLARE(PWD);
FTP_DECLARE(QUIT);
FTP_DECLARE(REST);
FTP_DECLARE(RETR);
FTP_DECLARE(RMD);
FTP_DECLARE(RNFR);
FTP_DECLARE(RNTO);
FTP_DECLARE(SIZE);
FTP_DECLARE(STAT);
FTP_DECLARE(STOR);
FTP_DECLARE(STOU);
FTP_DECLARE(STRU);
FTP_DECLARE(SYST);
FTP_DECLARE(TYPE);
FTP_DECLARE(USER);

/*! session state */
typedef enum {
  COMMAND_STATE,       /*!< waiting for a command */
  DATA_CONNECT_STATE,  /*!< waiting for connection after PASV command */
  DATA_TRANSFER_STATE, /*!< data transfer in progress */
} session_state_t;

/*! ftp_session_set_state flags */
typedef enum {
  CLOSE_PASV = BIT(0), /*!< Close the pasv_fd */
  CLOSE_DATA = BIT(1), /*!< Close the data_fd */
} set_state_flags_t;

/*! ftp_session_t flags */
typedef enum {
  SESSION_BINARY = BIT(0), /*!< data transfers in binary mode */
  SESSION_PASV   = BIT(1), /*!< have pasv_addr ready for data transfer command */
  SESSION_PORT   = BIT(2), /*!< have peer_addr ready for data transfer command */
  SESSION_RECV   = BIT(3), /*!< data transfer in source mode */
  SESSION_SEND   = BIT(4), /*!< data transfer in sink mode */
  SESSION_RENAME = BIT(5), /*!< last command was RNFR and buffer contains path */
  SESSION_URGENT = BIT(6), /*!< in telnet urgent mode */
} session_flags_t;

/*! ftp_xfer_dir mode */
typedef enum {
  XFER_DIR_LIST, /*!< Long list */
  XFER_DIR_MLSD, /*!< Machine list directory */
  XFER_DIR_MLST, /*!< Machine list */
  XFER_DIR_NLST, /*!< Short list */
  XFER_DIR_STAT, /*!< Stat command */
} xfer_dir_mode_t;

typedef enum {
  SESSION_MLST_TYPE      = BIT(0),
  SESSION_MLST_SIZE      = BIT(1),
  SESSION_MLST_MODIFY    = BIT(2),
  SESSION_MLST_PERM      = BIT(3),
  SESSION_MLST_UNIX_MODE = BIT(4),
} session_mlst_flags_t;

/*! ftp session */
struct ftp_session_t {
  char                 cwd[4096];  /*!< current working directory */
  char                 lwd[4096];  /*!< list working directory */
  struct sockaddr_in   peer_addr;  /*!< peer address for data connection */
  struct sockaddr_in   pasv_addr;  /*!< listen address for PASV connection */
  int                  cmd_fd;     /*!< socket for command connection */
  int                  pasv_fd;    /*!< listen socket for PASV */
  int                  data_fd;    /*!< socket for data transfer */
  time_t               timestamp;  /*!< time from last command */
  session_flags_t      flags;      /*!< session flags */
  xfer_dir_mode_t      dir_mode;   /*!< dir transfer mode */
  session_mlst_flags_t mlst_flags; /*!< session MLST flags */
  session_state_t      state;      /*!< session state */
  ftp_session_t        *next;      /*!< link to next session */
  ftp_session_t        *prev;      /*!< link to prev session */

  loop_status_t (*transfer)(ftp_session_t*);  /*! data transfer callback */
  char     buffer[XFER_BUFFERSIZE];      /*! persistent data between callbacks */
  char     file_buffer[FILE_BUFFERSIZE]; /*! stdio file buffer */
  char     cmd_buffer[CMD_BUFFERSIZE];   /*! command buffer */
  size_t   bufferpos;                    /*! persistent buffer position between callbacks */
  size_t   buffersize;                   /*! persistent buffer size between callbacks */
  size_t   cmd_buffersize;
  uint64_t filepos;                      /*! persistent file position between callbacks */
  uint64_t filesize;                     /*! persistent file size between callbacks */
  FILE     *fp;                          /*! persistent open file pointer between callbacks */
  DIR      *dp;                          /*! persistent open directory pointer between callbacks */
};

/*! ftp command descriptor */
typedef struct ftp_command {
  const char *name;                                   /*!< command name */
  void       (*handler)(ftp_session_t*, const char*); /*!< command callback */
} ftp_command_t;

/*! ftp command list */
static ftp_command_t ftp_commands[] = {
/*! ftp command */
#define FTP_COMMAND(x) { #x, x, }
/*! ftp alias */
#define FTP_ALIAS(x,y) { #x, y, }
  FTP_COMMAND(ABOR),
  FTP_COMMAND(ALLO),
  FTP_COMMAND(APPE),
  FTP_COMMAND(CDUP),
  FTP_COMMAND(CWD),
  FTP_COMMAND(DELE),
  FTP_COMMAND(FEAT),
  FTP_COMMAND(HELP),
  FTP_COMMAND(LIST),
  FTP_COMMAND(MDTM),
  FTP_COMMAND(MKD),
  FTP_COMMAND(MLSD),
  FTP_COMMAND(MLST),
  FTP_COMMAND(MODE),
  FTP_COMMAND(NLST),
  FTP_COMMAND(NOOP),
  FTP_COMMAND(OPTS),
  FTP_COMMAND(PASS),
  FTP_COMMAND(PASV),
  FTP_COMMAND(PORT),
  FTP_COMMAND(PWD),
  FTP_COMMAND(QUIT),
  FTP_COMMAND(REST),
  FTP_COMMAND(RETR),
  FTP_COMMAND(RMD),
  FTP_COMMAND(RNFR),
  FTP_COMMAND(RNTO),
  FTP_COMMAND(SIZE),
  FTP_COMMAND(STAT),
  FTP_COMMAND(STOR),
  FTP_COMMAND(STOU),
  FTP_COMMAND(STRU),
  FTP_COMMAND(SYST),
  FTP_COMMAND(TYPE),
  FTP_COMMAND(USER),
  FTP_ALIAS(XCUP, CDUP),
  FTP_ALIAS(XCWD, CWD),
  FTP_ALIAS(XMKD, MKD),
  FTP_ALIAS(XPWD, PWD),
  FTP_ALIAS(XRMD, RMD),
};
/*! number of ftp commands */
static const size_t num_ftp_commands = sizeof(ftp_commands)/sizeof(ftp_commands[0]);

static void update_free_space(void);

/*! compare ftp command descriptors
 *
 *  @param[in] p1 left side of comparison (ftp_command_t*)
 *  @param[in] p2 right side of comparison (ftp_command_t*)
 *
 *  @returns <0 if p1 <  p2
 *  @returns 0 if  p1 == p2
 *  @returns >0 if p1 >  p2
 */
static int ftp_command_cmp(const void *p1, const void *p2) {
  ftp_command_t *c1 = (ftp_command_t*)p1;
  ftp_command_t *c2 = (ftp_command_t*)p2;

  /* ordered by command name */
  return strcasecmp(c1->name, c2->name);
}

/*! server listen address */
static struct sockaddr_in serv_addr;
/*! listen file descriptor */
static int                listenfd = -1;
/*! list of ftp sessions */
static ftp_session_t      *sessions = NULL;
/*! socket buffersize */
static int                sock_buffersize = SOCK_BUFFERSIZE;
/*! server start time */
static time_t             start_time = 0;

/*! Allocate a new data port
 *
 *  @returns next data port
 */
static in_port_t next_data_port(void) {
  return 0; /* ephemeral port */
}

/*! set a socket to non-blocking
 *
 *  @param[in] fd socket
 *
 *  @returns error
 */
static int ftp_set_socket_nonblocking(int fd) {
  int rc, flags;

  /* get the socket flags */
  flags = fcntl(fd, F_GETFL, 0);
  if(flags == -1)
  {
    //console_print(RED "fcntl: %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }

  /* add O_NONBLOCK to the socket flags */
  rc = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  if(rc != 0)
  {
    //console_print(RED "fcntl: %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }

  return 0;
}

/*! set socket options
 *
 *  @param[in] fd socket
 *
 *  @returns failure
 */
static int ftp_set_socket_options(int fd) {
  int rc;

  /* increase receive buffer size */
  rc = setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                  &sock_buffersize, sizeof(sock_buffersize));
  if(rc != 0)
  {
    //console_print(RED "setsockopt: SO_RCVBUF %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }

  /* increase send buffer size */
  rc = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                  &sock_buffersize, sizeof(sock_buffersize));
  if(rc != 0)
  {
    //console_print(RED "setsockopt: SO_SNDBUF %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }

  return 0;
}

/*! close a socket
 *
 *  @param[in] fd        socket to close
 *  @param[in] connected whether this socket is connected
 */
static void ftp_closesocket(int  fd, bool connected) {
  int                rc;
  struct sockaddr_in addr;
  socklen_t          addrlen = sizeof(addr);
  struct pollfd      pollinfo;

//  console_print("0x%X\n", socketGetLastBsdResult());

  if(connected)
  {
    /* get peer address and print */
    rc = getpeername(fd, (struct sockaddr*)&addr, &addrlen);
    /*if(rc != 0)
    {
      console_print(RED "getpeername: %d %s\n" RESET, errno, strerror(errno));
      console_print(YELLOW "closing connection to fd=%d\n" RESET, fd);
    }
    else
      console_print(YELLOW "closing connection to %s:%u\n" RESET,
                    inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));*/

    /* shutdown connection */
    rc = shutdown(fd, SHUT_WR);
    /*if(rc != 0)
      console_print(RED "shutdown: %d %s\n" RESET, errno, strerror(errno));*/

    /* wait for client to close connection */
    pollinfo.fd      = fd;
    pollinfo.events  = POLLIN;
    pollinfo.revents = 0;
    rc = poll(&pollinfo, 1, 250);
    /*if(rc < 0)
      console_print(RED "poll: %d %s\n" RESET, errno, strerror(errno));*/
  }

  /* set linger to 0 */
  struct linger linger;
  linger.l_onoff  = 1;
  linger.l_linger = 0;
  rc = setsockopt(fd, SOL_SOCKET, SO_LINGER,
                  &linger, sizeof(linger));
  /*if(rc != 0)
    console_print(RED "setsockopt: SO_LINGER %d %s\n" RESET,
                  errno, strerror(errno));*/

  /* close socket */
  rc = close(fd);
  /*if(rc != 0)
    console_print(RED "close: %d %s\n" RESET, errno, strerror(errno));*/
}

/*! close command socket on ftp session
 *
 *  @param[in] session ftp session
 */
static void ftp_session_close_cmd(ftp_session_t *session) {
  /* close command socket */
  if(session->cmd_fd >= 0)
    ftp_closesocket(session->cmd_fd, true);
  session->cmd_fd = -1;
}

/*! close listen socket on ftp session
 *
 *  @param[in] session ftp session
 */
static void ftp_session_close_pasv(ftp_session_t *session) {
  /* close pasv socket */
  if(session->pasv_fd >= 0)
  {
    /*console_print(YELLOW "stop listening on %s:%u\n" RESET,
                  inet_ntoa(session->pasv_addr.sin_addr),
                  ntohs(session->pasv_addr.sin_port));*/

    ftp_closesocket(session->pasv_fd, false);
  }

  session->pasv_fd = -1;
}

/*! close data socket on ftp session
 *
 *  @param[in] session ftp session
 */
static void ftp_session_close_data(ftp_session_t *session) {
  /* close data connection */
  if(session->data_fd >= 0 && session->data_fd != session->cmd_fd)
    ftp_closesocket(session->data_fd, true);
  session->data_fd = -1;

  /* clear send/recv flags */
  session->flags &= ~(SESSION_RECV|SESSION_SEND);
}

/*! close open file for ftp session
 *
 *  @param[in] session ftp session
 */
static void ftp_session_close_file(ftp_session_t *session) {
  int rc;

  if(session->fp != NULL)
  {
    rc = fclose(session->fp);
    /*if(rc != 0)
      console_print(RED "fclose: %d %s\n" RESET, errno, strerror(errno));*/
  }

  session->fp      = NULL;
  session->filepos = 0;
}

/*! open file for reading for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns -1 for error
 */
static int ftp_session_open_file_read(ftp_session_t *session) {
  int         rc;
  struct stat st;

  /* open file in read mode */
  session->fp = fopen(session->buffer, "rb");
  if(session->fp == NULL)
  {
    //console_print(RED "fopen '%s': %d %s\n" RESET, session->buffer, errno, strerror(errno));
    return -1;
  }

  /* it's okay if this fails */
  errno = 0;
  rc = setvbuf(session->fp, session->file_buffer, _IOFBF, FILE_BUFFERSIZE);
  /*if(rc != 0)
  {
    console_print(RED "setvbuf: %d %s\n" RESET, errno, strerror(errno));
  }*/

  /* get the file size */
  rc = fstat(fileno(session->fp), &st);
  if(rc != 0)
  {
    //console_print(RED "fstat '%s': %d %s\n" RESET, session->buffer, errno, strerror(errno));
    return -1;
  }
  session->filesize = st.st_size;

  if(session->filepos != 0)
  {
    rc = fseek(session->fp, session->filepos, SEEK_SET);
    if(rc != 0)
    {
      //console_print(RED "fseek '%s': %d %s\n" RESET, session->buffer, errno, strerror(errno));
      return -1;
    }
  }

  return 0;
}

/*! read from an open file for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns bytes read
 */
static ssize_t ftp_session_read_file(ftp_session_t *session) {
  ssize_t rc;

  /* read file at current position */
  rc = fread(session->buffer, 1, sizeof(session->buffer), session->fp);
  if(rc < 0)
  {
    //console_print(RED "fread: %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }

  /* adjust file position */
  session->filepos += rc;

  return rc;
}

/*! open file for writing for ftp session
 *
 *  @param[in] session ftp session
 *  @param[in] append  whether to append
 *
 *  @returns -1 for error
 *
 *  @note truncates file
 */
static int ftp_session_open_file_write(ftp_session_t *session, bool append) {
  int        rc;
  const char *mode = "wb";

  if(append)
    mode = "ab";
  else if(session->filepos != 0)
    mode = "r+b";

  /* open file in write mode */
  session->fp = fopen(session->buffer, mode);
  if(session->fp == NULL)
  {
    //console_print(RED "fopen '%s': %d %s\n" RESET, session->buffer, errno, strerror(errno));
    return -1;
  }

  update_free_space();

  /* it's okay if this fails */
  errno = 0;
  rc = setvbuf(session->fp, session->file_buffer, _IOFBF, FILE_BUFFERSIZE);
  /*if(rc != 0)
  {
    console_print(RED "setvbuf: %d %s\n" RESET, errno, strerror(errno));
  }*/

  /* check if this had REST but not APPE */
  if(session->filepos != 0 && !append)
  {
    /* seek to the REST offset */
    rc = fseek(session->fp, session->filepos, SEEK_SET);
    if(rc != 0)
    {
      //console_print(RED "fseek '%s': %d %s\n" RESET, session->buffer, errno, strerror(errno));
      return -1;
    }
  }

  return 0;
}

/*! write to an open file for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns bytes written
 */
static ssize_t ftp_session_write_file(ftp_session_t *session) {
  ssize_t rc;

  /* write to file at current position */
  rc = fwrite(session->buffer + session->bufferpos,
              1, session->buffersize - session->bufferpos,
              session->fp);
  if(rc < 0)
  {
    //console_print(RED "fwrite: %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }
  /*else if(rc == 0)
    console_print(RED "fwrite: wrote 0 bytes\n" RESET);*/

  /* adjust file position */
  session->filepos += rc;

  update_free_space();
  return rc;
}

/*! close current working directory for ftp session
 *
 *   @param[in] session ftp session
 */
static void ftp_session_close_cwd(ftp_session_t *session) {
  int rc;

  /* close open directory pointer */
  if(session->dp != NULL)
  {
    rc = closedir(session->dp);
    /*if(rc != 0)
      console_print(RED "closedir: %d %s\n" RESET, errno, strerror(errno));*/
  }
  session->dp = NULL;
}

/*! open current working directory for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @return -1 for failure
 */
static int ftp_session_open_cwd(ftp_session_t *session) {
  /* open current working directory */
  session->dp = opendir(session->cwd);
  if(session->dp == NULL)
  {
    //console_print(RED "opendir '%s': %d %s\n" RESET, session->cwd, errno, strerror(errno));
    return -1;
  }

  return 0;
}

/*! set state for ftp session
 *
 *  @param[in] session ftp session
 *  @param[in] state   state to set
 *  @param[in] flags   flags
 */
static void ftp_session_set_state(ftp_session_t *session, session_state_t state, set_state_flags_t flags) {
  session->state = state;

  /* close pasv and data sockets */
  if(flags & CLOSE_PASV)
    ftp_session_close_pasv(session);
  if(flags & CLOSE_DATA)
    ftp_session_close_data(session);

  if(state == COMMAND_STATE)
  {
    /* close file/cwd */
    ftp_session_close_file(session);
    ftp_session_close_cwd(session);
  }
}

/*! fill directory entry
 *
 *  @param[in] session ftp session
 *  @param[in] st      stat data
 *  @param[in] path    path to fill
 *  @param[in] len     path length
 *  @param[in] type    type fact
 *
 *  @returns errno
 */
static int
ftp_session_fill_dirent_type(ftp_session_t *session, const struct stat *st,
                             const char *path, size_t len, const char *type)
{
  session->buffersize = 0;

  if(session->dir_mode == XFER_DIR_MLSD
  || session->dir_mode == XFER_DIR_MLST)
  {
    if(session->dir_mode == XFER_DIR_MLST)
      session->buffer[session->buffersize++] = ' ';

    if(session->mlst_flags & SESSION_MLST_TYPE)
    {
      /* type fact */
      if(!type)
      {
        type = "???";
        if(S_ISREG(st->st_mode))
          type = "file";
        else if(S_ISDIR(st->st_mode))
          type = "dir";
      }

      session->buffersize +=
        sprintf(session->buffer + session->buffersize, "Type=%s;", type);
    }

    if(session->mlst_flags & SESSION_MLST_SIZE)
    {
      /* size fact */
      session->buffersize +=
        sprintf(session->buffer + session->buffersize, "Size=%lld;",
                (signed long long)st->st_size);
    }

    if(session->mlst_flags & SESSION_MLST_MODIFY)
    {
      /* mtime fact */
      struct tm *tm = gmtime(&st->st_mtime);
      if(tm == NULL)
        return errno;

      session->buffersize +=
        strftime(session->buffer + session->buffersize,
                 sizeof(session->buffer) - session->buffersize,
                 "Modify=%Y%m%d%H%M%S;", tm);
      if(session->buffersize == 0)
        return EOVERFLOW;
    }

    if(session->mlst_flags & SESSION_MLST_PERM)
    {
      /* permission fact */
      strcpy(session->buffer + session->buffersize, "Perm=");
      session->buffersize += strlen("Perm=");

      /* append permission */
      if(S_ISREG(st->st_mode) && (st->st_mode & S_IWUSR))
        session->buffer[session->buffersize++] = 'a';

      /* create permission */
      if(S_ISDIR(st->st_mode) && (st->st_mode & S_IWUSR))
        session->buffer[session->buffersize++] = 'c';

      /* delete permission */
      session->buffer[session->buffersize++] = 'd';

      /* chdir permission */
      if(S_ISDIR(st->st_mode) && (st->st_mode & S_IXUSR))
        session->buffer[session->buffersize++] = 'e';

      /* rename permission */
      session->buffer[session->buffersize++] = 'f';

      /* list permission */
      if(S_ISDIR(st->st_mode) && (st->st_mode & S_IRUSR))
        session->buffer[session->buffersize++] = 'l';

      /* mkdir permission */
      if(S_ISDIR(st->st_mode) && (st->st_mode & S_IWUSR))
        session->buffer[session->buffersize++] = 'm';

      /* delete permission */
      if(S_ISDIR(st->st_mode) && (st->st_mode & S_IWUSR))
        session->buffer[session->buffersize++] = 'p';

      /* read permission */
      if(S_ISREG(st->st_mode) && (st->st_mode & S_IRUSR))
        session->buffer[session->buffersize++] = 'r';

      /* write permission */
      if(S_ISREG(st->st_mode) && (st->st_mode & S_IWUSR))
        session->buffer[session->buffersize++] = 'w';

      session->buffer[session->buffersize++] = ';';
    }

    if(session->mlst_flags & SESSION_MLST_UNIX_MODE)
    {
      /* unix mode fact */
      mode_t mask = S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX | S_ISGID | S_ISUID;
      session->buffersize +=
        sprintf(session->buffer + session->buffersize, "UNIX.mode=0%lo;",
                (unsigned long)(st->st_mode & mask));
    }

    /* make sure space precedes name */
    if(session->buffer[session->buffersize-1] != ' ')
      session->buffer[session->buffersize++] = ' ';
  }
  else if(session->dir_mode != XFER_DIR_NLST)
  {
    if(session->dir_mode == XFER_DIR_STAT)
      session->buffer[session->buffersize++] = ' ';

    /* perms nlinks owner group size */
    session->buffersize +=
      sprintf(session->buffer + session->buffersize,
              "%c%c%c%c%c%c%c%c%c%c %lu nx nx %lld ",
              S_ISREG(st->st_mode)  ? '-' :
              S_ISDIR(st->st_mode)  ? 'd' : '?',
              st->st_mode & S_IRUSR ? 'r' : '-',
              st->st_mode & S_IWUSR ? 'w' : '-',
              st->st_mode & S_IXUSR ? 'x' : '-',
              st->st_mode & S_IRGRP ? 'r' : '-',
              st->st_mode & S_IWGRP ? 'w' : '-',
              st->st_mode & S_IXGRP ? 'x' : '-',
              st->st_mode & S_IROTH ? 'r' : '-',
              st->st_mode & S_IWOTH ? 'w' : '-',
              st->st_mode & S_IXOTH ? 'x' : '-',
              (unsigned long)st->st_nlink,
              (signed long long)st->st_size);

    /* timestamp */
    struct tm *tm = gmtime(&st->st_mtime);
    if(tm)
    {
      const char *fmt = "%b %e %Y ";
      if(session->timestamp > st->st_mtime
      && session->timestamp - st->st_mtime < (60*60*24*365/2))
      {
        fmt = "%b %e %H:%M ";
      }

      session->buffersize +=
        strftime(session->buffer + session->buffersize,
                 sizeof(session->buffer) - session->buffersize,
                 fmt, tm);
    }
    else
    {
      session->buffersize +=
        sprintf(session->buffer + session->buffersize, "Jan 1 1970 ");
    }
  }

  if(session->buffersize + len + 2 > sizeof(session->buffer))
  {
    /* buffer will overflow */
    return EOVERFLOW;
  }

  /* copy path */
  memcpy(session->buffer+session->buffersize, path, len);
  len = session->buffersize + len;
  session->buffer[len++] = '\r';
  session->buffer[len++] = '\n';
  session->buffersize = len;

  return 0;
}

/*! fill directory entry
 *
 *  @param[in] session ftp session
 *  @param[in] st      stat data
 *  @param[in] path    path to fill
 *  @param[in] len     path length
 *
 *  @returns errno
 */
static int ftp_session_fill_dirent(ftp_session_t *session, const struct stat *st, const char *path, size_t len) {
  return ftp_session_fill_dirent_type(session, st, path, len, NULL);
}

/*! transfer loop
 *
 *  Try to transfer as much data as the sockets will allow without blocking
 *
 *  @param[in] session ftp session
 */
static void ftp_session_transfer(ftp_session_t *session) {
  int rc;
  do
  {
    rc = session->transfer(session);
  } while(rc == 0);
}

/*! encode a path
 *
 *  @param[in]     path   path to encode
 *  @param[in,out] len    path length
 *  @param[in]     quotes whether to encode quotes
 *
 *  @returns encoded path
 *
 *  @note The caller must free the returned path
 */
static char* encode_path(const char *path, size_t *len, bool quotes) {
  bool   enc = false;
  size_t i, diff = 0;
  char   *out, *p = (char*)path;

  /* check for \n that needs to be encoded */
  if(memchr(p, '\n', *len) != NULL)
    enc = true;

  if(quotes)
  {
    /* check for " that needs to be encoded */
    p = (char*)path;
    do
    {
      p = memchr(p, '"', path + *len - p);
      if(p != NULL)
      {
        ++p;
        ++diff;
      }
    } while(p != NULL);
  }

  /* check if an encode was needed */
  if(!enc && diff == 0)
    return strdup(path);

  /* allocate space for encoded path */
  p = out = (char*)malloc(*len + diff);
  if(out == NULL)
    return NULL;

  /* copy the path while performing encoding */
  for(i = 0; i < *len; ++i)
  {
    if(*path == '\n')
    {
      /* encoded \n is \0 */
      *p++ = 0;
    }
    else if(quotes && *path == '"')
    {
      /* encoded " is "" */
      *p++ = '"';
      *p++ = '"';
    }
    else
      *p++ = *path;
    ++path;
  }

  *len += diff;
  return out;
}

/*! decode a path
 *
 *  @param[in] session ftp session
 *  @param[in] len     command length
 */
static void decode_path(ftp_session_t *session, size_t len) {
  size_t i;

  /* decode \0 from the first command */
  for(i = 0; i < len; ++i)
  {
    /* this is an encoded \n */
    if(session->cmd_buffer[i] == 0)
      session->cmd_buffer[i] = '\n';
  }
}

/*! fill cdir directory entry
 *
 *  @param[in] session ftp session
 *  @param[in] path    path to fill
 *
 *  @returns errno
 */
static int ftp_session_fill_dirent_cdir(ftp_session_t *session, const char *path) {
  int         rc;
  struct stat st;
  char        *buffer;
  size_t      len;

  rc = stat(path, &st);
  /* double-check this was a directory */
  if(rc == 0 && !S_ISDIR(st.st_mode))
  {
    /* shouldn't happen but just in case */
    rc = -1;
    errno = ENOTDIR;
  }
  if(rc != 0)
    return errno;

  /* encode \n in path */
  len = strlen(path);
  buffer = encode_path(path, &len, false);
  if(!buffer)
    return ENOMEM;

  /* fill dirent with listed directory as type=cdir */
  rc = ftp_session_fill_dirent_type(session, &st, buffer, len, "cdir");
  free(buffer);

  return rc;
}
/*! send a response on the command socket
 *
 *  @param[in] session ftp session
 *  @param[in] buffer  buffer to send
 *  @param[in] len     buffer length
 */
static void ftp_send_response_buffer(ftp_session_t *session, const char *buffer, size_t len) {
  ssize_t rc, to_send;

  if(session->cmd_fd < 0)
    return;

  /* send response */
  to_send = len;
  //console_print(GREEN "%s" RESET, buffer);
  rc = send(session->cmd_fd, buffer, to_send, 0);
  if(rc < 0)
  {
    //console_print(RED "send: %d %s\n" RESET, errno, strerror(errno));
    ftp_session_close_cmd(session);
  }
  else if(rc != to_send)
  {
    /*console_print(RED "only sent %u/%u bytes\n" RESET,
                  (unsigned int)rc, (unsigned int)to_send);*/
    ftp_session_close_cmd(session);
  }
}

__attribute__((format(printf,3,4)))
/*! send ftp response to ftp session's peer
 *
 *  @param[in] session ftp session
 *  @param[in] code    response code
 *  @param[in] fmt     format string
 *  @param[in] ...     format arguments
 */
static void ftp_send_response(ftp_session_t *session, int code, const char *fmt, ...) {
  static char buffer[CMD_BUFFERSIZE];
  ssize_t     rc;
  va_list     ap;

  if(session->cmd_fd < 0)
    return;

  /* print response code and message to buffer */
  va_start(ap, fmt);
  if(code > 0)
    rc = sprintf(buffer, "%d ", code);
  else
    rc = sprintf(buffer, "%d-", -code);
  rc += vsnprintf(buffer+rc, sizeof(buffer)-rc, fmt, ap);
  va_end(ap);

  if(rc >= sizeof(buffer))
  {
    /* couldn't fit message; just send code */
    //console_print(RED "%s: buffersize too small\n" RESET, __func__);
    if(code > 0)
      rc = sprintf(buffer, "%d \r\n", code);
    else
      rc = sprintf(buffer, "%d-\r\n", -code);
  }

  ftp_send_response_buffer(session, buffer, rc);
}

/*! destroy ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns the next session in the list
 */
static ftp_session_t* ftp_session_destroy(ftp_session_t *session) {
  ftp_session_t *next = session->next;

  /* close all sockets/files */
  ftp_session_close_cmd(session);
  ftp_session_close_pasv(session);
  ftp_session_close_data(session);
  ftp_session_close_file(session);
  ftp_session_close_cwd(session);

  /* unlink from sessions list */
  if(session->next)
    session->next->prev = session->prev;
  if(session == sessions)
    sessions = session->next;
  else
  {
    session->prev->next = session->next;
    if(session == sessions->prev)
      sessions->prev = session->prev;
  }

  /* deallocate */
  free(session);

  return next;
}

/*! allocate new ftp session
 *
 *  @param[in] listen_fd socket to accept connection from
 */
static void ftp_session_new(int listen_fd) {
  ssize_t            rc;
  int                new_fd;
  ftp_session_t      *session;
  struct sockaddr_in addr;
  socklen_t          addrlen = sizeof(addr);

  /* accept connection */
  new_fd = accept(listen_fd, (struct sockaddr*)&addr, &addrlen);
  if(new_fd < 0)
  {
    //console_print(RED "accept: %d %s\n" RESET, errno, strerror(errno));
    return;
  }

  /*console_print(CYAN "accepted connection from %s:%u\n" RESET,
                inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));*/

  /* allocate a new session */
  session = (ftp_session_t*)calloc(1, sizeof(ftp_session_t));
  if(session == NULL)
  {
    //console_print(RED "failed to allocate session\n" RESET);
    ftp_closesocket(new_fd, true);
    return;
  }

  /* initialize session */
  strcpy(session->cwd, "/");
  session->peer_addr.sin_addr.s_addr = INADDR_ANY;
  session->cmd_fd     = new_fd;
  session->pasv_fd    = -1;
  session->data_fd    = -1;
  session->mlst_flags = SESSION_MLST_TYPE
                      | SESSION_MLST_SIZE
                      | SESSION_MLST_MODIFY
                      | SESSION_MLST_PERM;
  session->state      = COMMAND_STATE;

  /* link to the sessions list */
  if(sessions == NULL)
  {
    sessions = session;
    session->prev = session;
  }
  else
  {
    sessions->prev->next = session;
    session->prev        = sessions->prev;
    sessions->prev       = session;
  }

  /* copy socket address to pasv address */
  addrlen = sizeof(session->pasv_addr);
  rc = getsockname(new_fd, (struct sockaddr*)&session->pasv_addr, &addrlen);
  if(rc != 0)
  {
    //console_print(RED "getsockname: %d %s\n" RESET, errno, strerror(errno));
    ftp_send_response(session, 451, "Failed to get connection info\r\n");
    ftp_session_destroy(session);
    return;
  }

  session->cmd_fd = new_fd;

  /* send initiator response */
  ftp_send_response(session, 220, "Hello!\r\n");
}

/*! accept PASV connection for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns -1 for failure
 */
static int ftp_session_accept(ftp_session_t *session) {
  int                rc, new_fd;
  struct sockaddr_in addr;
  socklen_t          addrlen = sizeof(addr);

  if(session->flags & SESSION_PASV)
  {
    /* clear PASV flag */
    session->flags &= ~SESSION_PASV;

    /* tell the peer that we're ready to accept the connection */
    ftp_send_response(session, 150, "Ready\r\n");

    /* accept connection from peer */
    new_fd = accept(session->pasv_fd, (struct sockaddr*)&addr, &addrlen);
    if(new_fd < 0)
    {
      //console_print(RED "accept: %d %s\n" RESET, errno, strerror(errno));
      ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
      ftp_send_response(session, 425, "Failed to establish connection\r\n");
      return -1;
    }

    /* set the socket to non-blocking */
    rc = ftp_set_socket_nonblocking(new_fd);
    if(rc != 0)
    {
      ftp_closesocket(new_fd, true);
      ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
      ftp_send_response(session, 425, "Failed to establish connection\r\n");
      return -1;
    }

    /*console_print(CYAN "accepted connection from %s:%u\n" RESET,
                  inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));*/

    /* we are ready to transfer data */
    ftp_session_set_state(session, DATA_TRANSFER_STATE, CLOSE_PASV);
    session->data_fd = new_fd;

    return 0;
  }
  else
  {
    /* peer didn't send PASV command */
    ftp_send_response(session, 503, "Bad sequence of commands\r\n");
    return -1;
  }
}

/*! connect to peer for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns -1 for failure
 */
static int ftp_session_connect(ftp_session_t *session) {
  int rc;

  /* clear PORT flag */
  session->flags &= ~SESSION_PORT;

  /* create a new socket */
  session->data_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(session->data_fd < 0)
  {
    //console_print(RED "socket: %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }

  /* set socket options */
  rc = ftp_set_socket_options(session->data_fd);
  if(rc != 0)
  {
    ftp_closesocket(session->data_fd, false);
    session->data_fd = -1;
    return -1;
  }

  /* set socket to non-blocking */
  rc = ftp_set_socket_nonblocking(session->data_fd);
  if(rc != 0)
    return -1;

  /* connect to peer */
  rc = connect(session->data_fd, (struct sockaddr*)&session->peer_addr,
               sizeof(session->peer_addr));
  if(rc != 0)
  {
    if(errno != EINPROGRESS)
    {
      //console_print(RED "connect: %d %s\n" RESET, errno, strerror(errno));
      ftp_closesocket(session->data_fd, false);
      session->data_fd = -1;
      return -1;
    }
  }
  else
  {
    /*console_print(CYAN "connected to %s:%u\n" RESET,
                  inet_ntoa(session->peer_addr.sin_addr),
                  ntohs(session->peer_addr.sin_port));*/

    ftp_session_set_state(session, DATA_TRANSFER_STATE, CLOSE_PASV);
    ftp_send_response(session, 150, "Ready\r\n");
  }

  return 0;
}

/*! read command for ftp session
 *
 *  @param[in] session ftp session
 *  @param[in] events  poll events
 */
static void ftp_session_read_command(ftp_session_t *session, int events) {
  char          *buffer, *args, *next = NULL;
  size_t        i, len;
  int           atmark;
  ssize_t       rc;
  ftp_command_t key, *command;

  /* check out-of-band data */
  if(events & POLLPRI)
  {
    session->flags |= SESSION_URGENT;

    /* check if we are at the urgent marker */
    atmark = sockatmark(session->cmd_fd);
    if(atmark < 0)
    {
      //console_print(RED "sockatmark: %d %s\n" RESET, errno, strerror(errno));
      ftp_session_close_cmd(session);
      return;
    }

    if(!atmark)
    {
      /* discard in-band data */
      rc = recv(session->cmd_fd, session->cmd_buffer, sizeof(session->cmd_buffer), 0);
      if(rc < 0 && errno != EWOULDBLOCK)
      {
        //console_print(RED "recv: %d %s\n" RESET, errno, strerror(errno));
        ftp_session_close_cmd(session);
      }

      return;
    }

    /* retrieve the urgent data */
    rc = recv(session->cmd_fd, session->cmd_buffer, sizeof(session->cmd_buffer), MSG_OOB);
    if(rc < 0)
    {
      /* EWOULDBLOCK means out-of-band data is on the way */
      if(errno == EWOULDBLOCK)
        return;

      /* error retrieving out-of-band data */
      //console_print(RED "recv (oob): %d %s\n" RESET, errno, strerror(errno));
      ftp_session_close_cmd(session);
      return;
    }

    /* reset the command buffer */
    session->cmd_buffersize = 0;
    return;
  }

  /* prepare to receive data */
  buffer = session->cmd_buffer + session->cmd_buffersize;
  len    = sizeof(session->cmd_buffer) - session->cmd_buffersize;
  if(len == 0)
  {
    /* error retrieving command */
    //console_print(RED "Exceeded command buffer size\n" RESET);
    ftp_session_close_cmd(session);
    return;
  }

  /* retrieve command data */
  rc = recv(session->cmd_fd, buffer, len, 0);
  if(rc < 0)
  {
    /* error retrieving command */
    //console_print(RED "recv: %d %s\n" RESET, errno, strerror(errno));
    ftp_session_close_cmd(session);
    return;
  }
  if(rc == 0)
  {
    /* peer closed connection */
    // printf("peer closed connection\n");
    ftp_session_close_cmd(session);
    return;
  }
  else
  {
    session->cmd_buffersize += rc;
    len                      = sizeof(session->cmd_buffer) - session->cmd_buffersize;

    if(session->flags & SESSION_URGENT)
    {
      /* look for telnet data mark */
      for(i = 0; i < session->cmd_buffersize; ++i)
      {
        if((unsigned char)session->cmd_buffer[i] == 0xF2)
        {
          /* ignore all data that precedes the data mark */
          if(i < session->cmd_buffersize - 1)
            memmove(session->cmd_buffer, session->cmd_buffer + i + 1, len - i - 1);
          session->cmd_buffersize -= i + 1;
          session->flags &= ~SESSION_URGENT;
          break;
        }
      }
    }

    /* loop through commands */
    while(true)
    {
      /* must have at least enough data for the delimiter */
      if(session->cmd_buffersize < 1)
        return;

      /* look for \r\n or \n delimiter */
      for(i = 0; i < session->cmd_buffersize; ++i)
      {
        if(i < session->cmd_buffersize-1
        && session->cmd_buffer[i]   == '\r'
        && session->cmd_buffer[i+1] == '\n')
        {
          /* we found a \r\n delimiter */
          session->cmd_buffer[i] = 0;
          next = &session->cmd_buffer[i+2];
          break;
        }
        else if(session->cmd_buffer[i] == '\n')
        {
          /* we found a \n delimiter */
          session->cmd_buffer[i] = 0;
          next = &session->cmd_buffer[i+1];
          break;
        }
      }

      /* check if a delimiter was found */
      if(i == session->cmd_buffersize)
        return;

      /* decode the command */
      decode_path(session, i);

      /* split command from arguments */
      args = buffer = session->cmd_buffer;
      while(*args && !isspace((int)*args))
        ++args;
      if(*args)
        *args++ = 0;

      /* look up the command */
      key.name = buffer;
      command = bsearch(&key, ftp_commands,
                        num_ftp_commands, sizeof(ftp_command_t),
                        ftp_command_cmp);

      /* update command timestamp */
      session->timestamp = time(NULL);

      /* execute the command */
      if(command == NULL)
      {
        /* send header */
        ftp_send_response(session, 502, "Invalid command \"");

        /* send command */
        len = strlen(buffer);
        buffer = encode_path(buffer, &len, false);
        if(buffer != NULL)
          ftp_send_response_buffer(session, buffer, len);
        else
          ftp_send_response_buffer(session, key.name, strlen(key.name));
        free(buffer);

        /* send args (if any) */
        if(*args != 0)
        {
          ftp_send_response_buffer(session, " ", 1);

          len = strlen(args);
          buffer = encode_path(args, &len, false);
          if(buffer != NULL)
            ftp_send_response_buffer(session, buffer, len);
          else
            ftp_send_response_buffer(session, args, strlen(args));
          free(buffer);
        }

        /* send footer */
        ftp_send_response_buffer(session, "\"\r\n", 3);
      }
      else if(session->state != COMMAND_STATE)
      {
        /* only some commands are available during data transfer */
        if(strcasecmp(command->name, "ABOR") != 0
        && strcasecmp(command->name, "STAT") != 0
        && strcasecmp(command->name, "QUIT") != 0)
        {
          ftp_send_response(session, 503, "Invalid command during transfer\r\n");
          ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
          ftp_session_close_cmd(session);
        }
        else
          command->handler(session, args);
      }
      else
      {
        /* clear RENAME flag for all commands except RNTO */
        if(strcasecmp(command->name, "RNTO") != 0)
          session->flags &= ~SESSION_RENAME;

        command->handler(session, args);
      }

      /* remove executed command from the command buffer */
      len = session->cmd_buffer + session->cmd_buffersize - next;
      if(len > 0)
        memmove(session->cmd_buffer, next, len);
      session->cmd_buffersize = len;
    }
  }
}

/*! poll sockets for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns next session
 */
static ftp_session_t* ftp_session_poll(ftp_session_t *session) {
  int           rc;
  struct pollfd pollinfo[2];
  nfds_t        nfds = 1;

  /* the first pollfd is the command socket */
  pollinfo[0].fd      = session->cmd_fd;
  pollinfo[0].events  = POLLIN | POLLPRI;
  pollinfo[0].revents = 0;

  switch(session->state)
  {
    case COMMAND_STATE:
      /* we are waiting to read a command */
      break;

    case DATA_CONNECT_STATE:
      if(session->flags & SESSION_PASV)
      {
        /* we are waiting for a PASV connection */
        pollinfo[1].fd     = session->pasv_fd;
        pollinfo[1].events = POLLIN;
      }
      else
      {
        /* we are waiting to complete a PORT connection */
        pollinfo[1].fd     = session->data_fd;
        pollinfo[1].events = POLLOUT;
      }
      pollinfo[1].revents = 0;
      nfds = 2;
      break;

    case DATA_TRANSFER_STATE:
      /* we need to transfer data */
      pollinfo[1].fd     = session->data_fd;
      if(session->flags & SESSION_RECV)
        pollinfo[1].events = POLLIN;
      else
        pollinfo[1].events = POLLOUT;
      pollinfo[1].revents = 0;
      nfds = 2;
      break;
  }

  /* poll the selected sockets */
  rc = poll(pollinfo, nfds, 0);
  if(rc < 0)
  {
    //console_print(RED "poll: %d %s\n" RESET, errno, strerror(errno));
    ftp_session_close_cmd(session);
  }
  else if(rc > 0)
  {
    /* check the command socket */
    if(pollinfo[0].revents != 0)
    {
      /* handle command */
      /*if(pollinfo[0].revents & POLL_UNKNOWN)
        console_print(YELLOW "cmd_fd: revents=0x%08X\n" RESET, pollinfo[0].revents);*/

      /* we need to read a new command */
      if(pollinfo[0].revents & (POLLERR|POLLHUP))
      {
        // printf("cmd revents=0x%x\n", pollinfo[0].revents);
        ftp_session_close_cmd(session);
      }
      else if(pollinfo[0].revents & (POLLIN | POLLPRI))
        ftp_session_read_command(session, pollinfo[0].revents);
    }

    /* check the data/pasv socket */
    if(nfds > 1 && pollinfo[1].revents != 0)
    {
      switch(session->state)
      {
        case COMMAND_STATE:
          /* this shouldn't happen? */
          break;

        case DATA_CONNECT_STATE:
          /*if(pollinfo[1].revents & POLL_UNKNOWN)
            console_print(YELLOW "pasv_fd: revents=0x%08X\n" RESET, pollinfo[1].revents);*/

          /* we need to accept the PASV connection */
          if(pollinfo[1].revents & (POLLERR|POLLHUP))
          {
            ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
            ftp_send_response(session, 426, "Data connection failed\r\n");
          }
          else if(pollinfo[1].revents & POLLIN)
          {
            if(ftp_session_accept(session) != 0)
              ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
          }
          else if(pollinfo[1].revents & POLLOUT)
          {

            /*console_print(CYAN "connected to %s:%u\n" RESET,
                          inet_ntoa(session->peer_addr.sin_addr),
                          ntohs(session->peer_addr.sin_port));*/

            ftp_session_set_state(session, DATA_TRANSFER_STATE, CLOSE_PASV);
            ftp_send_response(session, 150, "Ready\r\n");
          }
          break;

        case DATA_TRANSFER_STATE:
          /*if(pollinfo[1].revents & POLL_UNKNOWN)
            console_print(YELLOW "data_fd: revents=0x%08X\n" RESET, pollinfo[1].revents);*/

          /* we need to transfer data */
          if(pollinfo[1].revents & (POLLERR|POLLHUP))
          {
            ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
            ftp_send_response(session, 426, "Data connection failed\r\n");
          }
          else if(pollinfo[1].revents & (POLLIN|POLLOUT))
            ftp_session_transfer(session);
          break;
      }
    }
  }

  /* still connected to peer; return next session */
  if(session->cmd_fd >= 0)
    return session->next;

  /* disconnected from peer; destroy it and return next session */
  // printf("disconnected from peer\n");
  return ftp_session_destroy(session);
}

/* Update free space in status bar */
static void update_free_space(void) {
#define KiB (1024.0)
#define MiB (1024.0*KiB)
#define GiB (1024.0*MiB)
  char           buffer[16];
  struct statvfs st;
  double         bytes_free;
  int            rc, len;

  rc = statvfs("sdmc:/", &st);
  /*if(rc != 0)
    console_print(RED "statvfs: %d %s\n" RESET, errno, strerror(errno));
  else
  {
    bytes_free = (double)st.f_bsize * st.f_bfree;

    if     (bytes_free < 1000.0)
      len = snprintf(buffer, sizeof(buffer), "%.0lfB", bytes_free);
    else if(bytes_free < 10.0*KiB)
      len = snprintf(buffer, sizeof(buffer), "%.2lfKiB", floor((bytes_free*100.0)/KiB)/100.0);
    else if(bytes_free < 100.0*KiB)
      len = snprintf(buffer, sizeof(buffer), "%.1lfKiB", floor((bytes_free*10.0)/KiB)/10.0);
    else if(bytes_free < 1000.0*KiB)
      len = snprintf(buffer, sizeof(buffer), "%.0lfKiB", floor(bytes_free/KiB));
    else if(bytes_free < 10.0*MiB)
      len = snprintf(buffer, sizeof(buffer), "%.2lfMiB", floor((bytes_free*100.0)/MiB)/100.0);
    else if(bytes_free < 100.0*MiB)
      len = snprintf(buffer, sizeof(buffer), "%.1lfMiB", floor((bytes_free*10.0)/MiB)/10.0);
    else if(bytes_free < 1000.0*MiB)
      len = snprintf(buffer, sizeof(buffer), "%.0lfMiB", floor(bytes_free/MiB));
    else if(bytes_free < 10.0*GiB)
      len = snprintf(buffer, sizeof(buffer), "%.2lfGiB", floor((bytes_free*100.0)/GiB)/100.0);
    else if(bytes_free < 100.0*GiB)
      len = snprintf(buffer, sizeof(buffer), "%.1lfGiB", floor((bytes_free*10.0)/GiB)/10.0);
    else
      len = snprintf(buffer, sizeof(buffer), "%.0lfGiB", floor(bytes_free/GiB));

    console_set_status("\x1b[0;%dH" GREEN "%s", 50-len, buffer);
  }*/
}

/*! Update status bar */
static int update_status(void) {
  /*console_set_status("\n" GREEN STATUS_STRING " "
#ifdef ENABLE_LOGGING
                     "DEBUG "
#endif
                     CYAN "%s:%u" RESET,
                     inet_ntoa(serv_addr.sin_addr),
                     ntohs(serv_addr.sin_port));*/
  update_free_space();
  return 0;
}

/*! initialize ftp subsystem */
int ftp_init(void) {
  int rc;

  start_time = time(NULL);

  /* allocate socket to listen for clients */
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if(listenfd < 0)
  {
    //console_print(RED "socket: %d %s\n" RESET, errno, strerror(errno));
    ftp_exit();
    return -1;
  }

  /* get address to listen on */
  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_addr.s_addr = gethostid();
  serv_addr.sin_port        = htons(LISTEN_PORT);

  /* reuse address */
  {
    int yes = 1;
    rc = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if(rc != 0)
    {
      //console_print(RED "setsockopt: %d %s\n" RESET, errno, strerror(errno));
      ftp_exit();
      return -1;
    }
  }

  /* bind socket to listen address */
  rc = bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
  if(rc != 0)
  {
    //console_print(RED "bind: %d %s\n" RESET, errno, strerror(errno));
    ftp_exit();
    return -1;
  }

  /* listen on socket */
  rc = listen(listenfd, 5);
  if(rc != 0)
  {
    //console_print(RED "listen: %d %s\n" RESET, errno, strerror(errno));
    ftp_exit();
    return -1;
  }

  /* print server address */
  rc = update_status();
  if(rc != 0)
  {
    ftp_exit();
    return -1;
  }

  return 0;
}

/*! deinitialize ftp subsystem */
void ftp_exit(void) {
  // printf("exiting ftp server\n");

  /* clean up all sessions */
  while(sessions != NULL)
    ftp_session_destroy(sessions);

  /* stop listening for new clients */
  if(listenfd >= 0)
    ftp_closesocket(listenfd, false);

  /* deinitialize socket driver */
  //console_render();
  //console_print(CYAN "Waiting for socketExit()...\n" RESET);
}

/*! ftp look
 *
 *  @returns whether to keep looping
 */
loop_status_t ftp_loop(void) {
  int           rc;
  struct pollfd pollinfo;
  ftp_session_t *session;

  /* we will poll for new client connections */
  pollinfo.fd      = listenfd;
  pollinfo.events  = POLLIN;
  pollinfo.revents = 0;

  /* poll for a new client */
  rc = poll(&pollinfo, 1, 0);
  if(rc < 0)
  {
    /* wifi got disabled */
    if(errno == ENETDOWN)
      return LOOP_RESTART;

    //console_print(RED "poll: %d %s\n" RESET, errno, strerror(errno));
    return LOOP_EXIT;
  }
  else if(rc > 0)
  {
    if(pollinfo.revents & POLLIN)
    {
      /* we got a new client */
      ftp_session_new(listenfd);
    }
    /*else
    {
      console_print(YELLOW "listenfd: revents=0x%08X\n" RESET, pollinfo.revents);
    }*/
  }

  /* poll each session */
  session = sessions;
  while(session != NULL)
    session = ftp_session_poll(session);

  return LOOP_CONTINUE;
}

/*! change to parent directory
 *
 *  @param[in] session ftp session
 */
static void cd_up(ftp_session_t *session) {
  char *slash = NULL, *p;

  /* remove basename from cwd */
  for(p = session->cwd; *p; ++p)
  {
    if(*p == '/')
      slash = p;
  }
  *slash = 0;
  if(strlen(session->cwd) == 0)
    strcat(session->cwd, "/");
}

/*! validate a path
 *
 *  @param[in] args path to validate
 */
static int validate_path(const char *args) {
  const char *p;

  /* make sure no path components are '..' */
  p = args;
  while((p = strstr(p, "/..")) != NULL)
  {
    if(p[3] == 0 || p[3] == '/')
      return -1;
  }

  /* make sure there are no '//' */
  if(strstr(args, "//") != NULL)
    return -1;

  return 0;
}

/*! get a path relative to cwd
 *
 *  @param[in] session ftp session
 *  @param[in] cwd     working directory
 *  @param[in] args    path to make
 *
 *  @returns error
 *
 *  @note the output goes to session->buffer
 */
static int build_path(ftp_session_t *session, const char *cwd, const char *args) {
  int  rc;
  char *p;

  session->buffersize = 0;
  memset(session->buffer, 0, sizeof(session->buffer));

  /* make sure the input is a valid path */
  if(validate_path(args) != 0)
  {
    errno = EINVAL;
    return -1;
  }

  if(args[0] == '/')
  {
    /* this is an absolute path */
    size_t len = strlen(args);
    if(len > sizeof(session->buffer)-1)
    {
      errno = ENAMETOOLONG;
      return -1;
    }

    memcpy(session->buffer, args, len);
    session->buffersize = len;
  }
  else
  {
    /* this is a relative path */
    if(strcmp(cwd, "/") == 0)
      rc = snprintf(session->buffer, sizeof(session->buffer), "/%s",
                    args);
    else
      rc = snprintf(session->buffer, sizeof(session->buffer), "%s/%s",
                    cwd, args);

    if(rc >= sizeof(session->buffer))
    {
      errno = ENAMETOOLONG;
      return -1;
    }

    session->buffersize = rc;
  }

  /* remove trailing / */
  p = session->buffer + session->buffersize;
  while(p > session->buffer && *--p == '/')
  {
    *p = 0;
    --session->buffersize;
  }

  /* if we ended with an empty path, it is the root directory */
  if(session->buffersize == 0)
    session->buffer[session->buffersize++] = '/';

  return 0;
}

/*! transfer a directory listing
 *
 *  @param[in] session ftp session
 *
 *  @returns whether to call again
 */
static loop_status_t list_transfer(ftp_session_t *session) {
  ssize_t       rc;
  size_t        len;
  char          *buffer;
  struct stat   st;
  struct dirent *dent;

  /* check if we sent all available data */
  if(session->bufferpos == session->buffersize)
  {
    /* check xfer dir type */
    if(session->dir_mode == XFER_DIR_STAT)
      rc = 213;
    else {
      rc = 226;
    }

    /* check if this was for a file */
    if(session->dp == NULL)
    {
      /* we already sent the file's listing */
      ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
      ftp_send_response(session, rc, "OK\r\n");
      return LOOP_EXIT;
    }

    /* get the next directory entry */
    dent = readdir(session->dp);
    if(dent == NULL)
    {
      /* we have exhausted the directory listing */
      ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
      ftp_send_response(session, rc, "OK\r\n");
      return LOOP_EXIT;
    }

    /* TODO I think we are supposed to return entries for . and .. */
    if(strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
      return LOOP_CONTINUE;

    /* check if this was a NLST */
    if(session->dir_mode == XFER_DIR_NLST)
    {
      /* NLST gives the whole path name */
      session->buffersize = 0;
      if(build_path(session, session->lwd, dent->d_name) == 0)
      {
        /* encode \n in path */
        len = session->buffersize;
        buffer = encode_path(session->buffer, &len, false);
        if(buffer != NULL)
        {
          /* copy to the session buffer to send */
          memcpy(session->buffer, buffer, len);
          free(buffer);
          session->buffer[len++] = '\r';
          session->buffer[len++] = '\n';
          session->buffersize = len;
        }
      }
    }
    else
    {
      /* lstat the entry */
      if((rc = build_path(session, session->lwd, dent->d_name)) != 0) {}
        // printf("build_path: %d %s\n", errno, strerror(errno));
        //console_print(RED "build_path: %d %s\n" RESET, errno, strerror(errno));
      else if((rc = lstat(session->buffer, &st)) != 0) {}
        // printf("stat '%s': %d %s\n", session->buffer, errno, strerror(errno));
        //console_print(RED "stat '%s': %d %s\n" RESET, session->buffer, errno, strerror(errno));

      if(rc != 0)
      {
        /* an error occurred */
        ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        ftp_send_response(session, 550, "unavailable\r\n");
        return LOOP_EXIT;
      }
      /* encode \n in path */
      len = strlen(dent->d_name);
      buffer = encode_path(dent->d_name, &len, false);
      if(buffer != NULL)
      {
        rc = ftp_session_fill_dirent(session, &st, buffer, len);
        free(buffer);
        if(rc != 0)
        {
          ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
          ftp_send_response(session, 425, "%s\r\n", strerror(rc));
          return LOOP_EXIT;
        }
      }
      else
        session->buffersize = 0;
    }
    session->bufferpos = 0;
  }

  /* send any pending data */
  rc = send(session->data_fd, session->buffer + session->bufferpos,
            session->buffersize - session->bufferpos, 0);
  if(rc <= 0)
  {
    /* error sending data */
    if(rc < 0)
    {
      if(errno == EWOULDBLOCK)
        return LOOP_EXIT;
      //console_print(RED "send: %d %s\n" RESET, errno, strerror(errno));
    }
    /*else
      console_print(YELLOW "send: %d %s\n" RESET, ECONNRESET, strerror(ECONNRESET));*/

    ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
    ftp_send_response(session, 426, "Connection broken during transfer\r\n");
    return LOOP_EXIT;
  }

  /* we can try to send more data */
  session->bufferpos += rc;
  return LOOP_CONTINUE;
}

/*! send a file to the client
 *
 *  @param[in] session ftp session
 *
 *  @returns whether to call again
 */
static loop_status_t retrieve_transfer(ftp_session_t *session) {
  ssize_t rc;

  if(session->bufferpos == session->buffersize)
  {
    /* we have sent all the data so read some more */
    rc = ftp_session_read_file(session);
    if(rc <= 0)
    {
      /* can't read any more data */
      ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
      if(rc < 0)
        ftp_send_response(session, 451, "Failed to read file\r\n");
      else {
        ftp_send_response(session, 226, "OK\r\n");
      }
      return LOOP_EXIT;
    }

    /* we read some data so reset the session buffer to send */
    session->bufferpos  = 0;
    session->buffersize = rc;
  }

  /* send any pending data */
  rc = send(session->data_fd, session->buffer + session->bufferpos,
            session->buffersize - session->bufferpos, 0);
  if(rc <= 0)
  {
    /* error sending data */
    if(rc < 0)
    {
      if(errno == EWOULDBLOCK)
        return LOOP_EXIT;
      //console_print(RED "send: %d %s\n" RESET, errno, strerror(errno));
    }
    /*else
      console_print(YELLOW "send: %d %s\n" RESET, ECONNRESET, strerror(ECONNRESET));*/

    ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
    ftp_send_response(session, 426, "Connection broken during transfer\r\n");
    return LOOP_EXIT;
  }

  /* we can try to send more data */
  session->bufferpos += rc;
  return LOOP_CONTINUE;
}

/*! send a file to the client
 *
 *  @param[in] session ftp session
 *
 *  @returns whether to call again
 */
static loop_status_t store_transfer(ftp_session_t *session) {
  ssize_t rc;

  if(session->bufferpos == session->buffersize)
  {
    /* we have written all the received data, so try to get some more */
    rc = recv(session->data_fd, session->buffer, sizeof(session->buffer), 0);
    if(rc <= 0)
    {
      /* can't read any more data */
      if(rc < 0)
      {
        if(errno == EWOULDBLOCK)
          return LOOP_EXIT;
        //console_print(RED "recv: %d %s\n" RESET, errno, strerror(errno));
      }

      ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);

      if(rc == 0)
        ftp_send_response(session, 226, "OK\r\n");
      else
        ftp_send_response(session, 426, "Connection broken during transfer\r\n");
      return LOOP_EXIT;
    }

    /* we received some data so reset the session buffer to write */
    session->bufferpos  = 0;
    session->buffersize = rc;
  }

  rc = ftp_session_write_file(session);
  if(rc <= 0)
  {
    /* error writing data */
    ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
    ftp_send_response(session, 451, "Failed to write file\r\n");
    return LOOP_EXIT;
  }

  /* we can try to receive more data */
  session->bufferpos += rc;
  return LOOP_CONTINUE;
}

/*! ftp_xfer_file mode */
typedef enum {
  XFER_FILE_RETR, /*!< Retrieve a file */
  XFER_FILE_STOR, /*!< Store a file */
  XFER_FILE_APPE, /*!< Append a file */
} xfer_file_mode_t;

/*! Transfer a file
 *
 *  @param[in] session ftp session
 *  @param[in] args    ftp arguments
 *  @param[in] mode    transfer mode
 *
 *  @returns failure
 */
static void ftp_xfer_file(ftp_session_t *session, const char *args, xfer_file_mode_t mode) {
  int rc;

  /* build the path of the file to transfer */
  if(build_path(session, session->cwd, args) != 0)
  {
    rc = errno;
    ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
    ftp_send_response(session, 553, "%s\r\n", strerror(rc));
    return;
  }

  /* open the file for retrieving or storing */
  if(mode == XFER_FILE_RETR)
    rc = ftp_session_open_file_read(session);
  else
    rc = ftp_session_open_file_write(session, mode == XFER_FILE_APPE);

  if(rc != 0)
  {
    /* error opening the file */
    ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
    ftp_send_response(session, 450, "failed to open file\r\n");
    return;
  }

  if(session->flags & (SESSION_PORT|SESSION_PASV))
  {
    ftp_session_set_state(session, DATA_CONNECT_STATE, CLOSE_DATA);

    if(session->flags & SESSION_PORT)
    {
      /* setup connection */
      rc = ftp_session_connect(session);
      if(rc != 0)
      {
        /* error connecting */
        ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        ftp_send_response(session, 425, "can't open data connection\r\n");
        return;
      }
    }

    /* set up the transfer */
    session->flags &= ~(SESSION_RECV|SESSION_SEND);
    if(mode == XFER_FILE_RETR)
    {
      session->flags   |= SESSION_SEND;
      session->transfer = retrieve_transfer;
    }
    else
    {
      session->flags   |= SESSION_RECV;
      session->transfer = store_transfer;
    }

    session->bufferpos  = 0;
    session->buffersize = 0;

    return;
  }

  ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
  ftp_send_response(session, 503, "Bad sequence of commands\r\n");
}

/*! Transfer a directory
 *
 *  @param[in] session    ftp session
 *  @param[in] args       ftp arguments
 *  @param[in] mode       transfer mode
 *  @param[in] workaround whether to workaround LIST -a
 */
static void
ftp_xfer_dir(ftp_session_t   *session,
             const char      *args,
             xfer_dir_mode_t mode,
             bool            workaround)
{
  ssize_t     rc;
  size_t      len;
  struct stat st;
  char        *buffer;

  /* set up the transfer */
  session->dir_mode = mode;
  session->flags &= ~SESSION_RECV;
  session->flags |= SESSION_SEND;

  session->transfer   = list_transfer;
  session->buffersize = 0;
  session->bufferpos  = 0;

  if(strlen(args) > 0)
  {
    /* an argument was provided */
    if(build_path(session, session->cwd, args) != 0)
    {
      /* error building path */
      ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
      ftp_send_response(session, 550, "%s\r\n", strerror(errno));
      return;
    }

    /* check if this is a directory */
    session->dp = opendir(session->buffer);
    if(session->dp == NULL)
    {
      /* not a directory; check if it is a file */
      rc = stat(session->buffer, &st);
      if(rc != 0)
      {
        /* error getting stat */
        rc = errno;

        /* work around broken clients that think LIST -a is valid */
        if(workaround && mode == XFER_DIR_LIST)
        {
          if(args[0] == '-' && (args[1] == 'a' || args[1] == 'l'))
          {
            if(args[2] == 0)
              buffer = strdup(args+2);
            else
              buffer = strdup(args+3);

            if(buffer != NULL)
            {
              ftp_xfer_dir(session, buffer, mode, false);
              free(buffer);
              return;
            }

            rc = ENOMEM;
          }
        }

        ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        ftp_send_response(session, 550, "%s\r\n", strerror(rc));
        return;
      }
      else if(mode == XFER_DIR_MLSD)
      {
        /* specified file instead of directory for MLSD */
        ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        ftp_send_response(session, 501, "%s\r\n", strerror(EINVAL));
        return;
      }
      else if(mode == XFER_DIR_NLST)
      {
        /* NLST uses full path name */
        len = session->buffersize;
        buffer = encode_path(session->buffer, &len, false);
      }
      else
      {
        /* everything else uses base name */
        const char *base = strrchr(session->buffer, '/') + 1;

        len = strlen(base);
        buffer = encode_path(base, &len, false);
      }

      if(buffer)
      {
        rc = ftp_session_fill_dirent(session, &st, buffer, len);
        free(buffer);
      }
      else
        rc = ENOMEM;

      if(rc != 0)
      {
        ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        ftp_send_response(session, 550, "%s\r\n", strerror(rc));
        return;
      }
    }
    else
    {
      /* it was a directory, so set it as the lwd */
      memcpy(session->lwd, session->buffer, session->buffersize);
      session->lwd[session->buffersize] = 0;
      session->buffersize = 0;

      if(session->dir_mode == XFER_DIR_MLSD
      && (session->mlst_flags & SESSION_MLST_TYPE))
      {
        /* send this directory as type=cdir */
        rc = ftp_session_fill_dirent_cdir(session, session->lwd);
        if(rc != 0)
        {
          ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
          ftp_send_response(session, 550, "%s\r\n", strerror(rc));
          return;
        }
      }
    }
  }
  else if(ftp_session_open_cwd(session) != 0)
  {
    /* no argument, but opening cwd failed */
    ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
    ftp_send_response(session, 550, "%s\r\n", strerror(errno));
    return;
  }
  else
  {
    /* set the cwd as the lwd */
    strcpy(session->lwd, session->cwd);
    session->buffersize = 0;

    if(session->dir_mode == XFER_DIR_MLSD
    && (session->mlst_flags & SESSION_MLST_TYPE))
    {
      /* send this directory as type=cdir */
      rc = ftp_session_fill_dirent_cdir(session, session->lwd);
      if(rc != 0)
      {
        ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        ftp_send_response(session, 550, "%s\r\n", strerror(rc));
        return;
      }
    }
  }

  if(mode == XFER_DIR_MLST || mode == XFER_DIR_STAT)
  {
    /* this is a little different; we have to send the data over the command socket */
    ftp_session_set_state(session, DATA_TRANSFER_STATE, CLOSE_PASV | CLOSE_DATA);
    session->data_fd = session->cmd_fd;
    session->flags  |= SESSION_SEND;
    ftp_send_response(session, -213, "Status\r\n");
    return;
  }
  else if(session->flags & (SESSION_PORT|SESSION_PASV))
  {
    ftp_session_set_state(session, DATA_CONNECT_STATE, CLOSE_DATA);

    if(session->flags & SESSION_PORT)
    {
      /* setup connection */
      rc = ftp_session_connect(session);
      if(rc != 0)
      {
        /* error connecting */
        ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
        ftp_send_response(session, 425, "can't open data connection\r\n");
      }
    }

    return;
  }

  /* we must have got LIST/MLSD/MLST/NLST without a preceding PORT or PASV */
  ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
  ftp_send_response(session, 503, "Bad sequence of commands\r\n");
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *                          F T P   C O M M A N D S                          *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*! @fn static void ABOR(ftp_session_t *session, const char *args)
 *
 *  @brief abort a transfer
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(ABOR)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  if(session->state == COMMAND_STATE)
  {
    ftp_send_response(session, 225, "No transfer to abort\r\n");
    return;
  }

  /* abort the transfer */
  ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);

  /* send response for this request */
  ftp_send_response(session, 225, "Aborted\r\n");

  /* send response for transfer */
  ftp_send_response(session, 425, "Transfer aborted\r\n");
}

/*! @fn static void ALLO(ftp_session_t *session, const char *args)
 *
 *  @brief allocate space
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(ALLO)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  ftp_send_response(session, 202, "superfluous command\r\n");
}

/*! @fn static void APPE(ftp_session_t *session, const char *args)
 *
 *  @brief append data to a file
 *
 *  @note requires a PASV or PORT connection
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(APPE)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* open the file in append mode */
  ftp_xfer_file(session, args, XFER_FILE_APPE);
}

/*! @fn static void CDUP(ftp_session_t *session, const char *args)
 *
 *  @brief CWD to parent directory
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(CDUP)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* change to parent directory */
  cd_up(session);

  ftp_send_response(session, 200, "OK\r\n");
}

/*! @fn static void CWD(ftp_session_t *session, const char *args)
 *
 *  @brief change working directory
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(CWD)
{
  struct stat st;
  int         rc;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* .. is equivalent to CDUP */
  if(strcmp(args, "..") == 0)
  {
    cd_up(session);
    ftp_send_response(session, 200, "OK\r\n");
    return;
  }

  /* build the new cwd path */
  if(build_path(session, session->cwd, args) != 0)
  {
    ftp_send_response(session, 553, "%s\r\n", strerror(errno));
    return;
  }

  /* get the path status */
  rc = stat(session->buffer, &st);
  if(rc != 0)
  {
    //console_print(RED "stat '%s': %d %s\n" RESET, session->buffer, errno, strerror(errno));
    ftp_send_response(session, 550, "unavailable\r\n");
    return;
  }

  /* make sure it is a directory */
  if(!S_ISDIR(st.st_mode))
  {
    ftp_send_response(session, 553, "not a directory\r\n");
    return;
  }

  /* copy the path into the cwd */
  strncpy(session->cwd, session->buffer, sizeof(session->cwd));
  session->cwd[sizeof(session->cwd)-1] = '\0';
  ftp_send_response(session, 200, "OK\r\n");
}

/*! @fn static void DELE(ftp_session_t *session, const char *args)
 *
 *  @brief delete a file
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(DELE)
{
  int rc;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* build the file path */
  if(build_path(session, session->cwd, args) != 0)
  {
    ftp_send_response(session, 553, "%s\r\n", strerror(errno));
    return;
  }

  /* try to unlink the path */
  rc = unlink(session->buffer);
  if(rc != 0)
  {
    /* error unlinking the file */
    //console_print(RED "unlink: %d %s\n" RESET, errno, strerror(errno));
    ftp_send_response(session, 550, "failed to delete file\r\n");
    return;
  }

  update_free_space();
  ftp_send_response(session, 250, "OK\r\n");
}

/*! @fn static void FEAT(ftp_session_t *session, const char *args)
 *
 *  @brief list server features
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(FEAT)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* list our features */
  ftp_send_response(session, -211, "\r\n"
    " MDTM\r\n"
    " MLST Type%s;Size%s;Modify%s;Perm%s;UNIX.mode%s;\r\n"
    " PASV\r\n"
    " SIZE\r\n"
    " TVFS\r\n"
    " UTF8\r\n"
    "\r\n"
    "211 End\r\n",
    session->mlst_flags & SESSION_MLST_TYPE      ? "*" : "",
    session->mlst_flags & SESSION_MLST_SIZE      ? "*" : "",
    session->mlst_flags & SESSION_MLST_MODIFY    ? "*" : "",
    session->mlst_flags & SESSION_MLST_PERM      ? "*" : "",
    session->mlst_flags & SESSION_MLST_UNIX_MODE ? "*" : "");
}

/*! @fn static void HELP(ftp_session_t *session, const char *args)
 *
 *  @brief print server help
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(HELP)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* list our accepted commands */
  ftp_send_response(session, -214,
      "The following commands are recognized\r\n"
      " ABOR ALLO APPE CDUP CWD DELE FEAT HELP LIST MDTM MKD MLSD MLST MODE\r\n"
      " NLST NOOP OPTS PASS PASV PORT PWD QUIT REST RETR RMD RNFR RNTO STAT\r\n"
      " STOR STOU STRU SYST TYPE USER XCUP XCWD XMKD XPWD XRMD\r\n"
      "214 End\r\n");
}

/*! @fn static void LIST(ftp_session_t *session, const char *args)
 *
 *  @brief retrieve a directory listing
 *
 *  @note Requires a PORT or PASV connection
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(LIST)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* open the path in LIST mode */
  ftp_xfer_dir(session, args, XFER_DIR_LIST, true);
}

/*! @fn static void MDTM(ftp_session_t *session, const char *args)
 *
 *  @brief get last modification time
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(MDTM)
{
  int         rc;
  struct stat st;
  time_t      t_mtime;
  struct tm   *tm;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* build the path */
  if(build_path(session, session->cwd, args) != 0)
  {
    ftp_send_response(session, 553, "%s\r\n", strerror(errno));
    return;
  }

  rc = stat(session->buffer, &st);
  if(rc != 0)
  {
    ftp_send_response(session, 550, "Error getting mtime\r\n");
    return;
  }
  t_mtime = st.st_mtime;

  tm = gmtime(&t_mtime);
  if(tm == NULL)
  {
    ftp_send_response(session, 550, "Error getting mtime\r\n");
    return;
  }

  session->buffersize = strftime(session->buffer, sizeof(session->buffer), "%Y%m%d%H%M%S", tm);
  if(session->buffersize == 0)
  {
    ftp_send_response(session, 550, "Error getting mtime\r\n");
    return;
  }

  session->buffer[session->buffersize] = 0;

  ftp_send_response(session, 213, "%s\r\n", session->buffer);
}
/*! @fn static void MKD(ftp_session_t *session, const char *args)
 *
 *  @brief create a directory
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(MKD)
{
  int rc;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* build the path */
  if(build_path(session, session->cwd, args) != 0)
  {
    ftp_send_response(session, 553, "%s\r\n", strerror(errno));
    return;
  }

  /* try to create the directory */
  rc = mkdir(session->buffer, 0755);
  if(rc != 0 && errno != EEXIST)
  {
    /* mkdir failure */
    //console_print(RED "mkdir: %d %s\n" RESET, errno, strerror(errno));
    ftp_send_response(session, 550, "failed to create directory\r\n");
    return;
  }

  update_free_space();
  ftp_send_response(session, 250, "OK\r\n");
}

/*! @fn static void MLSD(ftp_session_t *session, const char *args)
 *
 *  @brief set transfer mode
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(MLSD)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* open the path in MLSD mode */
  ftp_xfer_dir(session, args, XFER_DIR_MLSD, true);
}

/*! @fn static void MLST(ftp_session_t *session, const char *args)
 *
 *  @brief set transfer mode
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(MLST)
{
  struct stat st;
  int         rc;
  char        *path;
  size_t      len;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* build the path */
  if(build_path(session, session->cwd, args) != 0)
  {
    ftp_send_response(session, 501, "%s\r\n", strerror(errno));
    return;
  }

  /* stat path */
  rc = lstat(session->buffer, &st);
  if(rc != 0)
  {
    ftp_send_response(session, 550, "%s\r\n", strerror(errno));
    return;
  }

  /* encode \n in path */
  len = session->buffersize;
  path = encode_path(session->buffer, &len, true);
  if(!path)
  {
    ftp_send_response(session, 550, "%s\r\n", strerror(ENOMEM));
    return;
  }

  session->dir_mode = XFER_DIR_MLST;
  rc = ftp_session_fill_dirent(session, &st, path, len);
  free(path);
  if(rc != 0)
  {
    ftp_send_response(session, 550, "%s\r\n", strerror(errno));
    return;
  }

  path = malloc(session->buffersize + 1);
  if(!path)
  {
    ftp_send_response(session, 550, "%s\r\n", strerror(ENOMEM));
    return;
  }

  memcpy(path, session->buffer, session->buffersize);
  path[session->buffersize] = 0;
  ftp_send_response(session, -250, "Status\r\n%s250 End\r\n", path);
  free(path);
}

/*! @fn static void MODE(ftp_session_t *session, const char *args)
 *
 *  @brief set transfer mode
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(MODE)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* we only accept S (stream) mode */
  if(strcasecmp(args, "S") == 0)
  {
    ftp_send_response(session, 200, "OK\r\n");
    return;
  }

  ftp_send_response(session, 504, "unavailable\r\n");
}

/*! @fn static void NLST(ftp_session_t *session, const char *args)
 *
 *  @brief retrieve a name list
 *
 *  @note Requires a PASV or PORT connection
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(NLST)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* open the path in NLST mode */
  return ftp_xfer_dir(session, args, XFER_DIR_NLST, false);
}

/*! @fn static void NOOP(ftp_session_t *session, const char *args)
 *
 *  @brief no-op
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(NOOP)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* this is a no-op */
  ftp_send_response(session, 200, "OK\r\n");
}

/*! @fn static void OPTS(ftp_session_t *session, const char *args)
 *
 *  @brief set options
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(OPTS)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* we accept the following UTF8 options */
  if(strcasecmp(args, "UTF8") == 0
  || strcasecmp(args, "UTF8 ON") == 0
  || strcasecmp(args, "UTF8 NLST") == 0)
  {
    ftp_send_response(session, 200, "OK\r\n");
    return;
  }

  /* check MLST options */
  if(strncasecmp(args, "MLST ", 5) == 0)
  {
    static const struct
    {
      const char           *name;
      session_mlst_flags_t flag;
    } mlst_flags[] =
    {
      { "Type;",      SESSION_MLST_TYPE,      },
      { "Size;",      SESSION_MLST_SIZE,      },
      { "Modify;",    SESSION_MLST_MODIFY,    },
      { "Perm;",      SESSION_MLST_PERM,      },
      { "UNIX.mode;", SESSION_MLST_UNIX_MODE, },
    };
    static const size_t num_mlst_flags = sizeof(mlst_flags)/sizeof(mlst_flags[0]);

    session_mlst_flags_t flags = 0;
    args += 5;
    const char *p = args;
    while(*p)
    {
      for(size_t i = 0; i < num_mlst_flags; ++i)
      {
        if(strncasecmp(mlst_flags[i].name, p, strlen(mlst_flags[i].name)) == 0)
        {
          flags |= mlst_flags[i].flag;
          p += strlen(mlst_flags[i].name)-1;
          break;
        }
      }

      while(*p && *p != ';')
        ++p;

      if(*p == ';')
        ++p;
    }

    session->mlst_flags = flags;
    ftp_send_response(session, 200, "MLST OPTS%s%s%s%s%s%s\r\n",
                      flags ? " " : "",
                      flags & SESSION_MLST_TYPE      ? "Type;"      : "",
                      flags & SESSION_MLST_SIZE      ? "Size;"      : "",
                      flags & SESSION_MLST_MODIFY    ? "Modify;"    : "",
                      flags & SESSION_MLST_PERM      ? "Perm;"      : "",
                      flags & SESSION_MLST_UNIX_MODE ? "UNIX.mode;" : "");
    return;
  }

  ftp_send_response(session, 504, "invalid argument\r\n");
}

/*! @fn static void PASS(ftp_session_t *session, const char *args)
 *
 *  @brief provide password
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(PASS)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* we accept any password */
  ftp_session_set_state(session, COMMAND_STATE, 0);

  ftp_send_response(session, 230, "OK\r\n");
}

/*! @fn static void PASV(ftp_session_t *session, const char *args)
 *
 *  @brief request an address to connect to
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(PASV)
{
  int       rc;
  char      buffer[INET_ADDRSTRLEN + 10];
  char      *p;
  in_port_t port;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  memset(buffer, 0, sizeof(buffer));

  /* reset the state */
  ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
  session->flags &= ~(SESSION_PASV|SESSION_PORT);

  /* create a socket to listen on */
  session->pasv_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(session->pasv_fd < 0)
  {
    //console_print(RED "socket: %d %s\n" RESET, errno, strerror(errno));
    ftp_send_response(session, 451, "\r\n");
    return;
  }

  /* set the socket options */
  rc = ftp_set_socket_options(session->pasv_fd);
  if(rc != 0)
  {
    /* failed to set socket options */
    ftp_session_close_pasv(session);
    ftp_send_response(session, 451, "\r\n");
    return;
  }

  /* grab a new port */
  session->pasv_addr.sin_port = htons(next_data_port());

  /*console_print(YELLOW "binding to %s:%u\n" RESET,
                inet_ntoa(session->pasv_addr.sin_addr),
                ntohs(session->pasv_addr.sin_port));*/

  /* bind to the port */
  rc = bind(session->pasv_fd, (struct sockaddr*)&session->pasv_addr,
            sizeof(session->pasv_addr));
  if(rc != 0)
  {
    /* failed to bind */
    //console_print(RED "bind: %d %s\n" RESET, errno, strerror(errno));
    ftp_session_close_pasv(session);
    ftp_send_response(session, 451, "\r\n");
    return;
  }

  /* listen on the socket */
  rc = listen(session->pasv_fd, 1);
  if(rc != 0)
  {
    /* failed to listen */
    //console_print(RED "listen: %d %s\n" RESET, errno, strerror(errno));
    ftp_session_close_pasv(session);
    ftp_send_response(session, 451, "\r\n");
    return;
  }

  {
    /* get the socket address since we requested an ephemeral port */
    socklen_t addrlen = sizeof(session->pasv_addr);
    rc = getsockname(session->pasv_fd, (struct sockaddr*)&session->pasv_addr,
                     &addrlen);
    if(rc != 0)
    {
      /* failed to get socket address */
      //console_print(RED "getsockname: %d %s\n" RESET, errno, strerror(errno));
      ftp_session_close_pasv(session);
      ftp_send_response(session, 451, "\r\n");
      return;
    }
  }

  /* we are now listening on the socket */
  /*console_print(YELLOW "listening on %s:%u\n" RESET,
                inet_ntoa(session->pasv_addr.sin_addr),
                ntohs(session->pasv_addr.sin_port));*/
  session->flags |= SESSION_PASV;

  /* print the address in the ftp format */
  port = ntohs(session->pasv_addr.sin_port);
  strcpy(buffer, inet_ntoa(session->pasv_addr.sin_addr));
  sprintf(buffer+strlen(buffer), ",%u,%u",
          port >> 8, port & 0xFF);
  for(p = buffer; *p; ++p)
  {
    if(*p == '.')
      *p = ',';
  }

  ftp_send_response(session, 227, "%s\r\n", buffer);
}

/*! @fn static void PORT(ftp_session_t *session, const char *args)
 *
 *  @brief provide an address for the server to connect to
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(PORT)
{
  char               *addrstr, *p, *portstr;
  int                commas = 0, rc;
  short              port = 0;
  unsigned long      val;
  struct sockaddr_in addr;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* reset the state */
  ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
  session->flags &= ~(SESSION_PASV|SESSION_PORT);

  /* dup the args since they are const and we need to change it */
  addrstr = strdup(args);
  if(addrstr == NULL)
  {
    ftp_send_response(session, 425, "%s\r\n", strerror(ENOMEM));
    return;
  }

  /* replace a,b,c,d,e,f with a.b.c.d\0e.f */
  for(p = addrstr; *p; ++p)
  {
    if(*p == ',')
    {
      if(commas != 3)
        *p = '.';
      else
      {
        *p = 0;
        portstr = p+1;
      }
      ++commas;
    }
  }

  /* make sure we got the right number of values */
  if(commas != 5)
  {
    free(addrstr);
    ftp_send_response(session, 501, "%s\r\n", strerror(EINVAL));
    return;
  }

  /* parse the address */
  rc = inet_aton(addrstr, &addr.sin_addr);
  if(rc == 0)
  {
    free(addrstr);
    ftp_send_response(session, 501, "%s\r\n", strerror(EINVAL));
    return;
  }

  /* parse the port */
  val  = 0;
  port = 0;
  for(p = portstr; *p; ++p)
  {
    if(!isdigit((int)*p))
    {
      if(p == portstr || *p != '.' || val > 0xFF)
      {
        free(addrstr);
        ftp_send_response(session, 501, "%s\r\n", strerror(EINVAL));
        return;
      }
      port <<= 8;
      port  += val;
      val    = 0;
    }
    else
    {
      val *= 10;
      val += *p - '0';
    }
  }

  /* validate the port */
  if(val > 0xFF || port > 0xFF)
  {
    free(addrstr);
    ftp_send_response(session, 501, "%s\r\n", strerror(EINVAL));
    return;
  }
  port <<= 8;
  port  += val;

  /* fill in the address port and family */
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(port);

  free(addrstr);

  memcpy(&session->peer_addr, &addr, sizeof(addr));

  /* we are ready to connect to the client */
  session->flags |= SESSION_PORT;
  ftp_send_response(session, 200, "OK\r\n");
}

/*! @fn static void PWD(ftp_session_t *session, const char *args)
 *
 *  @brief print working directory
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(PWD)
{
  static char buffer[CMD_BUFFERSIZE];
  size_t      len = sizeof(buffer), i;
  char        *path;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* encode the cwd */
  len = strlen(session->cwd);
  path = encode_path(session->cwd, &len, true);
  if(path != NULL)
  {
    i = sprintf(buffer, "257 \"");
    if(i + len + 3 > sizeof(buffer))
    {
      /* buffer will overflow */
      free(path);
      ftp_session_set_state(session, COMMAND_STATE, CLOSE_PASV | CLOSE_DATA);
      ftp_send_response(session, 550, "unavailable\r\n");
      ftp_send_response(session, 425, "%s\r\n", strerror(EOVERFLOW));
      return;
    }
    memcpy(buffer+i, path, len);
    free(path);
    len += i;
    buffer[len++] = '"';
    buffer[len++] = '\r';
    buffer[len++] = '\n';

    ftp_send_response_buffer(session, buffer, len);
    return;
  }

  ftp_send_response(session, 425, "%s\r\n", strerror(ENOMEM));
}

/*! @fn static void QUIT(ftp_session_t *session, const char *args)
 *
 *  @brief terminate ftp session
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(QUIT)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* disconnect from the client */
  ftp_send_response(session, 221, "disconnecting\r\n");
  ftp_session_close_cmd(session);
}

/*! @fn static void REST(ftp_session_t *session, const char *args)
 *
 *  @brief restart a transfer
 *
 *  @note sets file position for a subsequent STOR operation
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(REST)
{
  const char *p;
  uint64_t   pos = 0;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* make sure an argument is provided */
  if(args == NULL)
  {
    ftp_send_response(session, 504, "invalid argument\r\n");
    return;
  }

  /* parse the offset */
  for(p = args; *p; ++p)
  {
    if(!isdigit((int)*p))
    {
      ftp_send_response(session, 504, "invalid argument\r\n");
      return;
    }

    if(UINT64_MAX / 10 < pos)
    {
      ftp_send_response(session, 504, "invalid argument\r\n");
      return;
    }

    pos *= 10;

    if(UINT64_MAX - (*p - '0') < pos)
    {
      ftp_send_response(session, 504, "invalid argument\r\n");
      return;
    }

    pos += (*p - '0');
  }

  /* set the restart offset */
  session->filepos = pos;
  ftp_send_response(session, 200, "OK\r\n");
}

/*! @fn static void RETR(ftp_session_t *session, const char *args)
 *
 *  @brief retrieve a file
 *
 *  @note Requires a PASV or PORT connection
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(RETR)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* open the file to retrieve */
  return ftp_xfer_file(session, args, XFER_FILE_RETR);
}

/*! @fn static void RMD(ftp_session_t *session, const char *args)
 *
 *  @brief remove a directory
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(RMD)
{
  int rc;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* build the path to remove */
  if(build_path(session, session->cwd, args) != 0)
  {
    ftp_send_response(session, 553, "%s\r\n", strerror(errno));
    return;
  }

  /* remove the directory */
  rc = rmdir(session->buffer);
  if(rc != 0)
  {
    /* rmdir error */
    //console_print(RED "rmdir: %d %s\n" RESET, errno, strerror(errno));
    ftp_send_response(session, 550, "failed to delete directory\r\n");
    return;
  }

  update_free_space();
  ftp_send_response(session, 250, "OK\r\n");
}

/*! @fn static void RNFR(ftp_session_t *session, const char *args)
 *
 *  @brief rename from
 *
 *  @note Must be followed by RNTO
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(RNFR)
{
  int         rc;
  struct stat st;
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* build the path to rename from */
  if(build_path(session, session->cwd, args) != 0)
  {
    ftp_send_response(session, 553, "%s\r\n", strerror(errno));
    return;
  }

  /* make sure the path exists */
  rc = lstat(session->buffer, &st);
  if(rc != 0)
  {
    /* error getting path status */
    //console_print(RED "lstat: %d %s\n" RESET, errno, strerror(errno));
    ftp_send_response(session, 450, "no such file or directory\r\n");
    return;
  }

  /* we are ready for RNTO */
  session->flags |= SESSION_RENAME;
  ftp_send_response(session, 350, "OK\r\n");
}

/*! @fn static void RNTO(ftp_session_t *session, const char *args)
 *
 *  @brief rename to
 *
 *  @note Must be preceded by RNFR
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(RNTO)
{
  static char rnfr[XFER_BUFFERSIZE]; // rename-from buffer
  int  rc;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* make sure the previous command was RNFR */
  if(!(session->flags & SESSION_RENAME))
  {
    ftp_send_response(session, 503, "Bad sequence of commands\r\n");
    return;
  }

  /* clear the rename state */
  session->flags &= ~SESSION_RENAME;

  /* copy the RNFR path */
  memcpy(rnfr, session->buffer, XFER_BUFFERSIZE);

  /* build the path to rename to */
  if(build_path(session, session->cwd, args) != 0)
  {
    ftp_send_response(session, 554, "%s\r\n", strerror(errno));
    return;
  }

  /* rename the file */
  rc = rename(rnfr, session->buffer);
  if(rc != 0)
  {
    /* rename failure */
    //console_print(RED "rename: %d %s\n" RESET, errno, strerror(errno));
    ftp_send_response(session, 550, "failed to rename file/directory\r\n");
    return;
  }

  update_free_space();
  ftp_send_response(session, 250, "OK\r\n");
}

/*! @fn static void SIZE(ftp_session_t *session, const char *args)
 *
 *  @brief get file size
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(SIZE)
{
  int         rc;
  struct stat st;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* build the path to stat */
  if(build_path(session, session->cwd, args) != 0)
  {
    ftp_send_response(session, 553, "%s\r\n", strerror(errno));
    return;
  }

  rc = stat(session->buffer, &st);
  if(rc != 0 || !S_ISREG(st.st_mode))
  {
    ftp_send_response(session, 550, "Could not get file size.\r\n");
    return;
  }

  ftp_send_response(session, 213, "%" PRIu64 "\r\n",
                    (uint64_t)st.st_size);
}

/*! @fn static void STAT(ftp_session_t *session, const char *args)
 *
 *  @brief get status
 *
 *  @note If no argument is supplied, and a transfer is occurring, get the
 *        current transfer status. If no argument is supplied, and no transfer
 *        is occurring, get the server status. If an argument is supplied, this
 *        is equivalent to LIST, except the data is sent over the command
 *        socket.
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(STAT)
{
  time_t uptime  = time(NULL) - start_time;
  int    hours   = uptime / 3600;
  int    minutes = (uptime / 60) % 60;
  int    seconds = uptime % 60;

  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  if(session->state == DATA_CONNECT_STATE)
  {
    /* we are waiting to connect to the client */
    ftp_send_response(session, -211, "FTP server status\r\n"
                                     " Waiting for data connection\r\n"
                                     "211 End\r\n");
    return;
  }
  else if(session->state == DATA_TRANSFER_STATE)
  {
    /* we are in the middle of a transfer */
    ftp_send_response(session, -211, "FTP server status\r\n"
                                     " Transferred %" PRIu64 " bytes\r\n"
                                     "211 End\r\n",
                                     session->filepos);
    return;
  }

  if(strlen(args) == 0)
  {
    /* no argument provided, send the server status */
    ftp_send_response(session, -211, "FTP server status\r\n"
                                     " Uptime: %02d:%02d:%02d\r\n"
                                     "211 End\r\n",
                                     hours, minutes, seconds);
    return;
  }

  /* argument provided, open the path in STAT mode */
  ftp_xfer_dir(session, args, XFER_DIR_STAT, false);
}

/*! @fn static void STOR(ftp_session_t *session, const char *args)
 *
 *  @brief store a file
 *
 *  @note Requires a PASV or PORT connection
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(STOR)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* open the file to store */
  return ftp_xfer_file(session, args, XFER_FILE_STOR);
}

/*! @fn static void STOU(ftp_session_t *session, const char *args)
 *
 *  @brief store a unique file
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(STOU)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  /* we do not support this yet */
  ftp_session_set_state(session, COMMAND_STATE, 0);

  ftp_send_response(session, 502, "unavailable\r\n");
}

/*! @fn static void STRU(ftp_session_t *session, const char *args)
 *
 *  @brief set file structure
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(STRU)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* we only support F (no structure) mode */
  if(strcasecmp(args, "F") == 0)
  {
    ftp_send_response(session, 200, "OK\r\n");
    return;
  }

  ftp_send_response(session, 504, "unavailable\r\n");
}

/*! @fn static void SYST(ftp_session_t *session, const char *args)
 *
 *  @brief identify system
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(SYST)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* we are UNIX compliant with 8-bit characters */
  ftp_send_response(session, 215, "UNIX Type: L8\r\n");
}

/*! @fn static void TYPE(ftp_session_t *session, const char *args)
 *
 *  @brief set transfer mode
 *
 *  @note transfer mode is always binary
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(TYPE)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* we always transfer in binary mode */
  ftp_send_response(session, 200, "OK\r\n");
}

/*! @fn static void USER(ftp_session_t *session, const char *args)
 *
 *  @brief provide user name
 *
 *  @param[in] session ftp session
 *  @param[in] args    arguments
 */
FTP_DECLARE(USER)
{
  //console_print(CYAN "%s %s\n" RESET, __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE, 0);

  /* we accept any user name */
  ftp_send_response(session, 230, "OK\r\n");
}
