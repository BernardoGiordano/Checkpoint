/* all platform-specific includes and defines go in this file */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* host platform includes */
#ifdef UNIX_HOST
#include <stdint.h>
#include <unistd.h>
#elif defined(WIN32) /*(predefined on MSVC)*/
#else
#error ***** A platform must be explicitly defined! *****
#endif

/* configurable options */
/* select your host type (or do it in the Makefile):
 #define UNIX_HOST
 #define DEBUGGER
 #define USE_READLINE (defined by default for UNIX_HOST)
 */

#if defined(WIN32) /*(predefined on MSVC)*/
#undef USE_READLINE
#endif

#if defined(__hppa__) || defined(__sparc__)
/* the default data type to use for alignment */
#define ALIGN_TYPE double
#else
/* the default data type to use for alignment */
#define ALIGN_TYPE void*
#endif

#define GLOBAL_TABLE_SIZE (97)         /* global variable table */
#define STRING_TABLE_SIZE (97)         /* shared string table size */
#define STRING_LITERAL_TABLE_SIZE (97) /* string literal table size */
#define RESERVED_WORD_TABLE_SIZE (97)  /* reserved word table size */
#define PARAMETER_MAX (16)             /* maximum number of parameters to a function */
#define LINEBUFFER_MAX (256)           /* maximum number of characters on a line */
#define LOCAL_TABLE_SIZE (11)          /* size of local variable table (can expand) */
#define STRUCT_TABLE_SIZE (11)         /* size of struct/union member table (can expand) */
#define GOTO_LABELS_TABLE_SIZE (5)     /* size of goto labels table */

#define INTERACTIVE_PROMPT_START "starting picoc " PICOC_VERSION " (Ctrl+D to exit)\n"
#define INTERACTIVE_PROMPT_STATEMENT "picoc> "
#define INTERACTIVE_PROMPT_LINE "     > "

#endif /* PLATFORM_H */
