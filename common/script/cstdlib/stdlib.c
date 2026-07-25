/*  */
#include <stdlib.h>

#include "interpreter.h"
#include "scriptheap_c.h"

static int Stdlib_ZeroValue = 0;

void StdlibAtof(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Double = atof(Param[0]->Val->Pointer);
}

void StdlibAtoi(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = atoi(Param[0]->Val->Pointer);
}

void StdlibAtol(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = atol(Param[0]->Val->Pointer);
}

void StdlibStrtod(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Double = strtod(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdlibStrtol(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer =
        strtol(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StdlibStrtoul(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer =
        strtoul(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void StdlibMalloc(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = ckpt_script_malloc(Param[0]->Val->Integer);
}

void StdlibCalloc(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = ckpt_script_calloc(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void StdlibRealloc(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = ckpt_script_realloc(Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void StdlibFree(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ckpt_script_free(Param[0]->Val->Pointer);
}

void StdlibRand(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = rand();
}

void StdlibSrand(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    srand(Param[0]->Val->Integer);
}

void StdlibAbort(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ProgramFail(Parser, "abort");
}

void StdlibExit(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    PlatformExit(Parser->pc, Param[0]->Val->Integer);
}

void StdlibGetenv(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = getenv(Param[0]->Val->Pointer);
}

void StdlibSystem(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = system(Param[0]->Val->Pointer);
}

#if 0
void StdlibBsearch(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = bsearch(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, Param[2]->Val->Integer, Param[3]->Val->Integer,
        (int (*)())Param[4]->Val->Pointer);
}
#endif

void StdlibAbs(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = abs(Param[0]->Val->Integer);
}

void StdlibLabs(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = labs(Param[0]->Val->Integer);
}

#if 0
void StdlibDiv(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = div(Param[0]->Val->Integer,
        Param[1]->Val->Integer);
}

void StdlibLdiv(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = ldiv(Param[0]->Val->Integer,
        Param[1]->Val->Integer);
}
#endif

#if 0
/* handy structure definitions */
const char StdlibDefs[] = "\
typedef struct { \
    int quot, rem; \
} div_t; \
\
typedef struct { \
    int quot, rem; \
} ldiv_t; \
";
#endif

/* all stdlib.h functions */
struct LibraryFunction StdlibFunctions[] = {
    {StdlibAtof,    "double atof(char *);"            },
    {StdlibStrtod,  "double strtod(char *,char **);"  },
    {StdlibAtoi,    "int atoi(char *);"               },
    {StdlibAtol,    "int atol(char *);"               },
    {StdlibStrtol,  "int strtol(char *,char **,int);" },
    {StdlibStrtoul, "int strtoul(char *,char **,int);"},
    {StdlibMalloc,  "void *malloc(int);"              },
    {StdlibCalloc,  "void *calloc(int,int);"          },
    {StdlibRealloc, "void *realloc(void *,int);"      },
    {StdlibFree,    "void free(void *);"              },
    {StdlibRand,    "int rand();"                     },
    {StdlibSrand,   "void srand(int);"                },
    {StdlibAbort,   "void abort();"                   },
    {StdlibExit,    "void exit(int);"                 },
    {StdlibGetenv,  "char *getenv(char *);"           },
    {StdlibSystem,  "int system(char *);"             },
    /*    {StdlibBsearch, "void *bsearch(void *,void *,int,int,int (*)());"}, */
    /*    {StdlibQsort, "void *qsort(void *,int,int,int (*)());"}, */
    {StdlibAbs,     "int abs(int);"                   },
    {StdlibLabs,    "int labs(int);"                  },
#if 0
    {StdlibDiv, "div_t div(int);"},
    {StdlibLdiv, "ldiv_t ldiv(int);"},
#endif
    {NULL,          NULL                              }
};

/* creates various system-dependent definitions */
void StdlibSetupFunc(Picoc* pc)
{
    /* define NULL, TRUE and FALSE */
    if (!VariableDefined(pc, TableStrRegister(pc, "NULL")))
    {
        VariableDefinePlatformVar(
            pc, NULL, "NULL", &pc->IntType, (union AnyValue*)&Stdlib_ZeroValue, false);
    }
}
