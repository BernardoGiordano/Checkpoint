#ifndef FTP_H
#define FTP_H

#if defined(_3DS)
#include <3ds.h>
#elif defined(__SWITCH__)
#include <switch.h>
#endif

/*! Loop status */
typedef enum {
  LOOP_CONTINUE, /*!< Continue looping */
  LOOP_RESTART,  /*!< Reinitialize */
  LOOP_EXIT,     /*!< Terminate looping */
} loop_status_t;

int           ftp_init(void);
loop_status_t ftp_loop(void);
void          ftp_exit(void);

#endif
