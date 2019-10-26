#ifndef FTP_H
#define FTP_H

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
