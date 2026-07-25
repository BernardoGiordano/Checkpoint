# Checkpoint scripting

Checkpoint is a **save management framework**: everything the app can do to a save —
read it, rewrite it, package it, send it somewhere — is also reachable from a script
that ships on the SD card. A script is a plain `.c` file, interpreted on the console
by [picoc](https://github.com/FlagBrew/picoc), with a native API (`<checkpoint.h>`)
bound to Checkpoint's own save archives, title catalog, HTTP stack, zip framing,
JSON parser and UI.

Nothing is compiled and nothing is flashed: drop a file in a folder on the SD card
and it appears in the console's **Scripts** menu on the next launch. The three
bundled scripts (a cheat manager, a Play Coins editor, a Google Drive sync) are
written against exactly the API documented here.

This document is the complete reference. If you are looking for runnable code, read
[`examples/example.c`](examples/example.c): it is a menu with one function per API
area, written to be read top to bottom.

---

## Contents

1. [Quick start](#1-quick-start)
2. [Where scripts live](#2-where-scripts-live)
3. [Running a script on the console](#3-running-a-script-on-the-console)
4. [The execution model](#4-the-execution-model)
5. [The language: picoc's C](#5-the-language-picocs-c)
6. [Standard library](#6-standard-library)
7. [The Checkpoint API](#7-the-checkpoint-api)
8. [Recipes](#8-recipes)
9. [Validating and debugging a script](#9-validating-and-debugging-a-script)
10. [Bundled scripts](#10-bundled-scripts)
11. [Contributing a script to Checkpoint](#11-contributing-a-script-to-checkpoint)
12. [Authoring checklist (humans and AI agents)](#12-authoring-checklist-humans-and-ai-agents)

---

## 1. Quick start

Write the file:

```c
#include <checkpoint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    char* name;
    int idx;

    /* argv[0] is the title that was selected, or "" if there wasn't one */
    idx = argv[0][0] != '\0' ? title_find(argv[0]) : -1;
    if (idx >= 0) {
        name = title_name(idx);
        printf("launched on %s (%s)\n", name, argv[0]);
        script_log(name);
        free(name);
    }

    gui_message("Hello from a Checkpoint script!");
    return 0;
}
```

Copy it onto the SD card:

| Console | Path |
| --- | --- |
| 3DS | `/3ds/Checkpoint/scripts/universal/hello.c` |
| Switch | `/switch/Checkpoint/scripts/universal/hello.c` |

Then, on the console: **SELECT** (3DS) or **Minus** (Switch) → **Scripts** → `hello`
→ confirm. The transcript appears on the log pane; the dialog appears on the
interaction screen.

Before you copy anything, parse-check it on your PC:

```bash
tools/scriptlint.sh path/to/hello.c
```

---

## 2. Where scripts live

Checkpoint merges two roots, in this order:

1. **Bundled** — `romfs:/scripts`, packed into the app from this repository's
   `scripts/` folder.
2. **SD card** — `<app root>/scripts`, i.e. `/3ds/Checkpoint/scripts` or
   `sdmc:/switch/Checkpoint/scripts`.

Inside either root, the folder decides *when* a script is offered:

| Folder | Offered for |
| --- | --- |
| `universal/` | every title, and also when no title is selected |
| `<TITLEID>/` | that title only — 16 uppercase hex digits, e.g. `0004000000055D00/` |

Rules that follow from the merge:

- An SD file **shadows** a bundled file of the same name; the picker tags it
  `override`. That is how you patch a bundled script without rebuilding the app.
- The file name minus `.c` is the display name. Only files **directly** in the
  folder are scripts; subfolders are ignored, so a script may keep assets in one.
- Universal entries are listed first, then the selected title's own, each group
  sorted alphabetically.

In this repository the bundled tree is:

```
scripts/
  common/      packed into both consoles
    universal/
  3ds/         3DS only
    universal/
  switch/      Switch only
    universal/
  examples/    NOT packed — read/copy by hand
```

Both target Makefiles wipe `<romfs>/scripts` and copy `common/` then
`<target>/` over it before packing, so `3ds/assets/romfs/scripts/` and
`switch/romfs/scripts/` are build output (gitignored). **Never edit those** — edit
`scripts/`.

---

## 3. Running a script on the console

**Launching.** 3DS: **SELECT** → *Scripts*. Switch: **Minus** → *Scripts*, or the
scripts glyph in the side rail. The picker is blocked while the title catalog is
still loading and while a backup, restore or another script is running. Picking an
entry asks for confirmation, then the run takes over the display.

**During a run** the script owns the screen(s):

| | 3DS | Switch |
| --- | --- | --- |
| Transcript | top screen, monospace, nothing else | whole screen |
| Status line (`gui_status`) | bottom screen header | header, right-hand note |
| Progress bars | bottom screen | above the hint row |
| Dialogs (`gui_*`) | take over the bottom screen | centred card over a dimmed transcript |

**Controls**

| Action | 3DS | Switch |
| --- | --- | --- |
| Scroll transcript by line | D-Pad ↑/↓, or **L**/**R** | D-Pad ↑/↓, or **L**/**R** |
| Page transcript | D-Pad ←/→ | D-Pad ←/→ |
| Push a dialog aside to read the log | — (log is the other screen) | **Y** |
| **Abort the script** | hold **B** ≈¾ s | hold **B** ≈¾ s |
| Close the finished run | **A**, **B** or **START** | **A**, **B** or **+** |

**L**/**R** always scroll, even while a dialog owns the D-Pad, so the transcript
stays readable while the script is asking a question.

On 3DS the HOME menu is blocked for the whole run: picoc cannot be preempted, so a
runaway script must be killed with hold-B rather than by suspending the app.

**When it ends**, the run reports success (`main` returned 0), failure (any other
value, an interpreter error, or an argument that failed validation) or *cancelled*
(you aborted it). The tail of the transcript is also written to Checkpoint's log
file under `<app root>/logs`, together with every `script_log` line — that is what
a bug report should carry.

---

## 4. The execution model

### Entry point

Checkpoint parses the file and then calls **`main`**, so a script must define one:
a file without `main` fails the run with `main() is not defined`. (Statements at
file scope do execute, during the parse, before `main` — but keep initialisation in
`main` where its failures can be reported.) Both forms work:

```c
int main(int argc, char** argv)   /* argc is always 1 */
void main(void)                   /* exit value is 0 */
```

`argv[0]` is the id of the title that was highlighted when the script was launched,
as 16 uppercase hex characters, or `""` when nothing was selected (a universal
script started with an empty list, for instance). `selected_title()` returns the
same string from anywhere.

**Return 0 for success.** Any other value is shown to the user as a failed run.
`exit(n)` from anywhere in the script does the same thing.

### One thread, one run

A script runs on a worker thread, one at a time, at a priority just below the UI
thread. Consequences worth designing around:

- **`gui_*` calls block** the script until the user answers, because the dialogs
  live on the UI thread. `gui_status` and every `progress_*` call are the
  exception: they only write state and return immediately, so they are safe inside
  a tight copy loop.
- The **title catalog is frozen** for the duration of the run. Catalog indices are
  stable while the script runs and meaningless afterwards — never persist one.
- Long native calls (`web_*`, `zip_dir`, `unzip`) run inside a single statement, so
  they poll the abort flag themselves rather than waiting for the next statement.

### Memory

Every string the API returns, and everything the script `malloc`s, is tracked on a
**run-scoped heap** that is released when the run ends — whatever the exit path
(return, `exit()`, an interpreter error, hold-B). A leak cannot outlive the script.

Inside a long loop it still matters: picoc's own heap is **64 KB**, so free what you
took, especially HTTP response bodies and `sav_read` buffers.

| Returned by | Release with |
| --- | --- |
| any `char*` from the API | `free()` (always safe, never mandatory) |
| `struct directory*` from `read_directory` / `sav_list` | `delete_directory()` |
| `struct JSON*` from `json_new` | `json_delete()` |
| `struct JSON*` from `json_array_element` / `json_object_element` | **nothing** — it is borrowed from the parent tree |

`delete_directory` frees the *listing*; it never deletes anything on disk.
`json_delete` on a borrowed element ends the run with a diagnostic instead of
corrupting the heap.

File bytes handled by `zip_dir`, `unzip` and `web_upload_file` never enter the
interpreter heap, so a multi-megabyte save is fine.

### Cancellation

Holding **B** for about three quarters of a second raises the abort flag. Then:

- picoc fails the run at the **next statement** — even an infinite loop dies
  without rebooting the console;
- a blocked `gui_*` call returns its "cancelled" answer immediately (`0` for
  `gui_confirm`, `-1` for `gui_pick_one`/`gui_numpad`, `""` for `gui_keyboard`) so
  the script can reach that next statement;
- `zip_dir` and `unzip` stop and return `-2`; an upload in `web_upload_file` stops
  too;
- afterwards the heap is released and every open save archive is closed for you.

Your script cannot poll or veto an abort. What it *can* do is keep the console in a
state that survives one: do the risky write as late as possible, and keep
`sav_commit` immediately behind `sav_write`.

### Errors

Two kinds of failure end a run:

- **Interpreter errors** — a syntax error, an undefined identifier, a bad
  operation. Reported as `file.c:line:col message` in the transcript.
- **Argument validation** — every binding checks its arguments (types, NULLs,
  ranges, counts, handle validity) and ends the run with
  `binding: argument N is …`. An out-of-range title index or a stale save handle
  is a *script bug*, and it is reported as one rather than returned as an error
  code.

Error *codes* (negative returns) are reserved for things outside the script's
control: a missing file, a network failure, an unsupported title.

### Limits

| | |
| --- | --- |
| Interpreter heap | 64 KB (variables, strings, parse nodes) |
| Open save archives | 8 handles per run |
| Progress layers | 3 (`0`–`2`), plus an automatic innermost row for native I/O |
| Transcript | 250 lines, scrollable; wrapped to the pane's width (~56 columns) |
| `gui_keyboard` text | 63 characters on 3DS (buffer capped at 64 bytes); no small cap on Switch |
| List arguments (`gui_pick_*`) | 65536 items maximum |

---

## 5. The language: picoc's C

picoc is a small C89-flavoured interpreter, not a compiler. It has structs, unions,
enums, pointers, function pointers, arrays, `switch`, `do`/`while`, `goto`
(forward only), `sizeof`, `float`/`double`, and a simplified preprocessor.

### What is not there

| Not supported | Write instead |
| --- | --- |
| `const` and `volatile` — **parse errors, not ignored keywords** (`register` is accepted and ignored) | drop the qualifier: `char* s`, not `const char* s` |
| pointer arithmetic on arrays: `s + i`, `*(s + i)` | index: `s[i]` |
| adjacent string-literal concatenation: `"a" "b"` | one literal, however long |
| `long long` / `unsigned long long` / 64-bit integers | 16-hex-digit **strings** (that is why title ids cross the API as strings) |
| variable-length arrays: `char buf[n]` | a `#define`d bound (`char buf[PATHN]`); an expression of constants is fine (`char buf[N * 2]`) |
| `struct`/`union`/`enum` definitions **inside** a function | define them at file scope |
| calling a function defined later in the file | define callees first — `main` goes last |
| `typedef` of function pointer types, arrays of function pointers | use the type directly |
| K&R function declarations | ANSI only |
| `defined()` in `#if` | `#ifdef` / `#ifndef` |
| `#define` macros as statements | macros must evaluate to a value; `#define URL "https://…"` and `#define SQ(x) ((x) * (x))` are fine |
| multi-file scripts | keep a script self-contained: one `.c` file |

### What works, and is easy to doubt

Verified against the interpreter this app ships:

- `switch`/`case`/`default`, `break`, `continue`, `while (1)`, `do`/`while`,
  recursion, the ternary operator.
- Declarations anywhere in a block, including `for (int i = 0; …)`, and locals
  inside a loop body across repeated calls.
- Struct assignment and struct-by-value parameters; arrays of structs; 2-D arrays;
  `->` on struct pointers; `sizeof(struct x)`; `sizeof(a) / sizeof(a[0])`.
- Initializers: `int a[3] = {1, 2, 3};`, `char msg[16] = "hi";`,
  `char* items[3] = {"a", "b", "c"};`.
- `short`, `unsigned char`, `unsigned int`, `long`, and `typedef` of a struct or a
  scalar (`typedef struct pt Point;`) — just not of a function pointer type.
- `malloc`/`free` with indexing (`p[i]`), casts (`(int*)malloc(…)`), out
  parameters (`&v` into an `int*`), array-to-pointer decay when passing `char buf[]`
  to a `char*` parameter.
- `int` is 32-bit and `unsigned int` behaves; bit operations and shifts work;
  `double`/`float` arithmetic and `%f` work; `math.h` is available.
- `//` comments lex fine — but every bundled script uses `/* … */`, so match that
  house style.
- `static` locals do keep their value between calls in this fork (they are name-
  mangled per file and function), unlike upstream picoc where the keyword is
  ignored. The bundled scripts use file-scope globals instead; prefer that, since
  it is the behaviour that cannot surprise you.

### Headers

Only these can be `#include`d — nine picoc stdlib headers plus Checkpoint's API:

```c
#include <checkpoint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>
```

Nothing is included implicitly: without `#include <checkpoint.h>` the whole
Checkpoint API is undefined, and the run dies on the first call to it. Anything
that is not one of those names is treated as a **file path** and read as source,
which on a console means "can't read file" — so do not `#include "helpers.h"`.

---

## 6. Standard library

The registered functions, per header. This is the picoc stdlib in
`common/script/cstdlib/` (a newlib-safe fork), so it is exactly what the console
has — not what your host compiler has.

**`<stdio.h>`** — `fopen` `freopen` `fclose` `fread` `fwrite` `fgetc` `getc` `fgets`
`fputc` `fputs` `remove` `rename` `rewind` `tmpfile` `clearerr` `feof` `ferror`
`fileno` `fflush` `fgetpos` `fsetpos` `ftell` `fseek` `perror` `putc` `putchar`
`setbuf` `setvbuf` `ungetc` `puts` `printf` `fprintf` `sprintf` `snprintf` `scanf`
`fscanf` `sscanf` `vprintf` `vfprintf` `vsprintf` `vsnprintf` `vscanf` `vfscanf`
`vsscanf`

**`<stdlib.h>`** — `malloc` `calloc` `realloc` `free` `atof` `atoi` `atol` `strtod`
`strtol` `strtoul` `rand` `srand` `abs` `labs` `div` `ldiv` `getenv` `system`
`abort` `exit`

**`<string.h>`** — `memcpy` `memmove` `memchr` `memcmp` `memset` `strcat` `strncat`
`strchr` `strrchr` `strcmp` `strncmp` `strcasecmp` `strncasecmp` `strcoll` `strcpy`
`strncpy` `strdup` `strerror` `strlen` `strspn` `strcspn` `strpbrk` `strstr`
`strtok` `strtok_r` `strxfrm` `index` `rindex`

**`<unistd.h>`** — useful on a console: `sleep` `usleep` `mkdir` `rmdir` `unlink`
`access` `getcwd` `chdir` `truncate` `ftruncate` `read` `write` `close` `lseek`
`fsync` `isatty`. Also registered but meaningless here: `fork` `dup` `dup2` `link`
`sbrk` `gethostid` `getpid` `getwd`.

**`<ctype.h>`** — `isalnum` `isalpha` `isblank` `iscntrl` `isdigit` `isgraph`
`islower` `isprint` `ispunct` `isspace` `isupper` `isxdigit` `isascii` `tolower`
`toupper` `toascii`

**`<math.h>`** — `acos` `asin` `atan` `atan2` `ceil` `cos` `cosh` `exp` `fabs`
`floor` `fmod` `frexp` `ldexp` `log` `log10` `modf` `pow` `round` `sin` `sinh`
`sqrt` `tan` `tanh`

**`<time.h>`** — `time` `clock` `difftime` `mktime` `gmtime` `gmtime_r` `localtime`
`asctime` `ctime` `strftime` `strptime` (`time_t` is an `int`; there is no
`timegm`)

**`<errno.h>`** — the `errno` variable and the `E*` constants.
**`<stdbool.h>`** — `bool`, `true`, `false`.

`printf` writes to the transcript pane. `fopen` and friends see the real SD card:
paths are bare on 3DS (`/3ds/Checkpoint/…`) and `sdmc:`-prefixed on Switch
(`sdmc:/switch/Checkpoint/…`) — build them from `app_root()` and one script serves
both consoles.

---

## 7. The Checkpoint API

`#include <checkpoint.h>`. The authoritative prototype list is the table in
[`common/script/library_checkpoint.c`](../common/script/library_checkpoint.c); the
implementations are in `common/script/`.

**Conventions**

- **Title ids** cross the boundary as 16-character uppercase hex strings, because
  picoc has no 64-bit integers.
- **Titles** are otherwise addressed by *catalog index*, `0 … titles_count()-1`,
  the same order as Checkpoint's Save list. Valid only during the run.
- **`kind`** is `0` for save data and `1` for extdata (3DS only).
- Functions returning `char*` return a heap block you may `free()`.
- `int` returns are `0`/positive on success and **negative on failure**; each
  negative value is documented below.
- Passing a bad argument (NULL string, out-of-range index, dead handle) is a
  script bug: the run ends with a diagnostic naming the binding and the argument.

### 7.1 Titles

| Signature | Returns |
| --- | --- |
| `int titles_count(void)` | number of titles in the catalog |
| `int title_find(char* idHex)` | catalog index of that id, `-1` if absent |
| `char* title_id(int idx)` | id as 16 uppercase hex digits |
| `char* title_name(int idx)` | display name |
| `char* title_product_code(int idx)` | product code, `""` on Switch |
| `int title_is_cart(int idx)` | `1` for a game card, else `0` (always `0` on Switch) |
| `int title_has_save(int idx)` | `1`/`0` |
| `int title_has_extdata(int idx)` | `1`/`0` (always `0` on Switch) |
| `char* title_backup_path(int idx, int kind)` | backup folder for that title, with a trailing `/`; `""` if the platform has no backups of that kind |

`title_backup_path` is the *parent* of the individual backup folders — list it with
`read_directory` to find them. On Switch the catalog is the list for the currently
selected user profile.

### 7.2 Save archives

A handle is an open save (or extdata) archive. Paths are archive-absolute:
`"/file.bin"`, `"/sub/dir/file.bin"`.

| Signature | Returns |
| --- | --- |
| `int sav_open(int titleIdx, int kind)` | handle ≥ 0; `-1` unsupported title/kind, `-2` all 8 handles taken, else a negative platform result |
| `int sav_open_shared(char* extdataIdHex)` | handle ≥ 0 for a console-wide shared extdata archive; `-1` where the platform has none (always on Switch), `-2` no free handle |
| `int sav_read(int h, char* path, char** out, int* outSize)` | `0` ok; `-3` out of memory, `-4` short read, else a negative platform result |
| `int sav_write(int h, char* path, char* data, int size)` | `0` ok, else negative |
| `int sav_delete(int h, char* path)` | `0` ok, else negative |
| `struct directory* sav_list(int h, char* path)` | listing, or `NULL` on error |
| `int sav_commit(int h)` | `0` ok, else negative |
| `void sav_close(int h)` | — |

- `sav_read` allocates `*out` for you, NUL-terminated one byte past `*outSize`
  (so a text file can be used as a string). On failure `*out` is `NULL` and
  `*outSize` is `0` — a partial file is **never** handed over as a whole one.
- `sav_write` is create-or-replace: an existing file is dropped first, so a
  shrinking write leaves no old tail. A zero-length write truncates.
- **`sav_commit` is what makes writes real.** It also clears the title's secure
  value, exactly as a restore does, and is a no-op on extdata and on Switch.
  Commit right after writing.
- `sav_list` returns full archive-absolute paths; folders carry a trailing `/`.
  Free it with `delete_directory` (not `free`).
- `sav_close` is lenient — closing a closed or bogus handle is a no-op, so cleanup
  paths can close unconditionally. Every handle is force-closed after the run.
- Unsupported by design (`sav_open` → `-1`): GBA Virtual Console, DSiWare and SPI
  cartridge saves on 3DS, extdata on Switch.

`sav_open_shared` is keyed by id rather than by a title because the archive belongs
to the console: the low 32 bits of the 16-hex id are the extdata id and the high 32
the archive magic. The Home Menu shared extdata `"00048000F000000B"` (which holds
`/gamecoin.dat`) is the practical example — see
[`3ds/universal/playcoins.c`](3ds/universal/playcoins.c).

### 7.3 SD card

| Signature | Returns |
| --- | --- |
| `struct directory* read_directory(char* dir)` | entries as **full paths**; `count` is `0` for a missing or unreadable directory |
| `void delete_directory(struct directory* dir)` | frees the listing (`NULL` accepted) |
| `int sd_mkdirs(char* path)` | `mkdir -p`: `0` ok, `-1` failed |
| `int sd_exists(char* path)` | `1`/`0` |

```c
struct directory { int count; char** files; };
```

`read_directory` does not tell you which entries are folders — `sd_exists` plus a
failed `fopen`, or a `remove` that fails and an `rmdir` that succeeds, is the usual
way to tell. Plain stdio (`fopen`, `fread`, `fwrite`, `remove`, `rename`, `rmdir`)
works on the same paths for everything else.

### 7.4 Zip

| Signature | Returns |
| --- | --- |
| `int zip_dir(char* srcDir, char* outZipPath)` | `0` ok, `-1` error, `-2` cancelled by hold-B |
| `int unzip(char* zipPath, char* outDir)` | `0` ok, `-1` error, `-2` cancelled |

Store-only (no compression) zips over the same framing — CRC and path safety —
that Checkpoint's wireless transfer and `tools/chlink` use, so the archives
interoperate. Entries carry `/`-separated relative paths. Both calls stream through
files, drive the automatic innermost progress row while they run, and poll the
abort flag between chunks.

### 7.5 Network

| Signature | Returns |
| --- | --- |
| `char* net_ip(void)` | console IP, `"0.0.0.0"` with no network |
| `int web_get(char** out, int* outSize, char* url)` | HTTP status, or negative (below) |
| `int web_request(char* method, char* url, char* headers, char* body, int bodySize, char** out, int* outSize, char** respHeaders)` | HTTP status, or negative |
| `int web_upload_file(char* method, char* url, char* headers, char* filePath, char** out, int* outSize, char** respHeaders)` | HTTP status, or negative (plus `-3`) |
| `char* url_encode(char* s)` | percent-encoded copy |
| `char* http_header_value(char* headers, char* key)` | that header's value, `""` if absent (case-sensitive key) |

Negative returns, shared by all three transfer calls:

| | |
| --- | --- |
| `-1` | no HTTP stack available |
| `-2` | the response did not fit in memory — the one worth retrying smaller |
| `-3` | *(`web_upload_file` only)* `filePath` could not be opened or sized |
| `-(CURLcode + 100)` | transfer error |

- `method` is `"GET"`, `"POST"`, `"PUT"`, `"PATCH"` or `"DELETE"`.
- `headers` is `"\n"`-separated `"Key: Value"` lines, `""` for none.
- `body`/`bodySize` is the request body (`""`/`0` for none) and should stay small —
  it passes through the interpreter heap. For anything large, use
  `web_upload_file`, which streams the file straight from SD, drives the innermost
  progress row with bytes sent, and can be aborted with hold-B.
- `out` is the malloc'd, NUL-terminated response body (`NULL`/`0` on failure);
  `respHeaders` is the raw response header block, or you may pass `NULL` if you do
  not need it. Free what you took.
- `web_upload_file` does not follow redirects (a redirect would re-send the body).

### 7.6 JSON

nlohmann's parser behind an opaque handle: `struct JSON*`.

| Signature | Returns |
| --- | --- |
| `struct JSON* json_new(void)` | a new, *invalid* tree — `json_delete` it |
| `void json_parse(struct JSON* obj, char* data)` | — (parse into an existing tree) |
| `void json_delete(struct JSON* obj)` | — (roots only) |
| `int json_is_valid(struct JSON* obj)` | `0` if parsing failed |
| `int json_is_int / json_is_bool / json_is_string / json_is_array / json_is_object(struct JSON*)` | `1`/`0` |
| `int json_get_int(struct JSON* obj)` | the number |
| `int json_get_bool(struct JSON* obj)` | `1`/`0` |
| `char* json_get_string(struct JSON* obj)` | the string |
| `int json_array_size(struct JSON* obj)` | element count (`1` for a scalar, `0` for null) |
| `struct JSON* json_array_element(struct JSON* obj, int index)` | borrowed element |
| `int json_object_contains(struct JSON* obj, char* name)` | `1`/`0` |
| `struct JSON* json_object_element(struct JSON* obj, char* name)` | borrowed member |
| `char* json_object_key(struct JSON* obj, int index)` | the *n*-th key of an object |

The three getters are strict: `json_get_int` on a string ends the run. Check with
`json_is_*` first. Likewise `json_array_element` with a bad index, and
`json_object_element` with a missing name, are script bugs — guard them with
`json_array_size` and `json_object_contains`.

Elements are **borrowed**: never `json_delete` (or `free`) one. Only a `json_new`
root is owned by the script.

```c
struct JSON* root;
struct JSON* item;

root = json_new();
json_parse(root, body);
if (!json_is_valid(root)) {
    json_delete(root);
    return -1;
}
if (json_object_contains(root, "items") && json_array_size(json_object_element(root, "items")) > 0) {
    item = json_array_element(json_object_element(root, "items"), 0);
    /* … */
}
json_delete(root);
```

### 7.7 Dialogs

Every call here except `gui_status` blocks the script until the user answers.

| Signature | Returns |
| --- | --- |
| `void gui_message(char* text)` | — |
| `int gui_confirm(char* text)` | `1` yes, `0` no/cancelled |
| `int gui_pick_one(char* prompt, char** items, int count)` | chosen index, `-1` cancelled |
| `int gui_pick_many(char* prompt, char** items, int count, int* selected)` | `1` confirmed, `0` cancelled |
| `void gui_keyboard(char* out, char* hint, int maxChars)` | — (writes into `out`) |
| `int gui_numpad(char* prompt, int min, int max)` | the value, `-1` cancelled |
| `void gui_status(char* text)` | — (non-blocking header line) |

- `gui_pick_many`'s `selected` array is **in and out**: `1`/`0` per item on the way
  in (the pre-checked rows), rewritten with the user's choice when the call returns
  `1`, untouched when it returns `0`. It must have exactly `count` elements.
- `gui_keyboard`'s `maxChars` is the size of `out` **including** the terminator, so
  it caps the input too. `out` is `""` if the user cancelled. On 3DS the effective
  limit is 63 characters.
- `gui_numpad` constrains input in the keyboard itself: out-of-range values cannot
  be entered. Pass `min >= 0` so the `-1` cancel sentinel stays unambiguous.
- `gui_status` sets the one-line status shown while the script works — the place
  for "Downloading database…" — and returns immediately.

### 7.8 Progress

Non-blocking by construction: these only write state, so a copy loop can report
every chunk without ever waiting for a frame.

| Signature | Meaning |
| --- | --- |
| `void progress_begin(int layer, char* label, int total)` | start/reset `layer` at `0/total` and drop every deeper layer |
| `void progress_set(int layer, int done)` | update the count |
| `void progress_label(int layer, char* label)` | relabel without resetting |
| `void progress_end(int layer)` | remove the layer |
| `void progress_note(char* text)` | text for the innermost I/O row ("preparing", "creating the folder") |
| `void progress_clear(void)` | remove everything |

Layers nest, outermost is `0`, maximum `3` (`0`–`2`): outer bar = items, inner bar =
the current item. `total <= 0` renders an indeterminate bar showing the raw count.
Because `progress_begin` drops deeper layers, starting the next outer item can
never leave a stale inner bar behind.

Below your layers sits a row that `zip_dir`, `unzip` and `web_upload_file` drive
themselves (bytes moved). `progress_note` is what that row says while no native
call is running, and setting it also puts the row on screen, so the line is
labelled from the first stage instead of appearing blank when a zip starts.

### 7.9 Logging and environment

| Signature | Returns |
| --- | --- |
| `void script_log(char* msg)` | — (one line to the transcript **and** Checkpoint's log file) |
| `char* selected_title(void)` | the launching title id as 16 hex digits, `""` if none |
| `char* app_root(void)` | `"/3ds/Checkpoint"` or `"sdmc:/switch/Checkpoint"` |

`printf` reaches the transcript only. Use `printf` for chatter and `script_log` for
anything worth keeping in a bug report.

`app_root()` returns the prefix in the form that platform's `fopen`/`stat` wants,
which is the whole trick to writing one script for both consoles.

### 7.10 Sealed storage

For a credential a script has to keep between runs — an OAuth refresh token, an
API key — instead of leaving it in a JSON file on the card.

| Signature | Returns |
| --- | --- |
| `int device_seal(char* plain, int plainSize, char* passphrase, char** out, int* outSize)` | `0` ok, else negative (below) |
| `int device_unseal(char* blob, int blobSize, char* passphrase, char** out, int* outSize)` | `0` ok, else negative |
| `int seal_needs_passphrase(char* blob, int blobSize)` | `1`/`0`, `-1` if not a sealed blob |

AES-256-GCM under a key mixed from two halves: material only this console's own
services can answer for (never written to the SD card), and — when `passphrase`
is not `""` — a PBKDF2 stretch of that passphrase. Pass `""` for no passphrase.

Failure codes, shared by both calls:

| Code | Meaning |
| --- | --- |
| `-1` | not a sealed blob, or `plainSize` was 0 |
| `-2` | blob written by a newer Checkpoint |
| `-3` | out of memory |
| `-4` | no console key source *and* no passphrase — nothing to derive from |
| `-5` | wrong passphrase, different console, or a tampered blob |
| `-6` | the console would not produce a salt/nonce |

`out` is a `malloc`'d block you `free()`, and stays `NULL`/`0` on failure. A
failed unseal never yields plaintext: GCM verifies the tag first, so garbage can
never be mistaken for your config.

The blob is **binary** and carries its own salt, nonce and tag. Write it to the SD
card with `fopen(..., "wb")`/`fwrite` and hand it back verbatim — and read it back
with the byte count from `ftell`, not `strlen`, since it is full of embedded NULs.

**Be accurate with users about what this protects.** Because the console-bound
half is never on the card: a card read on a PC, an SD image, or a `config/` folder
the user shares all come away with nothing, and a blob does not travel to another
console. Because neither console isolates homebrew and Checkpoint is open source:
other homebrew on the same console *can* reproduce that half by reading
[`common/script/seal_api.cpp`](../common/script/seal_api.cpp). Only the passphrase
is a real boundary. Never tell a user otherwise — see `googledrive.c` and
[`googledrive.md`](googledrive.md) for wording that holds up.

Also worth doing alongside a seal, because encryption is the weakest of the three:
keep the credential's scope as narrow as the API allows, and give the user a way
to revoke it.

---

## 8. Recipes

Patterns taken from the bundled scripts. All of them parse-check with
`tools/scriptlint.sh`.

### A menu loop

The shape most non-trivial scripts want: one place that decides, one function per
job.

```c
#define MENU_N 3

int main(int argc, char** argv)
{
    char* items[MENU_N];
    int pick;

    items[0] = "Do the thing";
    items[1] = "Do the other thing";
    items[2] = "Exit";

    pick = 0;
    while (pick >= 0 && pick < MENU_N - 1) {
        gui_status("My script");
        pick = gui_pick_one("What now?", items, MENU_N);
        if (pick == 0) {
            do_thing();
        }
        else if (pick == 1) {
            do_other_thing();
        }
    }

    progress_clear();
    return 0;
}
```

`gui_pick_one` returning `-1` (the user pressed B) falls out of the loop just like
the explicit *Exit* row.

### Cross-platform paths

```c
#define PATHN 512

char g_root[PATHN];    /* app_root() */
char g_work[PATHN];    /* <root>/myscript */

void paths_init(void)
{
    char* root;

    root = app_root();
    strncpy(g_root, root, PATHN - 1);
    g_root[PATHN - 1] = '\0';
    free(root);

    sprintf(g_work, "%s/myscript", g_root);
    sd_mkdirs(g_work);
}
```

If a script really needs to branch per console, `strncmp(g_root, "sdmc:", 5) == 0`
is true on Switch only.

### Read, modify and commit a save file

```c
int bump_counter(int titleIdx)
{
    char* data;
    int size;
    int handle;
    int res;

    handle = sav_open(titleIdx, 0);
    if (handle < 0) {
        printf("sav_open failed: %d\n", handle);
        return -1;
    }

    res = sav_read(handle, "/counter.bin", &data, &size);
    if (res != 0 || size < 4) {
        printf("sav_read failed: %d\n", res);
        sav_close(handle);
        return -1;
    }

    /* little-endian u16 at offset 0, index — never pointer arithmetic */
    data[0] = (data[0] + 1) & 0xFF;

    res = sav_write(handle, "/counter.bin", data, size);
    if (res == 0) {
        res = sav_commit(handle); /* without this the write is lost */
    }

    free(data);
    sav_close(handle);
    return res;
}
```

### List the backups of the launching title

```c
void list_backups(char* idHex)
{
    struct directory* dir;
    char* base;
    int idx;
    int i;

    idx = title_find(idHex);
    if (idx < 0) {
        gui_message("That title is not in the catalog.");
        return;
    }

    base = title_backup_path(idx, 0);
    if (base[0] == '\0') {
        free(base);
        gui_message("No backups of that kind on this console.");
        return;
    }

    dir = read_directory(base);
    for (i = 0; i < dir->count; i++) {
        script_log(dir->files[i]); /* full paths already */
    }
    printf("%d backup(s)\n", dir->count);

    delete_directory(dir);
    free(base);
}
```

### Fetch and walk a JSON document

```c
int fetch_names(char* url)
{
    struct JSON* root;
    struct JSON* list;
    struct JSON* item;
    char* body;
    char* name;
    int size;
    int status;
    int i;

    gui_status("Downloading");
    status = web_get(&body, &size, url);
    if (status != 200) {
        printf("web_get: %d\n", status);
        if (body != NULL) {
            free(body);
        }
        return -1;
    }

    root = json_new();
    json_parse(root, body);
    free(body);

    if (!json_is_valid(root) || !json_object_contains(root, "names")) {
        json_delete(root);
        return -1;
    }

    list = json_object_element(root, "names"); /* borrowed */
    progress_begin(0, "Names", json_array_size(list));
    for (i = 0; i < json_array_size(list); i++) {
        item = json_array_element(list, i);
        if (json_is_string(item)) {
            name = json_get_string(item);
            script_log(name);
            free(name);
        }
        progress_set(0, i + 1);
    }
    progress_end(0);

    json_delete(root); /* the root only; never an element */
    return 0;
}
```

### Zip a backup and upload it

```c
int upload_backup(char* backupDir, char* zipPath, char* url, char* headers)
{
    char* body;
    char* respHeaders;
    int size;
    int res;
    int status;

    progress_note("packing");
    res = zip_dir(backupDir, zipPath);
    if (res == -2) {
        return -2; /* the user aborted with hold-B */
    }
    if (res != 0) {
        return -1;
    }

    progress_note("uploading");
    /* web_upload_file streams the file and drives its own progress row */
    status = web_upload_file("PUT", url, headers, zipPath, &body, &size, &respHeaders);
    remove(zipPath);

    if (body != NULL) {
        free(body);
    }
    if (respHeaders != NULL) {
        free(respHeaders);
    }
    return (status >= 200 && status < 300) ? 0 : status;
}
```

### Nested progress over several titles

```c
void report(int titleCount)
{
    int i;
    int j;

    progress_begin(0, "Titles", titleCount);
    for (i = 0; i < titleCount; i++) {
        char* name;

        name = title_name(i);
        progress_label(0, name);
        free(name);

        /* layer 1 resets and drops nothing stale from the previous title */
        progress_begin(1, "Files", 10);
        for (j = 0; j < 10; j++) {
            progress_set(1, j + 1);
        }
        progress_end(1);

        progress_set(0, i + 1);
    }
    progress_end(0);
}
```

---

## 9. Validating and debugging a script

### Parse-check on the host

```bash
tools/scriptlint.sh                 # every bundled script, both targets
tools/scriptlint.sh path/to/x.c …   # just these
```

It builds picoc's interpreter core plus Checkpoint's platform layer and stdlib into
a host binary and runs the **real parser** over each file with execution disabled.
Needs a host `gcc` and the `3rd-party/picoc` submodule checked out
(`git submodule update --init --recursive`).

It catches syntax errors, malformed declarations and bad preprocessor use. It
**cannot** catch an unknown identifier, a wrong argument count or a wrong argument
type, because picoc resolves those when the statement actually runs. A script that
lints clean can still die on line 200 the first time that branch is taken.

Nothing else compiles scripts: they ship as romfs assets, so linting is the only
gate before the console.

### On the console

- Read the transcript. `printf` goes there live; `script_log` goes there *and* into
  `<app root>/logs`, which survives the run.
- Checkpoint's HTTP log server (the addresses are listed under Settings → Network)
  serves the same logs to a browser on your network — the fastest way to read a
  long run from a PC.
- Exercise every branch: an interpreter error only appears when its statement runs.
- Test the abort path (hold **B**) at the moments that matter, and check that a
  half-finished run left the save usable.

### Reading a failure

| Transcript | Meaning |
| --- | --- |
| `file.c:12:8 identifier expected` | syntax picoc does not accept (`long long`, `const`, …) |
| `VariableGet Ident: 'const' is undefined` | an unsupported keyword parsed as an identifier |
| `VariableGet Ident: 'free' is undefined` | the header that declares it is not `#include`d (here `<stdlib.h>`) — the linter cannot see this |
| `file.c:40:12 invalid operation` | usually pointer arithmetic on an array — index instead |
| `sav_read: argument 1 is 3, not an open save handle` | a stale or never-opened handle |
| `title_name: argument 1 is title index 99, out of range (42 titles)` | index bug — check `titles_count()` |
| `json_get_int: argument 1 is not a number` | missing `json_is_*` guard |
| `aborted by user` | hold-B; not a bug |
| `can't read file …` | an `#include` that is not one of the ten known headers |

---

## 10. Bundled scripts

| Script | Consoles | What it does |
| --- | --- | --- |
| [`common/universal/googledrive.c`](common/universal/googledrive.c) | both | Backs up save backups to the user's own Google Drive. OAuth 2.0 device flow, `drive.file` scope, store-only zip per backup folder, resumable upload. Setup guide: [`googledrive.md`](googledrive.md). |
| [`3ds/universal/sharkive.c`](3ds/universal/sharkive.c) | 3DS | Cheat manager: downloads [Sharkive](https://github.com/FlagBrew/Sharkive)'s `3ds.json`, ticks cheats per title, writes `/cheats/<TITLEID>.txt` for the Luma3DS patcher. |
| [`switch/universal/sharkive.c`](switch/universal/sharkive.c) | Switch | Same, for Atmosphere: resolves the build id and writes `/atmosphere/contents/<TITLEID>/cheats/<BUILDID>.txt`. |
| [`3ds/universal/playcoins.c`](3ds/universal/playcoins.c) | 3DS | Sets Play Coins by editing `/gamecoin.dat` in the Home Menu shared extdata. The shortest real example of `sav_open_shared`. |
| [`examples/example.c`](examples/example.c) | **not bundled** | The annotated API tour: one menu entry per API area. Copy it to `<app root>/scripts/universal/` by hand and run it while reading it. |

`sharkive.c` is genuinely per-target (Luma3DS cheat txt vs Atmosphere build-id
layout), so a copy lives in each tree — change both when the change is not
platform-specific.

---

## 11. Contributing a script to Checkpoint

1. Put the file in the tree that matches its reach: `scripts/common/universal/`
   (both consoles), `scripts/<target>/universal/`, or
   `scripts/<target>/<TITLEID>/` for a single title.
2. Head the file with a comment block saying what it does, where it stores things,
   and anything the user has to set up first — the bundled scripts all do.
3. Keep it to one file, and to the API in this document.
4. `tools/scriptlint.sh` must pass.
5. Test it on hardware, including the hold-B abort.
6. Scripts are **not** touched by `make format`, and the copies under
   `3ds/assets/romfs/scripts/` and `switch/romfs/scripts/` are build output — edit
   `scripts/` only.
7. If a script needs real setup (credentials, external accounts), add a page next
   to this one — `scripts/<name>.md` — and point at it from the file header, as
   `googledrive.c` does.

To try a bundled script without a rebuild, copy it to the SD card path of the same
name: the SD file shadows the bundled one and the picker marks it `override`.

---

## 12. Authoring checklist (humans and AI agents)

Hard constraints, in the order they usually bite. If you are generating a script
programmatically, treat this as the spec — most failures are a habit from real C
that this interpreter does not share.

**Language**

- [ ] `#include <checkpoint.h>` plus only the nine stdlib headers in §5. No other
      include, ever.
- [ ] No `const` and no `volatile` anywhere — they are parse errors, not ignored
      qualifiers.
- [ ] No `long long` and no 64-bit integers. Title ids are 16-hex **strings**.
- [ ] Index arrays (`s[i]`); never `s + i` or `*(s + i)`.
- [ ] One string literal per string: `"a" "b"` does not parse.
- [ ] Array bounds are constants — `#define PATHN 512`, then `char p[PATHN]`.
      Never `char p[n]`.
- [ ] `struct`, `union` and `enum` are declared at file scope only.
- [ ] Callees are defined before callers; `main` is last.
- [ ] Prefer file-scope globals to `static` locals for state that must survive a
      call.
- [ ] Block comments (`/* … */`), matching the bundled scripts.

**Structure**

- [ ] `int main(int argc, char** argv)`, returning `0` for success and non-zero
      only for a real failure.
- [ ] `argv[0]` may legitimately be `""` — handle "no title selected".
- [ ] Never hardcode `/3ds/…` or `sdmc:/switch/…`: build paths from `app_root()`.
- [ ] Never persist a catalog index between runs; resolve ids with `title_find`.

**Resources**

- [ ] `free()` every `char*` you keep, especially inside loops (64 KB heap).
- [ ] `delete_directory` for `read_directory`/`sav_list`; `json_delete` for
      `json_new` roots **only**; nothing for borrowed JSON elements.
- [ ] `sav_close` every handle you open (8 maximum), and `sav_commit` immediately
      after a successful `sav_write`.
- [ ] Check every negative return before using an out parameter; `sav_read` and
      `web_*` leave `*out` `NULL` on failure.
- [ ] Guard JSON getters with `json_is_*` / `json_object_contains` /
      `json_array_size` — a type mismatch ends the run.

**Behaviour**

- [ ] Confirm anything destructive with `gui_confirm` before doing it.
- [ ] `gui_status` + `progress_*` for anything that takes more than a moment; they
      never block.
- [ ] Assume an abort can land between any two statements: order the work so a
      half-run leaves the save usable.
- [ ] `script_log` the milestones (they reach the log file); `printf` the chatter.

**Before shipping**

- [ ] `tools/scriptlint.sh <file>` passes.
- [ ] Every menu branch was executed on hardware at least once — the linter cannot
      see unknown identifiers or wrong argument counts.
