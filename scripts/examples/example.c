/*
 * example.c — a guided tour of the Checkpoint scripting API.
 *
 * This script ships with no console build on purpose: `scripts/examples/` is
 * not copied into either romfs (the Makefiles pack `scripts/common/` plus
 * `scripts/<target>/` only). It exists to be read, and to be run by hand while
 * you read it. Copy it onto the SD card yourself:
 *
 *     3DS    /3ds/Checkpoint/scripts/universal/example.c
 *     Switch sdmc:/switch/Checkpoint/scripts/universal/example.c
 *
 * `universal/` means "offered for every title"; a folder named after a title id
 * (e.g. `0004000000055D00/`) offers the script for that title only.
 *
 * It is a menu: every entry demonstrates one area of the API, each area is one
 * function below, and nothing is destructive unless a confirm dialog says so in
 * as many words. Read top to bottom, or jump to the section you need.
 *
 *   1. Context      — argv, the selected title, the title catalog
 *   2. Output       — printf vs script_log
 *   3. Dialogs      — every blocking gui_* request
 *   4. Progress     — the non-blocking progress bars
 *   5. SD card      — app_root, mkdirs/exists, listings, plain stdio
 *   6. Zip          — zip_dir / unzip round trip
 *   7. Save data    — sav_* archive handles (read only, write behind a confirm)
 *   8. Shared data  — sav_open_shared (3DS: Play Coins)
 *   9. Network      — net_ip, web_get, web_request, url_encode
 *  10. JSON         — parsing and walking a document
 *  11. Abort        — how hold-B cancellation reaches your script
 *
 * ---------------------------------------------------------------------------
 * THE INTERPRETER
 *
 * Scripts are C, interpreted by picoc. It is C89-flavoured and small, so a few
 * habits from real C do not survive. The ones that actually bite:
 *
 *   - No pointer arithmetic on arrays. Index instead: `s[i]`, never `s + i`.
 *   - No adjacent string-literal concatenation. `"a" "b"` is a parse error;
 *     keep every literal whole, however long.
 *   - Array sizes must be literal (use a #define, not `n * 2`).
 *   - Declare a loop variable before the loop: `int i; for (i = 0; ...)`.
 *   - No reliable 64-bit integers. That is why title ids cross the API as
 *     16-character uppercase hex strings.
 *   - Functions must be defined before they are called, so main() goes last.
 *   - Block comments only; the bundled scripts avoid line comments throughout.
 *
 * Available headers: <checkpoint.h> (this API) plus picoc's stdlib —
 * <stdio.h>, <stdlib.h>, <string.h>, <unistd.h>, <ctype.h>, <math.h>, <time.h>.
 * printf/sprintf/fopen/malloc/strcmp all work as you expect.
 *
 * ---------------------------------------------------------------------------
 * MEMORY
 *
 * Every string an API call returns lives on a run-scoped heap: it is released
 * when the run ends, whatever the exit path (return, exit(), error, hold-B).
 * So a leak cannot outlive the script. Inside a long loop it still matters —
 * free() what you took, especially web response bodies and read buffers. free()
 * on an API-returned string is always safe.
 *
 * Two exceptions take a matching destructor rather than free():
 * `struct directory*` from read_directory/sav_list → delete_directory(), and
 * `struct JSON*` from json_new/json_parse → json_delete(). Note that
 * delete_directory frees the *listing*, it does not delete anything on disk.
 *
 * ---------------------------------------------------------------------------
 * EXIT
 *
 * main() returns 0 for success; any other value is reported to the user as a
 * failure, so return non-zero only when something really went wrong. exit(n)
 * from anywhere does the same. An open save archive is force-closed for you
 * after the run, but close your own handles anyway — this script does.
 */

#include <checkpoint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Buffer sizes as macros: picoc wants literal array bounds. */
#define PATHN    512
#define LINEN    256
#define MAX_PICK 64

/* Filled in by main() before any section runs. */
char g_root[PATHN];    /* app_root(): "/3ds/Checkpoint" or "sdmc:/switch/Checkpoint" */
char g_scratch[PATHN]; /* <root>/example_demo — everything this script writes */
int g_is_switch;       /* 1 on Switch, 0 on 3DS */

/* ------------------------------------------------------------------------- */
/* helpers                                                                    */
/* ------------------------------------------------------------------------- */

/* One line to the live console pane *and* the Checkpoint log file (which is
 * what a user attaches to a bug report). printf() reaches the pane only, so
 * use printf for chatter and script_log for anything worth keeping. */
void logline(char* msg)
{
    script_log(msg);
}

/* Recursively delete nothing — just this script's flat scratch files. Kept
 * deliberately dumb: remove() unlinks a file, rmdir() removes an empty dir. */
void scratch_clean(void)
{
    struct directory* dir;
    char path[PATHN];
    int i;

    if (!sd_exists(g_scratch)) {
        return;
    }
    dir = read_directory(g_scratch);
    if (dir != NULL) {
        for (i = 0; i < dir->count; i++) {
            sprintf(path, "%s/%s", g_scratch, dir->files[i]);
            /* files first; a nested dir would need its own pass, and this
             * script never makes one deeper than the unzip target. */
            if (remove(path) != 0) {
                rmdir(path);
            }
        }
        delete_directory(dir);
    }
    rmdir(g_scratch);
}

/* ------------------------------------------------------------------------- */
/* 1. Context: what the script knows about where it was launched from         */
/* ------------------------------------------------------------------------- */

/* Checkpoint calls main(argc, argv) with exactly one argument: argv[0] is the
 * id of the title highlighted when the user ran the script, as 16 uppercase hex
 * characters, or "" if nothing was selected. selected_title() returns the same
 * string, for code that is not inside main().
 *
 * Titles are otherwise addressed by *catalog index* — 0 .. titles_count()-1, the
 * same order as Checkpoint's own list. An index is only valid during this run;
 * never store one. To go from an id back to an index, use title_find(). */
void section_context(int argc, char** argv)
{
    char line[LINEN];
    char* id;
    char* name;
    char* code;
    char* path;
    int count;
    int idx;
    int shown;
    int i;

    count = titles_count();
    sprintf(line, "catalog holds %d titles", count);
    logline(line);

    if (argc > 0 && strlen(argv[0]) > 0) {
        idx = title_find(argv[0]);
        printf("launched on title %s -> catalog index %d\n", argv[0], idx);
        if (idx >= 0) {
            name = title_name(idx);
            printf("  name: %s\n", name);
            free(name);
        }
    }
    else {
        printf("launched with no title selected (argv[0] is empty)\n");
        idx = -1;
    }

    /* Everything you can ask about a title. Note that the strings are copies
     * you own; freeing them in a loop like this one is the point. */
    shown = 0;
    for (i = 0; i < count && shown < 5; i++) {
        id   = title_id(i);
        name = title_name(i);
        code = title_product_code(i);
        /* kind 0 = save backups, 1 = extdata backups; the path ends in '/' */
        path = title_backup_path(i, 0);

        printf("[%d] %s %s\n", i, id, name);
        printf("    code=%s cart=%d save=%d extdata=%d\n", code, title_is_cart(i), title_has_save(i), title_has_extdata(i));
        printf("    backups: %s\n", path);

        free(id);
        free(name);
        free(code);
        free(path);
        shown++;
    }
    if (count > shown) {
        printf("... %d more\n", count - shown);
    }

    printf("app root: %s\n", g_root);
    printf("this console: %s\n", g_is_switch ? "Switch" : "3DS");
}

/* ------------------------------------------------------------------------- */
/* 2. Output: the two places text can go                                      */
/* ------------------------------------------------------------------------- */

void section_output(void)
{
    char line[LINEN];
    int i;

    /* The console pane is live: it scrolls as the script runs, and the user can
     * scroll it themselves. Nothing here blocks. */
    printf("printf goes to the script console only.\n");
    logline("script_log goes to the console AND the Checkpoint log file.");

    /* gui_status sets the one-line header above the pane — a state, not a line
     * of history. Overwrite it as the script moves between phases. */
    gui_status("Phase 1 of 2");
    sleep(1);
    gui_status("Phase 2 of 2");
    sleep(1);
    gui_status("Output demo");

    for (i = 0; i < 3; i++) {
        sprintf(line, "formatted line %d of 3", i + 1);
        printf("%s\n", line);
    }

    /* No stdout flushing games are needed: each line reaches the pane as it is
     * written, even in a tight loop. */
}

/* ------------------------------------------------------------------------- */
/* 3. Dialogs: every gui_* call blocks the script until the user answers       */
/* ------------------------------------------------------------------------- */

void section_dialogs(void)
{
    char* items[4];
    int selected[4];
    char typed[64];
    char line[LINEN];
    int picked;
    int confirmed;
    int number;
    int i;

    gui_message("gui_message: one OK button. Use it for results and dead ends.");

    if (!gui_confirm("gui_confirm: yes/no. Continue the dialog tour?")) {
        printf("user declined at gui_confirm\n");
        return;
    }

    items[0] = "Alpha";
    items[1] = "Beta";
    items[2] = "Gamma";
    items[3] = "Delta";

    /* gui_pick_one returns the index picked, or -1 if the user backed out.
     * Always handle -1: it is the normal way to cancel. */
    picked = gui_pick_one("gui_pick_one: choose a row (B cancels)", items, 4);
    if (picked < 0) {
        printf("gui_pick_one: cancelled\n");
    }
    else {
        printf("gui_pick_one: %d (%s)\n", picked, items[picked]);
    }

    /* gui_pick_many takes an int[] of the same length: 1 = preselected on the
     * way in, and on the way out it holds the user's answer. The return value
     * is 1 when confirmed, 0 when cancelled (the array is untouched then). */
    selected[0] = 1;
    selected[1] = 0;
    selected[2] = 1;
    selected[3] = 0;
    confirmed   = gui_pick_many("gui_pick_many: Alpha and Gamma start ticked", items, 4, selected);
    printf("gui_pick_many: confirmed=%d\n", confirmed);
    if (confirmed) {
        for (i = 0; i < 4; i++) {
            if (selected[i]) {
                printf("  chosen: %s\n", items[i]);
            }
        }
    }

    /* gui_keyboard writes into a buffer you own; the third argument is that
     * buffer's size including the terminator. An empty result means the user
     * cancelled or typed nothing — you cannot tell the two apart. */
    typed[0] = '\0';
    gui_keyboard(typed, "gui_keyboard: type something", 64);
    printf("gui_keyboard: '%s'\n", typed);

    /* gui_numpad enforces [min, max] in the keyboard itself, so there is no
     * clamping to do. It returns -1 when cancelled — keep min >= 0 so that
     * sentinel stays unambiguous. */
    number = gui_numpad("gui_numpad: pick 1..100", 1, 100);
    if (number < 0) {
        printf("gui_numpad: cancelled\n");
    }
    else {
        sprintf(line, "gui_numpad returned %d", number);
        printf("%s\n", line);
    }
}

/* ------------------------------------------------------------------------- */
/* 4. Progress: the one-way half of the script UI                             */
/* ------------------------------------------------------------------------- */

/* Progress calls never block and never wait for a frame, so a copy loop can
 * report every single chunk. Bars nest: layer 0 is the outermost, up to 3
 * layers. progress_begin(layer, ...) resets that layer and discards every
 * deeper one, so starting the next outer item cannot leave a stale inner bar
 * behind. A total <= 0 renders as an indeterminate bar showing a raw count. */
void section_progress(void)
{
    char label[LINEN];
    int outer;
    int inner;

    gui_status("Progress demo");

    progress_begin(0, "Overall", 3);
    for (outer = 0; outer < 3; outer++) {
        sprintf(label, "Item %d of 3", outer + 1);
        progress_label(0, label);

        progress_begin(1, "Steps", 4);
        for (inner = 0; inner < 4; inner++) {
            /* progress_note drives the innermost row: the stage you are in
             * right now. Long native calls (zip_dir, unzip, web_upload_file)
             * take that row over themselves while they run. */
            sprintf(label, "step %d", inner + 1);
            progress_note(label);
            usleep(200000);
            progress_set(1, inner + 1);
        }
        progress_end(1);

        progress_set(0, outer + 1);
    }

    /* An unknown total: indeterminate bar, raw count. */
    progress_begin(0, "Unknown length", 0);
    for (outer = 0; outer < 20; outer++) {
        progress_set(0, outer + 1);
        usleep(50000);
    }
    progress_end(0);

    /* progress_clear tears every layer down at once — the right thing to do
     * before a dialog, or on an error path. */
    progress_clear();
    gui_message("Progress demo finished.");
}

/* ------------------------------------------------------------------------- */
/* 5. SD card: paths, directories and plain stdio                             */
/* ------------------------------------------------------------------------- */

/* Build every path from app_root(). On 3DS it is "/3ds/Checkpoint"; on Switch
 * "sdmc:/switch/Checkpoint" — each already in the form that console's fopen and
 * stat want, so a script that concatenates onto it is cross-platform for free.
 * Never hardcode "/3ds/..." unless the script is genuinely 3DS-only. */
void section_sdcard(void)
{
    struct directory* dir;
    char path[PATHN];
    char buf[LINEN];
    FILE* f;
    int n;
    int i;

    gui_status("SD card demo");

    /* mkdir -p. Returns 0 when the directory exists afterwards. */
    if (sd_mkdirs(g_scratch) != 0) {
        gui_message("sd_mkdirs failed.");
        return;
    }
    printf("sd_mkdirs %s: ok\n", g_scratch);
    printf("sd_exists: %d\n", sd_exists(g_scratch));

    /* picoc's stdio works on real paths — this is how you read and write your
     * own config, cache or export files. */
    sprintf(path, "%s/hello.txt", g_scratch);
    f = fopen(path, "w");
    if (f == NULL) {
        gui_message("fopen for write failed.");
        return;
    }
    strcpy(buf, "written by example.c\n");
    fwrite(buf, 1, strlen(buf), f);
    fclose(f);

    /* A second file, so the zip section below has something to pack. */
    sprintf(path, "%s/second.txt", g_scratch);
    f = fopen(path, "w");
    if (f != NULL) {
        strcpy(buf, "a second file\n");
        fwrite(buf, 1, strlen(buf), f);
        fclose(f);
    }

    sprintf(path, "%s/hello.txt", g_scratch);
    f = fopen(path, "r");
    if (f != NULL) {
        n = fread(buf, 1, LINEN - 1, f);
        buf[n] = '\0';
        fclose(f);
        printf("read back %d bytes: %s", n, buf);
    }

    /* read_directory returns names only (not full paths) — join them yourself.
     * Hand the struct back with delete_directory when you are done. */
    dir = read_directory(g_scratch);
    if (dir != NULL) {
        printf("read_directory %s: %d entries\n", g_scratch, dir->count);
        for (i = 0; i < dir->count; i++) {
            printf("  %s\n", dir->files[i]);
        }
        delete_directory(dir);
    }

    /* Where backups live, for a script that wants to walk them. */
    if (titles_count() > 0) {
        char* backups = title_backup_path(0, 0);
        printf("backups of title 0: %s (exists=%d)\n", backups, sd_exists(backups));
        free(backups);
    }
}

/* ------------------------------------------------------------------------- */
/* 6. Zip: store-only archives, straight to and from SD                       */
/* ------------------------------------------------------------------------- */

/* zip_dir packs a directory tree into one file; unzip is the inverse. Both use
 * the same framing (CRC + path safety) as Checkpoint's wireless transfer, and
 * both stream through stdio — the bytes never enter the interpreter heap, so a
 * multi-megabyte save backup is fine. Return 0 on success, negative on error;
 * -2 specifically means the user cancelled with hold-B. Both drive the
 * innermost progress bar while they run. */
void section_zip(void)
{
    struct directory* dir;
    char zipPath[PATHN];
    char outDir[PATHN];
    int res;
    int i;

    gui_status("Zip demo");

    if (!sd_exists(g_scratch)) {
        gui_message("Run the SD card section first — it creates the files to zip.");
        return;
    }

    sprintf(zipPath, "%s/demo.zip", g_scratch);
    sprintf(outDir, "%s/unpacked", g_scratch);

    progress_note("zipping");
    res = zip_dir(g_scratch, zipPath);
    printf("zip_dir: %d\n", res);
    if (res != 0) {
        progress_clear();
        gui_message(res == -2 ? "Zip cancelled." : "zip_dir failed, see the log.");
        return;
    }

    if (sd_mkdirs(outDir) != 0) {
        progress_clear();
        gui_message("Could not create the unzip target.");
        return;
    }

    progress_note("unzipping");
    res = unzip(zipPath, outDir);
    printf("unzip: %d\n", res);
    progress_clear();
    if (res != 0) {
        gui_message(res == -2 ? "Unzip cancelled." : "unzip failed, see the log.");
        return;
    }

    dir = read_directory(outDir);
    if (dir != NULL) {
        printf("unpacked %d entries:\n", dir->count);
        for (i = 0; i < dir->count; i++) {
            printf("  %s\n", dir->files[i]);
        }
        delete_directory(dir);
    }
    gui_message("Zip round trip finished.");

    /* Tidy up the archive; the scratch files stay for the other sections. */
    remove(zipPath);
}

/* ------------------------------------------------------------------------- */
/* 7. Save data: file-level access to a title's save archive                  */
/* ------------------------------------------------------------------------- */

/* sav_open(titleIdx, kind) — kind 0 = save, 1 = extdata — returns a handle, or:
 *   -1  the title/kind has no archive a script can mount (GBA VC, DSiWare, SPI
 *       cart saves: Checkpoint reaches those through other paths),
 *   -2  all 8 handle slots are in use,
 *   <0  otherwise a raw negative FS Result.
 *
 * Paths inside a handle are archive-absolute: "/file.bin", not an SD path.
 * sav_read allocates the out buffer (NUL-terminated as a convenience) and
 * leaves it NULL on failure — never a partial read: -3 is out of memory and -4
 * a short read. Writes are not visible to the game until sav_commit, which also
 * clears the title's secure value exactly as a restore does (a no-op on
 * extdata). Always sav_close. */
void section_savedata(void)
{
    char* names[MAX_PICK];
    int idxs[MAX_PICK];
    char line[LINEN];
    char* data;
    char* payload;
    struct directory* dir;
    int count;
    int n;
    int picked;
    int titleIdx;
    int h;
    int size;
    int res;
    int i;

    gui_status("Save data demo");

    count = titles_count();
    n     = 0;
    for (i = 0; i < count && n < MAX_PICK; i++) {
        if (title_has_save(i)) {
            idxs[n]  = i;
            names[n] = title_name(i);
            n++;
        }
    }
    if (n == 0) {
        gui_message("No title on this console exposes a save archive.");
        return;
    }

    picked = gui_pick_one("Open which title's save archive?", names, n);
    if (picked < 0) {
        for (i = 0; i < n; i++) {
            free(names[i]);
        }
        return;
    }
    titleIdx = idxs[picked];

    h = sav_open(titleIdx, 0);
    for (i = 0; i < n; i++) {
        free(names[i]);
    }
    if (h < 0) {
        sprintf(line, "sav_open failed: %d", h);
        logline(line);
        gui_message("Could not open that save archive (see the log for the code).");
        return;
    }
    printf("sav_open: handle %d\n", h);

    /* sav_list returns full in-archive paths; folders carry a trailing '/'.
     * It returns NULL on error, and delete_directory accepts NULL. */
    dir = sav_list(h, "/");
    if (dir == NULL) {
        gui_message("sav_list failed.");
        sav_close(h);
        return;
    }
    printf("archive root: %d entries\n", dir->count);
    for (i = 0; i < dir->count; i++) {
        printf("  %s\n", dir->files[i]);
    }

    if (dir->count > 0) {
        picked = gui_pick_one("Read which entry? (folders fail, on purpose)", dir->files, dir->count);
        if (picked >= 0) {
            res = sav_read(h, dir->files[picked], &data, &size);
            if (res == 0) {
                sprintf(line, "read %s: %d bytes", dir->files[picked], size);
                printf("%s\n", line);
                /* First bytes as hex — index only, no pointer arithmetic. */
                printf("  first bytes:");
                for (i = 0; i < size && i < 16; i++) {
                    printf(" %02X", data[i] & 0xFF);
                }
                printf("\n");
                gui_message(line);
                free(data);
            }
            else {
                sprintf(line, "sav_read failed: %d (a folder, or an FS error)", res);
                printf("%s\n", line);
                gui_message("Read failed — see the log.");
            }
        }
    }
    delete_directory(dir);

    /* The write path, gated. Everything below is what a real save editor does:
     * write, commit, read back, verify, then undo. Keep it behind an explicit
     * confirm — an interrupted commit is how saves get corrupted. */
    if (gui_confirm("Run the WRITE test? It creates and deletes /ckpt_example.bin in this archive and commits twice. Accept only on a title you can afford to lose.")) {
        payload = "checkpoint example payload";
        res     = sav_write(h, "/ckpt_example.bin", payload, strlen(payload));
        printf("sav_write: %d\n", res);
        if (res == 0) {
            res = sav_commit(h);
            printf("sav_commit: %d\n", res);
        }
        if (res == 0) {
            res = sav_read(h, "/ckpt_example.bin", &data, &size);
            if (res == 0 && size == (int)strlen(payload) && strcmp(data, payload) == 0) {
                printf("read back: identical\n");
                free(data);
            }
            else {
                printf("read back: MISMATCH res=%d size=%d\n", res, size);
                res = -1;
            }
        }
        /* Undo, whatever happened above. */
        sav_delete(h, "/ckpt_example.bin");
        sav_commit(h);
        gui_message(res == 0 ? "Write round trip OK; the test file was removed." : "Write test failed — see the log.");
    }

    sav_close(h);
}

/* ------------------------------------------------------------------------- */
/* 8. Shared archives: data no title owns                                     */
/* ------------------------------------------------------------------------- */

/* sav_open_shared takes an id instead of a catalog index, because the archive
 * belongs to the console rather than to a title. The id is a 16-hex string
 * whose low 32 bits are the extdata id and whose high 32 bits are the archive
 * magic. The handle then works with every sav_* call above (commit is a no-op).
 *
 * The example is the 3DS Home Menu's shared extdata, which holds Play Coins.
 * This section only reads. scripts/3ds/universal/playcoins.c writes it. */
#define GAMECOIN_EXTDATA "00048000F000000B"
#define GAMECOIN_FILE    "/gamecoin.dat"
#define COIN_OFFSET      4

void section_shared(void)
{
    char line[LINEN];
    char* data;
    int size;
    int res;
    int h;
    int coins;

    if (g_is_switch) {
        gui_message("Shared extdata is a 3DS concept — nothing to show on Switch.");
        return;
    }

    h = sav_open_shared(GAMECOIN_EXTDATA);
    if (h < 0) {
        sprintf(line, "sav_open_shared failed: %d", h);
        logline(line);
        gui_message("Could not open the Home Menu shared extdata.");
        return;
    }

    res = sav_read(h, GAMECOIN_FILE, &data, &size);
    if (res != 0 || size < COIN_OFFSET + 2) {
        sav_close(h);
        gui_message("gamecoin.dat could not be read.");
        return;
    }

    /* little-endian u16 at offset 4 */
    coins = (data[COIN_OFFSET] & 0xFF) | ((data[COIN_OFFSET + 1] & 0xFF) << 8);
    free(data);
    sav_close(h);

    sprintf(line, "gamecoin.dat is %d bytes; you have %d Play Coins.", size, coins);
    printf("%s\n", line);
    gui_message(line);
}

/* ------------------------------------------------------------------------- */
/* 9. Network                                                                 */
/* ------------------------------------------------------------------------- */

/* web_get is the one-liner: it returns the HTTP status, or negative on failure
 * (-1 no HTTP available, -2 the response did not fit in memory, -(CURLcode+100)
 * for a transfer error). -2 is the one worth retrying with a smaller request.
 *
 * web_request is the general form — method, headers, request body, and the raw
 * response headers back. web_upload_file is web_request with the body streamed
 * from a file on SD instead of built in the interpreter heap, which is what
 * makes multi-megabyte uploads possible; it drives the innermost progress bar
 * itself and honours hold-B. See scripts/common/universal/googledrive.c for all
 * three used in anger.
 *
 * Response buffers are yours to free(). net_ip() answers "0.0.0.0" with no
 * network, which is the cheapest way to check for connectivity. */
void section_network(void)
{
    char line[LINEN];
    char* ip;
    char* body;
    char* headers;
    char* ctype;
    char* encoded;
    int size;
    int status;

    gui_status("Network demo");

    ip = net_ip();
    printf("net_ip: %s\n", ip);
    if (strcmp(ip, "0.0.0.0") == 0) {
        free(ip);
        gui_message("No network connection — skipping the HTTP calls.");
        return;
    }
    free(ip);

    /* url_encode: percent-encoding for query strings and form bodies. */
    encoded = url_encode("a b&c=d/e");
    printf("url_encode(\"a b&c=d/e\") = %s\n", encoded);
    free(encoded);

    if (!gui_confirm("Fetch http://example.com to demonstrate web_get and web_request?")) {
        return;
    }

    progress_note("web_get");
    status = web_get(&body, &size, "http://example.com");
    printf("web_get: status=%d size=%d\n", status, size);
    if (status == 200) {
        free(body);
    }

    /* The same fetch through web_request, with a header of our own, and then
     * one header read back out of the raw response block. Headers are
     * "\n"-separated "Key: Value" lines; "" means none. The body is "" / 0 for
     * a request that has none. Pass a valid char** for the response headers
     * even if you ignore them. */
    progress_note("web_request");
    status = web_request("GET", "http://example.com", "User-Agent: Checkpoint-example/1.0", "", 0, &body, &size, &headers);
    printf("web_request: status=%d size=%d\n", status, size);
    if (status >= 0) {
        ctype = http_header_value(headers, "Content-Type");
        sprintf(line, "HTTP %d, %d bytes, Content-Type: %s", status, size, ctype);
        printf("%s\n", line);
        gui_message(line);
        free(ctype);
        free(headers);
        free(body);
    }
    else {
        sprintf(line, "web_request failed: %d", status);
        logline(line);
        gui_message("Request failed — see the log.");
    }
    progress_clear();
}

/* ------------------------------------------------------------------------- */
/* 10. JSON                                                                   */
/* ------------------------------------------------------------------------- */

/* struct JSON* is an opaque handle over one node. json_new gives you an empty
 * one, json_parse fills it from text, json_delete frees the tree — child nodes
 * from array_element/object_element belong to that tree and must NOT be deleted
 * individually. json_get_string hands back a copy you free().
 *
 * The important discipline: check the type before you read it. Calling
 * json_get_int on a string aborts the whole script, so the guards below
 * (is_valid → is_object → object_contains → is_<type>) are not optional
 * politeness, they are how a malformed server reply stays survivable. */
void section_json(void)
{
    /* One literal: picoc does not concatenate adjacent string literals. */
    char* doc = "{\"name\":\"Checkpoint\",\"version\":5,\"beta\":true,\"tags\":[\"saves\",\"scripts\"],\"author\":{\"handle\":\"FlagBrew\"}}";
    struct JSON* root;
    struct JSON* node;
    struct JSON* child;
    char* s;
    int i;

    root = json_new();
    json_parse(root, doc);

    if (!json_is_valid(root) || !json_is_object(root)) {
        json_delete(root);
        gui_message("json_parse rejected the document.");
        return;
    }

    /* strings */
    if (json_object_contains(root, "name")) {
        node = json_object_element(root, "name");
        if (json_is_string(node)) {
            s = json_get_string(node);
            printf("name: %s\n", s);
            free(s);
        }
    }

    /* numbers and booleans */
    node = json_object_element(root, "version");
    if (json_is_int(node)) {
        printf("version: %d\n", json_get_int(node));
    }
    node = json_object_element(root, "beta");
    if (json_is_bool(node)) {
        printf("beta: %d\n", json_get_bool(node));
    }

    /* arrays */
    node = json_object_element(root, "tags");
    if (json_is_array(node)) {
        printf("tags (%d):\n", json_array_size(node));
        for (i = 0; i < json_array_size(node); i++) {
            child = json_array_element(node, i);
            if (json_is_string(child)) {
                s = json_get_string(child);
                printf("  %s\n", s);
                free(s);
            }
        }
    }

    /* nested objects, and walking keys by index when you do not know them */
    node = json_object_element(root, "author");
    if (json_is_object(node)) {
        s = json_object_key(node, 0);
        printf("author key 0: %s\n", s);
        child = json_object_element(node, s);
        free(s);
        if (json_is_string(child)) {
            s = json_get_string(child);
            printf("author: %s\n", s);
            free(s);
        }
    }

    /* One delete for the whole tree. */
    json_delete(root);
    gui_message("JSON walk finished — see the console pane.");
}

/* ------------------------------------------------------------------------- */
/* 11. Abort: how a script gets cancelled                                     */
/* ------------------------------------------------------------------------- */

/* Holding B while a script runs kills it: the interpreter fails the run at the
 * next statement, and long native calls (web_get, web_upload_file, zip_dir,
 * unzip) poll the same flag and bail out early — zip_dir and unzip report it as
 * -2. Your script does not poll anything and cannot veto it.
 *
 * What that means for you: an abort can land between any two statements, so
 * leave the console in a state that survives it. Do the risky write as late as
 * possible, keep sav_commit close behind sav_write, and remember that the run
 * still gets its heap and its open archive handles cleaned up afterwards. */
void section_abort(void)
{
    int i;

    if (!gui_confirm("Start a 15 second busy loop? Hold B during it to see cancellation.")) {
        return;
    }

    gui_status("Hold B to cancel");
    progress_begin(0, "Sleeping", 15);
    for (i = 0; i < 15; i++) {
        printf("tick %d\n", i + 1);
        sleep(1);
        progress_set(0, i + 1);
    }
    progress_end(0);
    gui_message("Loop finished without being cancelled.");
}

/* ------------------------------------------------------------------------- */
/* main                                                                       */
/* ------------------------------------------------------------------------- */

int main(int argc, char** argv)
{
    char* root;
    char* items[12];
    int pick;

    logline("example: started");

    /* Resolve the console's app root once; every path this script writes hangs
     * off it, which is all it takes to be cross-platform. */
    root = app_root();
    strncpy(g_root, root, PATHN - 1);
    g_root[PATHN - 1] = '\0';
    free(root);

    g_is_switch = (strncmp(g_root, "sdmc:", 5) == 0);
    sprintf(g_scratch, "%s/example_demo", g_root);

    items[0]  = "1. Context: title catalog and argv";
    items[1]  = "2. Output: printf, script_log, status";
    items[2]  = "3. Dialogs: every gui_* request";
    items[3]  = "4. Progress bars";
    items[4]  = "5. SD card and stdio";
    items[5]  = "6. Zip round trip";
    items[6]  = "7. Save archives (sav_*)";
    items[7]  = "8. Shared extdata (3DS Play Coins)";
    items[8]  = "9. Network";
    items[9]  = "10. JSON";
    items[10] = "11. Abort with hold-B";
    items[11] = "Clean up and exit";

    /* A menu loop is the shape most non-trivial scripts want: one place that
     * decides what to do, and a function per job. gui_pick_one returning -1
     * (the user pressed B) is treated exactly like the explicit exit row. */
    pick = 0;
    while (pick >= 0 && pick < 11) {
        gui_status("Checkpoint scripting example");
        pick = gui_pick_one("Pick a demo", items, 12);

        if (pick == 0) {
            section_context(argc, argv);
        }
        else if (pick == 1) {
            section_output();
        }
        else if (pick == 2) {
            section_dialogs();
        }
        else if (pick == 3) {
            section_progress();
        }
        else if (pick == 4) {
            section_sdcard();
        }
        else if (pick == 5) {
            section_zip();
        }
        else if (pick == 6) {
            section_savedata();
        }
        else if (pick == 7) {
            section_shared();
        }
        else if (pick == 8) {
            section_network();
        }
        else if (pick == 9) {
            section_json();
        }
        else if (pick == 10) {
            section_abort();
        }
    }

    /* Leave nothing behind: this script's scratch directory is the only thing
     * it created outside a save archive. */
    scratch_clean();
    progress_clear();

    logline("example: finished");
    /* 0 = success. Anything else is shown to the user as a failed run. */
    return 0;
}
