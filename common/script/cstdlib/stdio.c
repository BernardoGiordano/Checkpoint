/*  */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

#include "interpreter.h"
#include "scriptconsole_c.h"

/* Checkpoint: neither console has a real stdout, so everything a script prints
 * goes to the live log pane instead. CKPT_STDOUT stands in for `stdout` at
 * every point where picoc's stdlib means "the script's console". */
#define CKPT_STDOUT (ckpt_console_stdout())

#define MAX_FORMAT (80)
#define MAX_SCANF_ARGS (10)

static int Stdio_ZeroValue   = 0;
static int EOFValue          = EOF;
static int SEEK_SETValue     = SEEK_SET;
static int SEEK_CURValue     = SEEK_CUR;
static int SEEK_ENDValue     = SEEK_END;
static int BUFSIZValue       = BUFSIZ;
static int FILENAME_MAXValue = FILENAME_MAX;
static int _IOFBFValue       = _IOFBF;
static int _IOLBFValue       = _IOLBF;
static int _IONBFValue       = _IONBF;
static int L_tmpnamValue     = L_tmpnam;
static int GETS_MAXValue     = 255; /* arbitrary maximum size of a gets() file */

static FILE* stdinValue;
static FILE* stdoutValue;
static FILE* stderrValue;

/* our own internal output stream which can output to FILE * or strings */
typedef struct StdOutStreamStruct
{
    FILE* FilePtr;
    char* StrOutPtr;
    int StrOutLen;
    int CharCount;

} StdOutStream;

/* our representation of varargs within picoc */
struct StdVararg
{
    struct Value** Param;
    int NumArgs;
};

/* initializes the I/O system so error reporting works */
void BasicIOInit(Picoc* pc)
{
    pc->CStdOut = CKPT_STDOUT;
    stdinValue  = stdin;
    stdoutValue = CKPT_STDOUT;
    stderrValue = stderr;
}

/* output a single character to either a FILE * or a string */
void StdioOutPutc(int OutCh, StdOutStream* Stream)
{
    if (Stream->FilePtr != NULL)
    {
        /* output to stdio stream */
        putc(OutCh, Stream->FilePtr);
        Stream->CharCount++;
    }
    else if (Stream->StrOutLen < 0 || Stream->StrOutLen > 1)
    {
        /* output to a string */
        *Stream->StrOutPtr = OutCh;
        Stream->StrOutPtr++;

        if (Stream->StrOutLen > 1)
        {
            Stream->StrOutLen--;
        }

        Stream->CharCount++;
    }
}

/* output a string to either a FILE * or a string */
void StdioOutPuts(const char* Str, StdOutStream* Stream)
{
    if (Stream->FilePtr != NULL)
    {
        /* output to stdio stream */
        fputs(Str, Stream->FilePtr);
    }
    else
    {
        /* output to a string */
        while (*Str != '\0')
        {
            if (Stream->StrOutLen < 0 || Stream->StrOutLen > 1)
            {
                /* output to a string */
                *Stream->StrOutPtr = *Str;
                Str++;
                Stream->StrOutPtr++;

                if (Stream->StrOutLen > 1)
                {
                    Stream->StrOutLen--;
                }

                Stream->CharCount++;
            }
        }
    }
}

/* printf-style format of an int or other word-sized object */
void StdioFprintfWord(StdOutStream* Stream, const char* Format, unsigned int Value)
{
    if (Stream->FilePtr != NULL)
    {
        Stream->CharCount += fprintf(Stream->FilePtr, Format, Value);
    }
    else if (Stream->StrOutLen >= 0)
    {
#ifndef WIN32
        int CCount = snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#else
        int CCount = _snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#endif
        Stream->StrOutPtr += CCount;
        Stream->StrOutLen -= CCount;
        Stream->CharCount += CCount;
    }
    else
    {
        int CCount         = sprintf(Stream->StrOutPtr, Format, Value);
        Stream->CharCount += CCount;
        Stream->StrOutPtr += CCount;
    }
}

/* printf-style format of a long */
void StdioFprintfLong(StdOutStream* Stream, const char* Format, uint64_t Value)
{
    char PlatformFormat[MAX_FORMAT + 1], *FPos = PlatformFormat;

    while (*Format)
    {
        char* UseFormat = NULL;

        switch (*Format)
        {
            case 'd':
                UseFormat = PRId64;
                break;
            case 'i':
                UseFormat = PRIi64;
                break;
            case 'o':
                UseFormat = PRIo64;
                break;
            case 'u':
                UseFormat = PRIu64;
                break;
            case 'x':
                UseFormat = PRIx64;
                break;
            case 'X':
                UseFormat = PRIX64;
                break;
            /* Ignore the %l (long) specifier, because of course we're doing longs in this function
             */
            case 'l':
                break;
            default:
                *FPos++ = *Format;
                break;
        }
        ++Format;
        if (UseFormat)
        {
            strcpy(FPos, UseFormat);
            FPos += strlen(UseFormat);
        }
    }

    if (Stream->FilePtr != NULL)
    {
        Stream->CharCount += fprintf(Stream->FilePtr, PlatformFormat, Value);
    }
    else if (Stream->StrOutLen >= 0)
    {
#ifndef WIN32
        int CCount = snprintf(Stream->StrOutPtr, Stream->StrOutLen, PlatformFormat, Value);
#else
        int CCount = _snprintf(Stream->StrOutPtr, Stream->StrOutLen, PlatformFormat, Value);
#endif
        Stream->StrOutPtr += CCount;
        Stream->StrOutLen -= CCount;
        Stream->CharCount += CCount;
    }
    else
    {
        int CCount         = sprintf(Stream->StrOutPtr, PlatformFormat, Value);
        Stream->CharCount += CCount;
        Stream->StrOutPtr += CCount;
    }
}

/* printf-style format of a floating point number */
void StdioFprintfFP(StdOutStream* Stream, const char* Format, double Value)
{
    if (Stream->FilePtr != NULL)
    {
        Stream->CharCount += fprintf(Stream->FilePtr, Format, Value);
    }
    else if (Stream->StrOutLen >= 0)
    {
#ifndef WIN32
        int CCount = snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#else
        int CCount = _snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#endif
        Stream->StrOutPtr += CCount;
        Stream->StrOutLen -= CCount;
        Stream->CharCount += CCount;
    }
    else
    {
        int CCount         = sprintf(Stream->StrOutPtr, Format, Value);
        Stream->CharCount += CCount;
        Stream->StrOutPtr += CCount;
    }
}

/* printf-style format of a pointer */
void StdioFprintfPointer(StdOutStream* Stream, const char* Format, void* Value)
{
    if (Stream->FilePtr != NULL)
    {
        Stream->CharCount += fprintf(Stream->FilePtr, Format, Value);
    }
    else if (Stream->StrOutLen >= 0)
    {
#ifndef WIN32
        int CCount = snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#else
        int CCount = _snprintf(Stream->StrOutPtr, Stream->StrOutLen, Format, Value);
#endif
        Stream->StrOutPtr += CCount;
        Stream->StrOutLen -= CCount;
        Stream->CharCount += CCount;
    }
    else
    {
        int CCount         = sprintf(Stream->StrOutPtr, Format, Value);
        Stream->CharCount += CCount;
        Stream->StrOutPtr += CCount;
    }
}

/* internal do-anything v[s][n]printf() formatting system with output
    to strings or FILE * */
int StdioBasePrintf(struct ParseState* Parser, FILE* Stream, char* StrOut, int StrOutLen,
    char* Format, struct StdVararg* Args)
{
    struct Value* ThisArg = Args->Param[0];
    int ArgCount          = 0;
    char* FPos;
    char OneFormatBuf[MAX_FORMAT + 1];
    int OneFormatCount;
    int ShowLong = 0;
    struct ValueType* ShowType;
    StdOutStream SOStream;
    Picoc* pc = Parser->pc;

    if (Format == NULL)
    {
        Format = "[null format]\n";
    }

    FPos               = Format;
    SOStream.FilePtr   = Stream;
    SOStream.StrOutPtr = StrOut;
    SOStream.StrOutLen = StrOutLen;
    SOStream.CharCount = 0;

    while (*FPos != '\0')
    {
        if (*FPos == '%')
        {
            /* work out what type we're printing */
            FPos++;
            ShowType        = NULL;
            OneFormatBuf[0] = '%';
            OneFormatCount  = 1;

            do
            {
                switch (*FPos)
                {
                    case 'd':
                    case 'i':
                        if (ShowLong)
                        {
                            ShowLong = 0;
                            ShowType = &pc->LongType;
                        }
                        else
                        {
                            ShowType = &pc->IntType;
                        }
                        break;
                    case 'u':
                        if (ShowLong)
                        {
                            ShowLong = 0;
                            ShowType = &pc->UnsignedLongType;
                            break;
                        }
                        // falls through
                    case 'o':
                    case 'x':
                    case 'X':
                        ShowType = &pc->IntType;
                        break; /* integer base conversions */
                    case 'l':
                        ShowLong = 1;
                        break; /* long integer */
                    case 'e':
                    case 'E':
                        ShowType = &pc->DoubleType;
                        break; /* double, exponent form */
                    case 'f':
                    case 'F':
                        ShowType = &pc->DoubleType;
                        break; /* double, fixed-point */
                    case 'g':
                    case 'G':
                        ShowType = &pc->DoubleType;
                        break; /* double, flexible format */
                    case 'a':
                    case 'A':
                        ShowType = &pc->IntType;
                        break; /* hexadecimal, 0x- format */
                    case 'c':
                        ShowType = &pc->IntType;
                        break; /* character */
                    case 's':
                        ShowType = pc->CharPtrType;
                        break; /* string */
                    case 'p':
                        ShowType = pc->VoidPtrType;
                        break; /* pointer */
                    case 'n':
                        ShowType = &pc->VoidType;
                        break; /* number of characters written */
                    case 'm':
                        ShowType = &pc->VoidType;
                        break; /* strerror(errno) */
                    case '%':
                        ShowType = &pc->VoidType;
                        break; /* just a '%' character */
                    case '\0':
                        ShowType = &pc->VoidType;
                        break; /* end of format string */
                }

                /* copy one character of format across to the OneFormatBuf */
                if (*FPos != 'l')
                {
                    OneFormatBuf[OneFormatCount] = *FPos;
                    OneFormatCount++;
                }
                /* do special actions depending on the conversion type */
                if (ShowType == &pc->VoidType)
                {
                    switch (*FPos)
                    {
                        case 'm':
                            StdioOutPuts(strerror(errno), &SOStream);
                            break;
                        case '%':
                            StdioOutPutc(*FPos, &SOStream);
                            break;
                        case '\0':
                            OneFormatBuf[OneFormatCount] = '\0';
                            StdioOutPutc(*FPos, &SOStream);
                            break;
                        case 'n':
                            ThisArg = (struct Value*)((char*)ThisArg +
                                                      MEM_ALIGN(sizeof(struct Value) +
                                                                TypeStackSizeValue(ThisArg)));
                            if (ThisArg->Typ->Base == TypeArray &&
                                ThisArg->Typ->FromType->Base == TypeInt)
                            {
                                *(int*)ThisArg->Val->Pointer = SOStream.CharCount;
                            }
                            break;
                    }
                }

                FPos++;
            }
            while (ShowType == NULL && OneFormatCount < MAX_FORMAT);

            if (ShowType != &pc->VoidType)
            {
                if (ArgCount >= Args->NumArgs)
                {
                    StdioOutPuts("XXX", &SOStream);
                }
                else
                {
                    /* null-terminate the buffer */
                    OneFormatBuf[OneFormatCount] = '\0';

                    /* print this argument */
                    ThisArg =
                        (struct Value*)((char*)ThisArg + MEM_ALIGN(sizeof(struct Value) +
                                                                   TypeStackSizeValue(ThisArg)));

                    if (ShowType == &pc->LongType)
                    {
                        /* show a signed long */
                        if (IS_NUMERIC_COERCIBLE(ThisArg))
                        {
                            StdioFprintfLong(&SOStream, OneFormatBuf, ThisArg->Val->LongInteger);
                        }
                        else
                        {
                            StdioOutPuts("XXX", &SOStream);
                        }
                    }
                    else if (ShowType == &pc->UnsignedLongType)
                    {
                        /* show a unsigned long */
                        if (IS_NUMERIC_COERCIBLE(ThisArg))
                        {
                            StdioFprintfLong(
                                &SOStream, OneFormatBuf, ThisArg->Val->UnsignedLongInteger);
                        }
                        else
                        {
                            StdioOutPuts("XXX", &SOStream);
                        }
                    }
                    else if (ShowType == &pc->IntType)
                    {
                        /* show a signed integer */
                        if (IS_NUMERIC_COERCIBLE(ThisArg))
                        {
                            StdioFprintfWord(&SOStream, OneFormatBuf,
                                (unsigned int)ExpressionCoerceInteger(ThisArg));
                        }
                        else
                        {
                            StdioOutPuts("XXX", &SOStream);
                        }
                    }
                    else if (ShowType == &pc->UnsignedIntType)
                    {
                        /* show a signed integer */
                        if (IS_NUMERIC_COERCIBLE(ThisArg))
                        {
                            StdioFprintfWord(
                                &SOStream, OneFormatBuf, (int)ExpressionCoerceInteger(ThisArg));
                        }
                        else
                        {
                            StdioOutPuts("XXX", &SOStream);
                        }
                    }
                    else if (ShowType == &pc->FloatType)
                    {
                        /* show a floating point number */
                        if (IS_NUMERIC_COERCIBLE(ThisArg))
                        {
                            StdioFprintfFP(&SOStream, OneFormatBuf, ExpressionCoerceFloat(ThisArg));
                        }
                        else
                        {
                            StdioOutPuts("XXX", &SOStream);
                        }
                    }
                    else if (ShowType == &pc->DoubleType)
                    {
                        /* show a floating point number */
                        if (IS_NUMERIC_COERCIBLE(ThisArg))
                        {
                            StdioFprintfFP(
                                &SOStream, OneFormatBuf, ExpressionCoerceDouble(ThisArg));
                        }
                        else
                        {
                            StdioOutPuts("XXX", &SOStream);
                        }
                    }
                    else if (ShowType == pc->CharPtrType)
                    {
                        if (ThisArg->Typ->Base == TypePointer)
                        {
                            StdioFprintfPointer(&SOStream, OneFormatBuf, ThisArg->Val->Pointer);
                        }
                        else if (ThisArg->Typ->Base == TypeArray &&
                                 ThisArg->Typ->FromType->Base == TypeChar)
                        {
                            StdioFprintfPointer(
                                &SOStream, OneFormatBuf, &ThisArg->Val->ArrayMem[0]);
                        }
                        else
                        {
                            StdioOutPuts("XXX", &SOStream);
                        }
                    }
                    else if (ShowType == pc->VoidPtrType)
                    {
                        if (ThisArg->Typ->Base == TypePointer)
                        {
                            StdioFprintfPointer(&SOStream, OneFormatBuf, ThisArg->Val->Pointer);
                        }
                        else if (ThisArg->Typ->Base == TypeArray)
                        {
                            StdioFprintfPointer(
                                &SOStream, OneFormatBuf, &ThisArg->Val->ArrayMem[0]);
                        }
                        else
                        {
                            StdioOutPuts("XXX", &SOStream);
                        }
                    }

                    ArgCount++;
                }
            }
        }
        else
        {
            /* just output a normal character */
            StdioOutPutc(*FPos, &SOStream);
            FPos++;
        }
    }

    /* null-terminate */
    if (SOStream.StrOutPtr != NULL && SOStream.StrOutLen > 0)
    {
        *SOStream.StrOutPtr = '\0';
    }

    return SOStream.CharCount;
}

/* internal do-anything v[s][n]scanf() formatting system with input
    from strings or FILE * */
int StdioBaseScanf(
    struct ParseState* Parser, FILE* Stream, char* StrIn, char* Format, struct StdVararg* Args)
{
    struct Value* ThisArg = Args->Param[0];
    int ArgCount          = 0;
    void* ScanfArg[MAX_SCANF_ARGS];

    if (Args->NumArgs > MAX_SCANF_ARGS)
    {
        ProgramFail(Parser, "too many arguments to scanf() - %d max", MAX_SCANF_ARGS);
    }

    for (ArgCount = 0; ArgCount < Args->NumArgs; ArgCount++)
    {
        ThisArg = (struct Value*)((char*)ThisArg +
                                  MEM_ALIGN(sizeof(struct Value) + TypeStackSizeValue(ThisArg)));

        if (ThisArg->Typ->Base == TypePointer)
        {
            ScanfArg[ArgCount] = ThisArg->Val->Pointer;
        }
        else if (ThisArg->Typ->Base == TypeArray)
        {
            ScanfArg[ArgCount] = &ThisArg->Val->ArrayMem[0];
        }
        else
        {
            ProgramFail(
                Parser, "non-pointer argument to scanf() - argument %d after format", ArgCount + 1);
        }
    }

    if (Stream != NULL)
    {
        return fscanf(Stream, Format, ScanfArg[0], ScanfArg[1], ScanfArg[2], ScanfArg[3],
            ScanfArg[4], ScanfArg[5], ScanfArg[6], ScanfArg[7], ScanfArg[8], ScanfArg[9]);
    }
    else
    {
        return sscanf(StrIn, Format, ScanfArg[0], ScanfArg[1], ScanfArg[2], ScanfArg[3],
            ScanfArg[4], ScanfArg[5], ScanfArg[6], ScanfArg[7], ScanfArg[8], ScanfArg[9]);
    }
}

/* stdio calls */
void StdioFopen(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = fopen(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdioFreopen(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer =
        freopen(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Pointer);
}

void StdioFclose(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fclose(Param[0]->Val->Pointer);
}

void StdioFread(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fread(Param[0]->Val->Pointer, Param[1]->Val->Integer,
        Param[2]->Val->Integer, Param[3]->Val->Pointer);
}

void StdioFwrite(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fwrite(Param[0]->Val->Pointer, Param[1]->Val->Integer,
        Param[2]->Val->Integer, Param[3]->Val->Pointer);
}

void StdioFgetc(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fgetc(Param[0]->Val->Pointer);
}

void StdioFgets(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer =
        fgets(Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Pointer);
}

void StdioRemove(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = remove(Param[0]->Val->Pointer);
}

void StdioRename(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = rename(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdioRewind(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    rewind(Param[0]->Val->Pointer);
}

void StdioTmpfile(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = tmpfile();
}

void StdioClearerr(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    clearerr((FILE*)Param[0]->Val->Pointer);
}

void StdioFeof(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = feof((FILE*)Param[0]->Val->Pointer);
}

void StdioFerror(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = ferror((FILE*)Param[0]->Val->Pointer);
}

void StdioFileno(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
#ifndef WIN32
    ReturnValue->Val->Integer = fileno(Param[0]->Val->Pointer);
#else
    ReturnValue->Val->Integer = _fileno(Param[0]->Val->Pointer);
#endif
}

void StdioFflush(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fflush(Param[0]->Val->Pointer);
}

void StdioFgetpos(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fgetpos(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdioFsetpos(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fsetpos(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdioFputc(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fputc(Param[0]->Val->Integer, Param[1]->Val->Pointer);
}

void StdioFputs(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fputs(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdioFtell(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = ftell(Param[0]->Val->Pointer);
}

void StdioFseek(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer =
        fseek(Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void StdioPerror(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    perror(Param[0]->Val->Pointer);
}

void StdioPutc(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = putc(Param[0]->Val->Integer, Param[1]->Val->Pointer);
}

void StdioPutchar(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = putc(Param[0]->Val->Integer, CKPT_STDOUT);
}

void StdioSetbuf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    setbuf(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdioSetvbuf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    setvbuf(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer,
        Param[3]->Val->Integer);
}

void StdioUngetc(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = ungetc(Param[0]->Val->Integer, Param[1]->Val->Pointer);
}

void StdioPuts(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = fputs(Param[0]->Val->Pointer, CKPT_STDOUT);
    fputc('\n', CKPT_STDOUT);
}

void StdioGets(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = fgets(Param[0]->Val->Pointer, GETS_MAXValue, stdin);
    if (ReturnValue->Val->Pointer != NULL)
    {
        char* EOLPos = strchr(Param[0]->Val->Pointer, '\n');
        if (EOLPos != NULL)
        {
            *EOLPos = '\0';
        }
    }
}

void StdioGetchar(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = getchar();
}

void StdioPrintf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    struct StdVararg PrintfArgs;

    PrintfArgs.Param   = Param;
    PrintfArgs.NumArgs = NumArgs - 1;
    ReturnValue->Val->Integer =
        StdioBasePrintf(Parser, CKPT_STDOUT, NULL, 0, Param[0]->Val->Pointer, &PrintfArgs);
}

void StdioVprintf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer =
        StdioBasePrintf(Parser, CKPT_STDOUT, NULL, 0, Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdioFprintf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    struct StdVararg PrintfArgs;

    PrintfArgs.Param          = Param + 1;
    PrintfArgs.NumArgs        = NumArgs - 2;
    ReturnValue->Val->Integer = StdioBasePrintf(
        Parser, Param[0]->Val->Pointer, NULL, 0, Param[1]->Val->Pointer, &PrintfArgs);
}

void StdioVfprintf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBasePrintf(
        Parser, Param[0]->Val->Pointer, NULL, 0, Param[1]->Val->Pointer, Param[2]->Val->Pointer);
}

void StdioSprintf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    struct StdVararg PrintfArgs;

    PrintfArgs.Param          = Param + 1;
    PrintfArgs.NumArgs        = NumArgs - 2;
    ReturnValue->Val->Integer = StdioBasePrintf(
        Parser, NULL, Param[0]->Val->Pointer, -1, Param[1]->Val->Pointer, &PrintfArgs);
}

void StdioSnprintf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    struct StdVararg PrintfArgs;

    PrintfArgs.Param          = Param + 2;
    PrintfArgs.NumArgs        = NumArgs - 3;
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, NULL, Param[0]->Val->Pointer,
        Param[1]->Val->Integer, Param[2]->Val->Pointer, &PrintfArgs);
}

void StdioScanf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    struct StdVararg ScanfArgs;

    ScanfArgs.Param   = Param;
    ScanfArgs.NumArgs = NumArgs - 1;
    ReturnValue->Val->Integer =
        StdioBaseScanf(Parser, stdin, NULL, Param[0]->Val->Pointer, &ScanfArgs);
}

void StdioFscanf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    struct StdVararg ScanfArgs;

    ScanfArgs.Param   = Param + 1;
    ScanfArgs.NumArgs = NumArgs - 2;
    ReturnValue->Val->Integer =
        StdioBaseScanf(Parser, Param[0]->Val->Pointer, NULL, Param[1]->Val->Pointer, &ScanfArgs);
}

void StdioSscanf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    struct StdVararg ScanfArgs;

    ScanfArgs.Param   = Param + 1;
    ScanfArgs.NumArgs = NumArgs - 2;
    ReturnValue->Val->Integer =
        StdioBaseScanf(Parser, NULL, Param[0]->Val->Pointer, Param[1]->Val->Pointer, &ScanfArgs);
}

void StdioVsprintf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBasePrintf(
        Parser, NULL, Param[0]->Val->Pointer, -1, Param[1]->Val->Pointer, Param[2]->Val->Pointer);
}

void StdioVsnprintf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBasePrintf(Parser, NULL, Param[0]->Val->Pointer,
        Param[1]->Val->Integer, Param[2]->Val->Pointer, Param[3]->Val->Pointer);
}

void StdioVscanf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer =
        StdioBaseScanf(Parser, stdin, NULL, Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdioVfscanf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBaseScanf(
        Parser, Param[0]->Val->Pointer, NULL, Param[1]->Val->Pointer, Param[2]->Val->Pointer);
}

void StdioVsscanf(
    struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = StdioBaseScanf(
        Parser, NULL, Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Pointer);
}

/* handy structure definitions */
const char StdioDefs[] = "\
typedef struct __va_listStruct va_list; \
typedef struct __FILEStruct FILE;\
";

/* all stdio functions */
struct LibraryFunction StdioFunctions[] = {
    {StdioFopen,     "FILE *fopen(char *, char *);"                },
    {StdioFreopen,   "FILE *freopen(char *, char *, FILE *);"      },
    {StdioFclose,    "int fclose(FILE *);"                         },
    {StdioFread,     "int fread(void *, int, int, FILE *);"        },
    {StdioFwrite,    "int fwrite(void *, int, int, FILE *);"       },
    {StdioFgetc,     "int fgetc(FILE *);"                          },
    {StdioFgetc,     "int getc(FILE *);"                           },
    {StdioFgets,     "char *fgets(char *, int, FILE *);"           },
    {StdioFputc,     "int fputc(int, FILE *);"                     },
    {StdioFputs,     "int fputs(char *, FILE *);"                  },
    {StdioRemove,    "int remove(char *);"                         },
    {StdioRename,    "int rename(char *, char *);"                 },
    {StdioRewind,    "void rewind(FILE *);"                        },
    {StdioTmpfile,   "FILE *tmpfile();"                            },
    {StdioClearerr,  "void clearerr(FILE *);"                      },
    {StdioFeof,      "int feof(FILE *);"                           },
    {StdioFerror,    "int ferror(FILE *);"                         },
    {StdioFileno,    "int fileno(FILE *);"                         },
    {StdioFflush,    "int fflush(FILE *);"                         },
    {StdioFgetpos,   "int fgetpos(FILE *, int *);"                 },
    {StdioFsetpos,   "int fsetpos(FILE *, int *);"                 },
    {StdioFtell,     "int ftell(FILE *);"                          },
    {StdioFseek,     "int fseek(FILE *, int, int);"                },
    {StdioPerror,    "void perror(char *);"                        },
    {StdioPutc,      "int putc(char *, FILE *);"                   },
    {StdioPutchar,   "int putchar(int);"                           },
    {StdioPutchar,   "int fputchar(int);"                          },
    {StdioSetbuf,    "void setbuf(FILE *, char *);"                },
    {StdioSetvbuf,   "void setvbuf(FILE *, char *, int, int);"     },
    {StdioUngetc,    "int ungetc(int, FILE *);"                    },
    {StdioPuts,      "int puts(char *);"                           },
    {StdioGets,      "char *gets(char *);"                         },
    {StdioGetchar,   "int getchar();"                              },
    {StdioPrintf,    "int printf(char *, ...);"                    },
    {StdioFprintf,   "int fprintf(FILE *, char *, ...);"           },
    {StdioSprintf,   "int sprintf(char *, char *, ...);"           },
    {StdioSnprintf,  "int snprintf(char *, int, char *, ...);"     },
    {StdioScanf,     "int scanf(char *, ...);"                     },
    {StdioFscanf,    "int fscanf(FILE *, char *, ...);"            },
    {StdioSscanf,    "int sscanf(char *, char *, ...);"            },
    {StdioVprintf,   "int vprintf(char *, va_list);"               },
    {StdioVfprintf,  "int vfprintf(FILE *, char *, va_list);"      },
    {StdioVsprintf,  "int vsprintf(char *, char *, va_list);"      },
    {StdioVsnprintf, "int vsnprintf(char *, int, char *, va_list);"},
    {StdioVscanf,    "int vscanf(char *, va_list);"                },
    {StdioVfscanf,   "int vfscanf(FILE *, char *, va_list);"       },
    {StdioVsscanf,   "int vsscanf(char *, char *, va_list);"       },
    {NULL,           NULL                                          }
};

/* creates various system-dependent definitions */
void StdioSetupFunc(Picoc* pc)
{
    struct ValueType* StructFileType;
    struct ValueType* FilePtrType;

    /* make a "struct __FILEStruct" which is the same size as a
        native FILE structure */
    StructFileType =
        TypeCreateOpaqueStruct(pc, NULL, TableStrRegister(pc, "__FILEStruct"), sizeof(FILE));

    /* get a FILE * type */
    FilePtrType = TypeGetMatching(pc, NULL, StructFileType, TypePointer, 0, pc->StrEmpty, true);

    /* make a "struct __va_listStruct" which is the same size as
        our struct StdVararg */
    TypeCreateOpaqueStruct(pc, NULL, TableStrRegister(pc, "__va_listStruct"), sizeof(FILE));

    /* define EOF equal to the system EOF */
    VariableDefinePlatformVar(pc, NULL, "EOF", &pc->IntType, (union AnyValue*)&EOFValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "SEEK_SET", &pc->IntType, (union AnyValue*)&SEEK_SETValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "SEEK_CUR", &pc->IntType, (union AnyValue*)&SEEK_CURValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "SEEK_END", &pc->IntType, (union AnyValue*)&SEEK_ENDValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "BUFSIZ", &pc->IntType, (union AnyValue*)&BUFSIZValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "FILENAME_MAX", &pc->IntType, (union AnyValue*)&FILENAME_MAXValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "_IOFBF", &pc->IntType, (union AnyValue*)&_IOFBFValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "_IOLBF", &pc->IntType, (union AnyValue*)&_IOLBFValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "_IONBF", &pc->IntType, (union AnyValue*)&_IONBFValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "L_tmpnam", &pc->IntType, (union AnyValue*)&L_tmpnamValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "GETS_MAX", &pc->IntType, (union AnyValue*)&GETS_MAXValue, false);

    /* define stdin, stdout and stderr */
    VariableDefinePlatformVar(pc, NULL, "stdin", FilePtrType, (union AnyValue*)&stdinValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "stdout", FilePtrType, (union AnyValue*)&stdoutValue, false);
    VariableDefinePlatformVar(
        pc, NULL, "stderr", FilePtrType, (union AnyValue*)&stderrValue, false);

    /* define NULL, true and false */
    if (!VariableDefined(pc, TableStrRegister(pc, "NULL")))
    {
        VariableDefinePlatformVar(
            pc, NULL, "NULL", &pc->IntType, (union AnyValue*)&Stdio_ZeroValue, false);
    }
}

/* portability-related I/O calls */
void PrintCh(char OutCh, FILE* Stream)
{
    putc(OutCh, Stream);
}

void PrintSimpleInt(long Num, FILE* Stream)
{
    fprintf(Stream, "%ld", Num);
}

void PrintStr(const char* Str, FILE* Stream)
{
    fputs(Str, Stream);
}

void PrintFP(double Num, FILE* Stream)
{
    fprintf(Stream, "%f", Num);
}
