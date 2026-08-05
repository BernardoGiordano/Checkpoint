/*  */
#include <string.h>

#include "interpreter.h"
#include "scriptheap_c.h"

static int String_ZeroValue = 0;

void StringStrcpy(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strcpy(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrncpy(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer =
        strncpy(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringStrcmp(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = strcmp(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrcasecmp(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = strcasecmp(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrncmp(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer =
        strncmp(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringStrncasecmp(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer =
        strncasecmp(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringStrcat(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strcat(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrncat(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer =
        strncat(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

#ifndef WIN32
void StringIndex(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = index(Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void StringRindex(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = rindex(Param[0]->Val->Pointer, Param[1]->Val->Integer);
}
#endif

void StringStrlen(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = strlen(Param[0]->Val->Pointer);
}

void StringMemset(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer =
        memset(Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void StringMemcpy(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer =
        memcpy(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringMemcmp(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer =
        memcmp(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringMemmove(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer =
        memmove(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StringMemchr(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer =
        memchr(Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void StringStrchr(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strchr(Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void StringStrrchr(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strrchr(Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void StringStrcoll(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = strcoll(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrerror(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strerror(Param[0]->Val->Integer);
}

void StringStrspn(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = strspn(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrcspn(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = strcspn(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrpbrk(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strpbrk(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrstr(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strstr(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrtok(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strtok(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StringStrxfrm(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer =
        strxfrm(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

#ifndef WIN32
void StringStrdup(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    /* Through the script heap, not the C library's strdup: a script's free()
       is ScriptHeap::release, which ignores a pointer the heap does not own, so
       a libc strdup would leak for the whole run and log a warning per free.
       Anything a script can free has to be allocated the way its malloc is. */
    const char* src = Param[0]->Val->Pointer;
    if (src == NULL)
    {
        ReturnValue->Val->Pointer = NULL;
        return;
    }

    size_t size = strlen(src) + 1;
    void* copy  = ckpt_script_malloc(size);
    if (copy != NULL)
    {
        memcpy(copy, src, size);
    }
    ReturnValue->Val->Pointer = copy;
}

void StringStrtok_r(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer =
        (void*)strtok_r(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Pointer);
}
#endif

/* all string.h functions */
struct LibraryFunction StringFunctions[] = {
#ifndef WIN32
    {StringIndex,       "char *index(char *,int);"              },
     {StringRindex,      "char *rindex(char *,int);"             },
#endif
    {StringMemcpy,      "void *memcpy(void *,void *,int);"      },
    {StringMemmove,     "void *memmove(void *,void *,int);"     },
    {StringMemchr,      "void *memchr(char *,int,int);"         },
    {StringMemcmp,      "int memcmp(void *,void *,int);"        },
    {StringMemset,      "void *memset(void *,int,int);"         },
     {StringStrcat,      "char *strcat(char *,char *);"          },
    {StringStrncat,     "char *strncat(char *,char *,int);"     },
    {StringStrchr,      "char *strchr(char *,int);"             },
     {StringStrrchr,     "char *strrchr(char *,int);"            },
    {StringStrcmp,      "int strcmp(char *,char *);"            },
    {StringStrncmp,     "int strncmp(char *,char *,int);"       },
    {StringStrcasecmp,  "int strcasecmp(char *,char *);"        },
    {StringStrncasecmp, "int strncasecmp(char *,char *,int);"   },
    {StringStrcoll,     "int strcoll(char *,char *);"           },
     {StringStrcpy,      "char *strcpy(char *,char *);"          },
    {StringStrncpy,     "char *strncpy(char *,char *,int);"     },
     {StringStrerror,    "char *strerror(int);"                  },
    {StringStrlen,      "int strlen(char *);"                   },
     {StringStrspn,      "int strspn(char *,char *);"            },
    {StringStrcspn,     "int strcspn(char *,char *);"           },
    {StringStrpbrk,     "char *strpbrk(char *,char *);"         },
    {StringStrstr,      "char *strstr(char *,char *);"          },
     {StringStrtok,      "char *strtok(char *,char *);"          },
    {StringStrxfrm,     "int strxfrm(char *,char *,int);"       },
#ifndef WIN32
    {StringStrdup,      "char *strdup(char *);"                 },
    {StringStrtok_r,    "char *strtok_r(char *,char *,char **);"},
#endif
    {NULL,              NULL                                    }
};

/* creates various system-dependent definitions */
void StringSetupFunc(Picoc* pc)
{
    /* define NULL */
    if (!VariableDefined(pc, TableStrRegister(pc, "NULL")))
    {
        VariableDefinePlatformVar(
            pc, NULL, "NULL", &pc->IntType, (union AnyValue*)&String_ZeroValue, false);
    }
}
