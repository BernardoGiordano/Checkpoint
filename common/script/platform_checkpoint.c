#include "interpreter.h"
#include "picoc.h"

#ifdef USE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

/* DEBUGGER here only enables the per-statement hook scriptabort.c turns into
 * the script kill switch — no SIGINT-driven manual break on a console. */
#ifdef DEBUGGER
static int gEnableDebugger = true;
#else
static int gEnableDebugger = false;
#endif
void PlatformInit(Picoc* pc) {}

void PlatformCleanup(Picoc* pc) {}

/* get a line of interactive input */
char* PlatformGetLine(char* Buf, int MaxLen, const char* Prompt)
{
#ifdef USE_READLINE
    if (Prompt != NULL) {
        /* use GNU readline to read the line */
        char* InLine = readline(Prompt);
        if (InLine == NULL) {
            return NULL;
        }

        Buf[MaxLen - 1] = '\0';
        strncpy(Buf, InLine, MaxLen - 2);
        strncat(Buf, "\n", MaxLen - 2);

        if (InLine[0] != '\0') {
            add_history(InLine);
        }

        free(InLine);
        return Buf;
    }
#endif

    if (Prompt != NULL) {
        printf("%s", Prompt);
    }

    fflush(stdout);
    return fgets(Buf, MaxLen, stdin);
}

/* get a character of interactive input */
int PlatformGetCharacter()
{
    fflush(stdout);
    return getchar();
}

/* write a character to the console */
void PlatformPutc(unsigned char OutCh, union OutputStreamInfo* Stream)
{
    putchar(OutCh);
}

/* read a file into memory */
char* PlatformReadFile(Picoc* pc, const char* FileName)
{
    struct stat FileInfo;
    char* ReadText;
    FILE* InFile;
    int BytesRead;
    char* p;

    if (stat(FileName, &FileInfo)) {
        ProgramFailNoParser(pc, "can't read file %s\n", FileName);
    }

    ReadText = malloc(FileInfo.st_size + 1);
    if (ReadText == NULL) {
        ProgramFailNoParser(pc, "out of memory\n");
    }

    InFile = fopen(FileName, "r");
    if (InFile == NULL) {
        ProgramFailNoParser(pc, "can't read file %s\n", FileName);
    }

    BytesRead = fread(ReadText, 1, FileInfo.st_size, InFile);
    if (BytesRead == 0) {
        ProgramFailNoParser(pc, "can't read file %s\n", FileName);
    }

    ReadText[BytesRead] = '\0';
    fclose(InFile);

    if ((ReadText[0] == '#') && (ReadText[1] == '!')) {
        for (p = ReadText; (*p != '\0') && (*p != '\r') && (*p != '\n'); ++p) {
            *p = ' ';
        }
    }

    return ReadText;
}

/* read and scan a file for definitions */
void PicocPlatformScanFile(Picoc* pc, const char* FileName)
{
    char* SourceStr = PlatformReadFile(pc, FileName);

    /* ignore "#!/path/to/picoc" .. by replacing the "#!" with "//" */
    if (SourceStr != NULL && SourceStr[0] == '#' && SourceStr[1] == '!') {
        SourceStr[0] = '/';
        SourceStr[1] = '/';
    }

    PicocParse(pc, FileName, SourceStr, strlen(SourceStr), true, false, true, gEnableDebugger);
}

/* exit the program */
void PlatformExit(Picoc* pc, int RetVal)
{
    pc->PicocExitValue = RetVal;
    longjmp(pc->PicocExitBuf, 1);
}
