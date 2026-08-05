/*
 * browser.c — file browser for the SD card and for save archives
 *             (Checkpoint script, both consoles)
 */

#include <checkpoint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* buffer sizes (macros, not expressions, so picoc's array bounds stay literal) */
#define PATHN    512  /* a full path                                          */
#define NAMEN    128  /* one path component, or a mount's label               */
#define MSGN     512  /* a dialog string                                      */
#define SIZEN    32   /* "12.34 MB"                                           */
#define MAXENT   400  /* entries listed per folder                            */
#define MAXTITLE 300  /* rows in the title picker                             */
#define MAXOPT   16   /* rows in a menu                                       */
#define MAXDEPTH 12   /* recursion cap for the tree walkers                   */
#define CHUNK    16384    /* SD -> SD copy block                              */
#define COPY_MAX 16777216 /* biggest file an archive copy may hold in RAM     */

/* the two kinds of place a side can be mounted on */
#define DOM_SD  0
#define DOM_SAV 1

#define SIDE_SRC 0
#define SIDE_DST 1

/* menu codes */
#define OP_OPEN   1
#define OP_COPY   2
#define OP_MOVE   3
#define OP_RENAME 4
#define OP_DELETE 5
#define OP_PROPS  6
#define OP_ZIP    7
#define OP_UNZIP  8
#define OP_BACK   9
#define OP_ITEM   10
#define OP_MULTI  11
#define OP_MKDIR  12
#define OP_SETDST 13
#define OP_GODST  14
#define OP_MOUNT  15

/* the browsed side */
int g_sdom;            /* DOM_SD / DOM_SAV                                    */
int g_sh;              /* archive handle, -1 on SD                            */
int g_stitle;          /* catalog index behind the handle, -1 on SD           */
int g_skind;           /* 0 save, 1 extdata                                   */
char g_sroot[PATHN];   /* the mount point: ".." never goes above it           */
char g_spath[PATHN];   /* the folder on show                                  */
char g_slabel[NAMEN];  /* what to call this mount in dialogs                  */

/* the destination side */
int g_dset;
int g_ddom;
int g_dh;
int g_dtitle;
int g_dkind;
char g_droot[PATHN];
char g_dpath[PATHN];
char g_dlabel[NAMEN];

/* the current folder's listing, rebuilt on every pass of the browse loop */
char* g_ename[MAXENT]; /* display name, folders keep a trailing '/'           */
char* g_epath[MAXENT]; /* full path, never a trailing '/'                     */
int g_eisdir[MAXENT];
int g_esel[MAXENT];    /* gui_pick_many's in/out array                        */
int g_ecount;
int g_etrunc;          /* the folder had more entries than MAXENT             */

/* the rows handed to gui_pick_one: global because a 400-pointer array is a lot
 * of frame for picoc's 64 KB interpreter stack, while a global lives on the
 * ordinary heap */
char* g_rows[MAXENT + 3];

/* what the recursive walkers report through */
int g_walk_files;
int g_walk_dirs;
int g_walk_bytes;
int g_done;
int g_skipped;
int g_layer; /* progress layer the walkers count on: 0 alone, 1 inside a batch */

/* the SD card's root in this platform's spelling, and the launching title */
char g_sdroot[PATHN];
int g_launch;
int g_launch_kind; /* 0 save, 1 extdata: what the launching title has          */

/* Which console this is, told apart the documented way (app_root()'s prefix).
 * Extdata is a 3DS concept: every row and every message about it is drawn only
 * where the archive exists, rather than offered and then refused. */
int g_is_switch;

/* Did the user back out of the location picker itself? It is the difference
 * between "leave the script" and "that mount did not open, pick another". */
int g_pickcancel;

/* ---- strings and paths --------------------------------------------------- */

void str_copy(char* dst, char* src, int size)
{
    int i = 0;
    while (src[i] != '\0' && i < size - 1) {
        dst[i] = src[i];
        i      = i + 1;
    }
    dst[i] = '\0';
}

/* The last component of `full`, tolerating the trailing '/' sav_list puts on a
 * folder, and "" for a filesystem root — "/" and "sdmc:/" name no thing that
 * could be copied or renamed, and callers test for the empty name to say so.
 * Index-only: picoc has no pointer arithmetic. */
void basename_of(char* full, char* dst, int size)
{
    int end = strlen(full);
    if (end > 0 && full[end - 1] == '/') {
        end = end - 1;
    }

    int start = 0;
    int i     = 0;
    while (i < end) {
        if (full[i] == '/') {
            start = i + 1;
        }
        i = i + 1;
    }

    if (end > 0 && full[end - 1] == ':') {
        end = start; /* a device root: "sdmc:/" -> "" like "/" already is */
    }

    int j = 0;
    while (start + j < end && j < size - 1) {
        dst[j] = full[start + j];
        j      = j + 1;
    }
    dst[j] = '\0';
}

int ends_with_ci(char* s, char* suffix)
{
    int n = strlen(s);
    int m = strlen(suffix);
    if (m == 0 || m > n) {
        return 0;
    }

    int i = 0;
    while (i < m) {
        if (tolower(s[n - m + i]) != tolower(suffix[i])) {
            return 0;
        }
        i = i + 1;
    }
    return 1;
}

/* base + name, without ever producing "//": the FS rejects the empty component
 * when something later walks the path, and both roots that end in '/' ("/" on
 * 3DS, "sdmc:/" on Switch) would otherwise make one. */
void path_join(char* base, char* name, char* out, int size)
{
    int n = strlen(base);
    if (n > 0 && base[n - 1] == '/') {
        snprintf(out, size, "%s%s", base, name);
    }
    else {
        snprintf(out, size, "%s/%s", base, name);
    }
}

/* The folder holding `path`. Roots keep their slash: "/x" -> "/" and
 * "sdmc:/x" -> "sdmc:/", both of which are real paths, while "sdmc:" is not. */
void path_parent(char* path, char* out, int size)
{
    str_copy(out, path, size);
    int n = strlen(out);

    /* a root is its own parent, and losing its slash would make it a device
     * name ("sdmc:") that opens nothing */
    if (n == 0 || strcmp(out, "/") == 0 || (n > 1 && out[n - 1] == '/' && out[n - 2] == ':')) {
        return;
    }

    if (n > 1 && out[n - 1] == '/') {
        n      = n - 1;
        out[n] = '\0';
    }

    int cut = -1;
    int i   = 0;
    while (i < n) {
        if (out[i] == '/') {
            cut = i;
        }
        i = i + 1;
    }

    if (cut < 0) {
        return; /* no separator to cut at: leave it alone */
    }
    if (cut == 0) {
        out[1] = '\0';
        return;
    }
    if (out[cut - 1] == ':') {
        out[cut + 1] = '\0';
        return;
    }
    out[cut] = '\0';
}

/* Is `child` `parent` itself, or something under it? Copying a folder into its
 * own subtree would recurse until the card filled up. */
int is_within(char* parent, char* child)
{
    int n = strlen(parent);
    if (n > 1 && parent[n - 1] == '/') {
        n = n - 1;
    }
    if (n == 1 && parent[0] == '/') {
        return child[0] == '/'; /* the card's root holds every path on it */
    }
    if (strncmp(parent, child, n) != 0) {
        return 0;
    }
    return child[n] == '\0' || child[n] == '/';
}

void fmt_size(int bytes, char* out, int size)
{
    if (bytes < 1024) {
        snprintf(out, size, "%d bytes", bytes);
    }
    else if (bytes < 1024 * 1024) {
        snprintf(out, size, "%.2f KB", bytes / 1024.0);
    }
    else {
        snprintf(out, size, "%.2f MB", bytes / (1024.0 * 1024.0));
    }
}

/* ---- the two domains, behind one set of calls ---------------------------- */

/* read_directory says nothing about which entries are folders, so this is the
 * documented test: an entry that will not open as a file is one. Only for paths
 * that came out of a listing, which therefore exist.
 *
 * Both consoles refuse to open a folder as a file, which is the first branch
 * and costs a folder nothing beyond the failed open. The read is for the
 * libraries that do open one (glibc, and so the host test harness): the open
 * succeeds and the first read fails instead. */
int sd_is_listed_dir(char* path)
{
    char probe[1];

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return 1;
    }

    fread(probe, 1, 1, f);
    int isdir = ferror(f) != 0;
    fclose(f);
    return isdir;
}

int sd_is_dir(char* path)
{
    if (!sd_exists(path)) {
        return 0;
    }
    return sd_is_listed_dir(path);
}

struct directory* dom_list(int dom, int h, char* path)
{
    if (dom == DOM_SD) {
        return read_directory(path);
    }
    return sav_list(h, path);
}

int dom_is_dir(int dom, int h, char* path)
{
    if (dom == DOM_SD) {
        return sd_is_dir(path);
    }

    /* an archive lists only directories, so a listing that comes back is one */
    struct directory* d = sav_list(h, path);
    if (d == NULL) {
        return 0;
    }
    delete_directory(d);
    return 1;
}

/* Folder or file, for one entry of a listing. sav_list marks folders with a
 * trailing '/'; on SD the rebuilt path is probed, because read_directory's own
 * entry can carry a "//" the FS will not open. */
int entry_is_dir(int dom, int h, char* raw, char* rebuilt)
{
    if (dom == DOM_SAV) {
        int n = strlen(raw);
        return n > 0 && raw[n - 1] == '/';
    }
    return sd_is_listed_dir(rebuilt);
}

/* Is there anything at `path`? There is no stat inside an archive, so a file is
 * looked for in its own folder's listing. */
int dom_exists(int dom, int h, char* path)
{
    if (dom == DOM_SD) {
        return sd_exists(path);
    }
    if (dom_is_dir(dom, h, path)) {
        return 1;
    }

    char parent[PATHN];
    char name[NAMEN];
    path_parent(path, parent, PATHN);
    basename_of(path, name, NAMEN);

    struct directory* d = sav_list(h, parent);
    if (d == NULL) {
        return 0;
    }

    int found = 0;
    int i     = 0;
    while (i < d->count) {
        char entry[NAMEN];
        basename_of(d->files[i], entry, NAMEN);
        if (strcmp(entry, name) == 0) {
            found = 1;
        }
        i = i + 1;
    }
    delete_directory(d);
    return found;
}

int dom_mkdir(int dom, int h, char* path)
{
    if (dom == DOM_SD) {
        return sd_mkdirs(path);
    }
    return sav_mkdir(h, path);
}

int dom_del_file(int dom, int h, char* path)
{
    if (dom == DOM_SD) {
        return remove(path) == 0 ? 0 : -1;
    }
    return sav_delete(h, path);
}

int dom_del_dir(int dom, int h, char* path)
{
    if (dom == DOM_SD) {
        return rmdir(path) == 0 ? 0 : -1;
    }
    return sav_rmdir(h, path);
}

int dom_rename(int dom, int h, char* from, char* to)
{
    if (dom == DOM_SD) {
        return rename(from, to) == 0 ? 0 : -1;
    }
    return sav_rename(h, from, to);
}

/* What makes a write to an archive real. A no-op on the SD card and on
 * extdata, so operations can call it unconditionally when they finish. */
int dom_commit(int dom, int h)
{
    if (dom == DOM_SAV) {
        return sav_commit(h);
    }
    return 0;
}

/* Size in bytes, -1 if it cannot be had. Inside an archive there is no stat, so
 * the file is read and dropped — cheap for a save, and the reason folder
 * properties there count entries instead of totalling bytes. */
int dom_file_size(int dom, int h, char* path)
{
    if (dom == DOM_SD) {
        FILE* f = fopen(path, "rb");
        if (f == NULL) {
            return -1;
        }
        fseek(f, 0, SEEK_END);
        int n = ftell(f);
        fclose(f);
        return n;
    }

    char* data;
    int size;
    if (sav_read(h, path, &data, &size) != 0) {
        return -1;
    }
    free(data);
    return size;
}

/* ---- copying, counting, deleting ----------------------------------------- */

/* One file, in any direction. 0 on success; -1 source, -2 destination,
 * -3 memory, -4 short write, -5 too big to pass through RAM, or the archive's
 * own negative result.
 *
 * SD -> SD streams in CHUNK blocks. Anything involving an archive goes through
 * one buffer holding the whole file, because sav_read and sav_write have no
 * offset — hence COPY_MAX. */
int copy_file(int sdom, int sh, char* spath, int ddom, int dh, char* dpath)
{
    if (sdom == DOM_SD && ddom == DOM_SD) {
        FILE* in = fopen(spath, "rb");
        if (in == NULL) {
            return -1;
        }
        FILE* out = fopen(dpath, "wb");
        if (out == NULL) {
            fclose(in);
            return -2;
        }
        char* buf = (char*)malloc(CHUNK);
        if (buf == NULL) {
            fclose(in);
            fclose(out);
            return -3;
        }

        int res = 0;
        int n   = 1;
        while (n > 0 && res == 0) {
            n = fread(buf, 1, CHUNK, in);
            if (n > 0 && fwrite(buf, 1, n, out) != n) {
                res = -4;
            }
        }

        free(buf);
        fclose(in);
        fclose(out);
        return res;
    }

    if (sdom == DOM_SAV) {
        char* data;
        int size;
        int res = sav_read(sh, spath, &data, &size);
        if (res != 0) {
            return res;
        }

        if (ddom == DOM_SD) {
            FILE* out = fopen(dpath, "wb");
            if (out == NULL) {
                free(data);
                return -2;
            }
            int written = size > 0 ? fwrite(data, 1, size, out) : 0;
            fclose(out);
            free(data);
            return written == size ? 0 : -4;
        }

        res = sav_write(dh, dpath, data, size);
        free(data);
        if (res == 0) {
            res = sav_commit(dh);
        }
        return res;
    }

    /* SD -> archive */
    int size = dom_file_size(DOM_SD, -1, spath);
    if (size < 0) {
        return -1;
    }
    if (size > COPY_MAX) {
        return -5;
    }

    char* data = (char*)malloc(size > 0 ? size : 1);
    if (data == NULL) {
        return -3;
    }

    int got  = 0;
    FILE* in = fopen(spath, "rb");
    if (in != NULL) {
        got = size > 0 ? fread(data, 1, size, in) : 0;
        fclose(in);
    }

    int res = 0;
    if (in == NULL || got != size) {
        res = -1;
    }
    else {
        res = sav_write(dh, dpath, data, size);
        if (res == 0) {
            res = sav_commit(dh);
        }
    }

    free(data);
    return res;
}

/* Adds `path` to the g_walk_* counters. Bytes are counted on the SD card only:
 * see dom_file_size. 0 on success, -1 if part of the tree could not be read or
 * MAXDEPTH stopped the walk. */
int count_tree(int dom, int h, char* path, int isdir, int depth)
{
    if (!isdir) {
        g_walk_files = g_walk_files + 1;
        if (dom == DOM_SD) {
            int size = dom_file_size(dom, h, path);
            if (size > 0) {
                g_walk_bytes = g_walk_bytes + size;
            }
        }
        return 0;
    }

    if (depth >= MAXDEPTH) {
        return -1;
    }

    struct directory* d = dom_list(dom, h, path);
    if (d == NULL) {
        return -1;
    }

    char* child = (char*)malloc(PATHN);
    char* name  = (char*)malloc(NAMEN);
    int res     = (child == NULL || name == NULL) ? -1 : 0;

    int i = 0;
    while (i < d->count && res == 0) {
        basename_of(d->files[i], name, NAMEN);
        path_join(path, name, child, PATHN);
        int sub = entry_is_dir(dom, h, d->files[i], child);
        if (sub) {
            g_walk_dirs = g_walk_dirs + 1;
        }
        res = count_tree(dom, h, child, sub, depth + 1);
        i   = i + 1;
    }

    free(child);
    free(name);
    delete_directory(d);
    return res;
}

/* Copies a file or a whole folder. Counts every file it lands on into g_done
 * and paints it on progress layer g_layer. */
int copy_tree(int sdom, int sh, char* spath, int ddom, int dh, char* dpath, int isdir, int depth)
{
    if (!isdir) {
        char note[NAMEN];
        basename_of(spath, note, NAMEN);
        progress_note(note);

        int res = copy_file(sdom, sh, spath, ddom, dh, dpath);
        if (res == 0) {
            g_done = g_done + 1;
            progress_set(g_layer, g_done);
        }
        return res;
    }

    if (depth >= MAXDEPTH) {
        return -7;
    }

    /* mkdir fails when the folder is already there, which is not an error for a
     * merge — only a failure that left nothing behind is */
    int res = dom_mkdir(ddom, dh, dpath);
    if (res != 0 && !dom_is_dir(ddom, dh, dpath)) {
        return res;
    }

    struct directory* d = dom_list(sdom, sh, spath);
    if (d == NULL) {
        return -1;
    }

    char* schild = (char*)malloc(PATHN);
    char* dchild = (char*)malloc(PATHN);
    char* name   = (char*)malloc(NAMEN);
    res          = (schild == NULL || dchild == NULL || name == NULL) ? -3 : 0;

    int i = 0;
    while (i < d->count && res == 0) {
        basename_of(d->files[i], name, NAMEN);
        path_join(spath, name, schild, PATHN);
        path_join(dpath, name, dchild, PATHN);
        int sub = entry_is_dir(sdom, sh, d->files[i], schild);
        res     = copy_tree(sdom, sh, schild, ddom, dh, dchild, sub, depth + 1);
        i       = i + 1;
    }

    free(schild);
    free(dchild);
    free(name);
    delete_directory(d);
    return res;
}

/* Deletes a file or a whole folder, children first. Whatever refuses to go is
 * counted in g_skipped rather than stopping the walk, so one locked file does
 * not leave the rest of a folder untouched. */
int delete_tree(int dom, int h, char* path, int isdir, int depth)
{
    if (!isdir) {
        char note[NAMEN];
        basename_of(path, note, NAMEN);
        progress_note(note);

        int res = dom_del_file(dom, h, path);
        if (res == 0) {
            g_done = g_done + 1;
            progress_set(g_layer, g_done);
        }
        else {
            g_skipped = g_skipped + 1;
        }
        return res;
    }

    if (depth >= MAXDEPTH) {
        g_skipped = g_skipped + 1;
        return -7;
    }

    struct directory* d = dom_list(dom, h, path);
    if (d == NULL) {
        g_skipped = g_skipped + 1;
        return -1;
    }

    char* child = (char*)malloc(PATHN);
    char* name  = (char*)malloc(NAMEN);
    int res     = (child == NULL || name == NULL) ? -3 : 0;
    int failed  = 0;

    int i = 0;
    while (i < d->count && res == 0) {
        basename_of(d->files[i], name, NAMEN);
        path_join(path, name, child, PATHN);
        int sub = entry_is_dir(dom, h, d->files[i], child);
        if (delete_tree(dom, h, child, sub, depth + 1) != 0) {
            failed = 1;
        }
        i = i + 1;
    }

    free(child);
    free(name);
    delete_directory(d);

    if (res != 0) {
        return res;
    }
    if (failed) {
        return -1; /* something inside stayed: the folder cannot go either */
    }

    res = dom_del_dir(dom, h, path);
    if (res == 0) {
        g_done = g_done + 1;
        progress_set(g_layer, g_done);
    }
    else {
        g_skipped = g_skipped + 1;
    }
    return res;
}

/* ---- the listing on show ------------------------------------------------- */

void free_entries(void)
{
    int i = 0;
    while (i < g_ecount) {
        free(g_ename[i]);
        free(g_epath[i]);
        i = i + 1;
    }
    g_ecount = 0;
    g_etrunc = 0;
}

/* Does (adir, aname) sort after (bdir, bname)? Folders first, then names,
 * case-insensitively. */
int sorts_after(int adir, char* aname, int bdir, char* bname)
{
    if (adir != bdir) {
        return bdir != 0; /* a file sorts after a folder */
    }
    return strcasecmp(aname, bname) > 0;
}

/* Insertion sort: a directory read off the card usually comes back close to
 * sorted already, and this runs in the interpreter, where the difference
 * between "nearly sorted" and "n squared" is seconds. */
void sort_entries(void)
{
    int i = 1;
    while (i < g_ecount) {
        char* keyName = g_ename[i];
        char* keyPath = g_epath[i];
        int keyDir    = g_eisdir[i];

        int j = i - 1;
        while (j >= 0 && sorts_after(g_eisdir[j], g_ename[j], keyDir, keyName)) {
            g_ename[j + 1]  = g_ename[j];
            g_epath[j + 1]  = g_epath[j];
            g_eisdir[j + 1] = g_eisdir[j];
            j               = j - 1;
        }

        g_ename[j + 1]  = keyName;
        g_epath[j + 1]  = keyPath;
        g_eisdir[j + 1] = keyDir;
        i               = i + 1;
    }
}

/* Fills the entry table for `path`. 0 if the folder could not be listed. */
int load_entries(int dom, int h, char* path)
{
    free_entries();

    struct directory* d = dom_list(dom, h, path);
    if (d == NULL) {
        return 0;
    }

    int i = 0;
    while (i < d->count) {
        if (g_ecount >= MAXENT) {
            g_etrunc = 1;
            break;
        }

        char name[NAMEN];
        char full[PATHN];
        char shown[NAMEN + 2];
        basename_of(d->files[i], name, NAMEN);
        path_join(path, name, full, PATHN);
        int isdir = entry_is_dir(dom, h, d->files[i], full);
        if (isdir) {
            snprintf(shown, NAMEN + 2, "%s/", name);
        }
        else {
            str_copy(shown, name, NAMEN + 2);
        }

        g_ename[g_ecount]  = strdup(shown);
        g_epath[g_ecount]  = strdup(full);
        g_eisdir[g_ecount] = isdir;
        g_ecount           = g_ecount + 1;
        i                  = i + 1;
    }

    delete_directory(d);
    sort_entries();
    return 1;
}

/* ---- mounting a side ----------------------------------------------------- */

void mount_close(int side)
{
    if (side == SIDE_SRC) {
        if (g_sdom == DOM_SAV && g_sh >= 0) {
            sav_close(g_sh);
        }
        g_sh = -1;
    }
    else {
        if (g_dset && g_ddom == DOM_SAV && g_dh >= 0) {
            sav_close(g_dh);
        }
        g_dh = -1;
    }
}

void mount_sd(int side, char* root, char* label)
{
    mount_close(side);

    if (side == SIDE_SRC) {
        g_sdom   = DOM_SD;
        g_sh     = -1;
        g_stitle = -1;
        g_skind  = 0;
        str_copy(g_sroot, root, PATHN);
        str_copy(g_spath, root, PATHN);
        str_copy(g_slabel, label, NAMEN);
    }
    else {
        g_ddom   = DOM_SD;
        g_dh     = -1;
        g_dtitle = -1;
        g_dkind  = 0;
        str_copy(g_droot, root, PATHN);
        str_copy(g_dpath, root, PATHN);
        str_copy(g_dlabel, label, NAMEN);
        g_dset = 1;
    }
}

/* Opens the title's archive on this side, at its root. 1 on success. The old
 * handle is dropped only once the new one exists. */
int mount_sav(int side, int idx, int kind)
{
    int h = sav_open(idx, kind);
    if (h < 0) {
        char msg[MSGN];
        /* the kinds of save a script cannot reach are a 3DS list; on the Switch
         * every catalog entry is an ordinary save directory */
        if (g_is_switch) {
            sprintf(msg, "That archive could not be opened (%d).", h);
        }
        else {
            sprintf(msg, "That archive could not be opened (%d).\nGBA VC, DSiWare and cartridge SPI\nsaves are not reachable from a script.", h);
        }
        gui_message(msg);
        return 0;
    }

    /* A Switch title has one archive, so naming its kind says nothing; on 3DS
     * the same title can be mounted twice and the label is what tells the two
     * mounts apart. */
    char label[NAMEN];
    char* name = title_name(idx);
    if (g_is_switch) {
        str_copy(label, name, NAMEN);
    }
    else {
        snprintf(label, NAMEN, "%s%s", name, kind == 1 ? " [extdata]" : " [save]");
    }
    free(name);

    mount_close(side);

    if (side == SIDE_SRC) {
        g_sdom   = DOM_SAV;
        g_sh     = h;
        g_stitle = idx;
        g_skind  = kind;
        str_copy(g_sroot, "/", PATHN);
        str_copy(g_spath, "/", PATHN);
        str_copy(g_slabel, label, NAMEN);
    }
    else {
        g_ddom   = DOM_SAV;
        g_dh     = h;
        g_dtitle = idx;
        g_dkind  = kind;
        str_copy(g_droot, "/", PATHN);
        str_copy(g_dpath, "/", PATHN);
        str_copy(g_dlabel, label, NAMEN);
        g_dset = 1;
    }
    return 1;
}

/* A catalog index, or -1. `need` is 0 for every title, 1 for those with save
 * data, 2 for those with extdata. */
int pick_title(char* prompt, int need)
{
    char* names[MAXTITLE];
    int idxs[MAXTITLE];
    int n     = 0;
    int total = titles_count();

    int i = 0;
    while (i < total && n < MAXTITLE) {
        int ok = 1;
        if (need == 1) {
            ok = title_has_save(i);
        }
        else if (need == 2) {
            ok = title_has_extdata(i);
        }

        if (ok) {
            names[n] = title_name(i);
            idxs[n]  = i;
            n        = n + 1;
        }
        i = i + 1;
    }

    if (n == 0) {
        if (need == 1) {
            gui_message("No title on this console has save data\na script can open.");
        }
        else if (need == 2) {
            gui_message("No title on this console has extdata.");
        }
        else {
            gui_message("This console lists no titles.");
        }
        return -1;
    }

    int pick = gui_pick_one(prompt, names, n);

    int j = 0;
    while (j < n) {
        free(names[j]);
        j = j + 1;
    }
    return pick >= 0 ? idxs[pick] : -1;
}

/* Backups of a title, as a mount. 1 on success. */
int mount_backups(int side, int idx)
{
    int kind    = 0;
    int hasSave = title_has_save(idx);
    int hasExt  = title_has_extdata(idx);

    if (hasSave && hasExt) {
        char* which[2];
        which[0] = "Save backups";
        which[1] = "Extdata backups";
        kind     = gui_pick_one("Which backups?", which, 2);
        if (kind < 0) {
            return 0;
        }
    }
    else if (hasExt) {
        /* a title Checkpoint lists for its extdata alone — PKSM's archive is the
         * one everybody meets — has no save backups to ask about */
        kind = 1;
    }

    char* base = title_backup_path(idx, kind);
    if (base[0] == '\0') {
        free(base);
        gui_message("This console keeps no backups of that kind.");
        return 0;
    }

    char root[PATHN];
    char label[NAMEN];
    char* name = title_name(idx);
    str_copy(root, base, PATHN);
    snprintf(label, NAMEN, "%s [backups]", name);
    free(name);
    free(base);

    /* title_backup_path ends in '/', and the folder may not exist yet */
    int n = strlen(root);
    if (n > 1 && root[n - 1] == '/') {
        root[n - 1] = '\0';
    }
    sd_mkdirs(root);

    mount_sd(side, root, label);
    return 1;
}

/* The location picker: what this side should be mounted on. 1 if it was. */
int pick_mount(int side)
{
    char* opts[MAXOPT];
    int codes[MAXOPT];
    int n = 0;

    if (g_launch >= 0) {
        opts[n]  = g_launch_kind == 1 ? "Extdata of the selected title" : "Save data of the selected title";
        codes[n] = 0;
        n        = n + 1;
    }
    opts[n]  = "SD card root";
    codes[n] = 1;
    n        = n + 1;
    opts[n]  = "Checkpoint folder";
    codes[n] = 2;
    n        = n + 1;
    opts[n]  = "Backups of a title";
    codes[n] = 3;
    n        = n + 1;
    opts[n]  = "Save data of a title";
    codes[n] = 4;
    n        = n + 1;
    if (!g_is_switch) {
        opts[n]  = "Extdata of a title";
        codes[n] = 5;
        n        = n + 1;
    }
    opts[n]  = "Type a path...";
    codes[n] = 6;
    n        = n + 1;

    int pick     = gui_pick_one(side == SIDE_SRC ? "Browse what?" : "Destination: browse what?", opts, n);
    g_pickcancel = pick < 0;
    if (pick < 0) {
        return 0;
    }

    int code = codes[pick];

    if (code == 0) {
        return mount_sav(side, g_launch, g_launch_kind);
    }

    if (code == 1) {
        mount_sd(side, g_sdroot, "SD card");
        return 1;
    }

    if (code == 2) {
        char* root = app_root();
        char path[PATHN];
        str_copy(path, root, PATHN);
        free(root);
        mount_sd(side, path, "Checkpoint");
        return 1;
    }

    if (code == 3) {
        int idx = pick_title("Backups of which title?", 0);
        return idx >= 0 ? mount_backups(side, idx) : 0;
    }

    if (code == 4) {
        int idx = pick_title("Save data of which title?", 1);
        return idx >= 0 ? mount_sav(side, idx, 0) : 0;
    }

    if (code == 5) {
        int idx = pick_title("Extdata of which title?", 2);
        return idx >= 0 ? mount_sav(side, idx, 1) : 0;
    }

    /* a typed path. The 3DS keyboard stops at 63 characters, which is the real
     * cap here whatever the buffer says. */
    char typed[PATHN];
    char hint[MSGN];
    snprintf(hint, MSGN, "Path to open (e.g. %s)", g_sdroot);
    gui_keyboard(typed, hint, PATHN);
    if (typed[0] == '\0') {
        return 0;
    }
    if (!sd_is_dir(typed)) {
        gui_message("That is not a folder on the SD card.");
        return 0;
    }

    char label[NAMEN];
    basename_of(typed, label, NAMEN);
    if (label[0] == '\0') {
        str_copy(label, "SD card", NAMEN);
    }
    mount_sd(side, typed, label);
    return 1;
}

/* Where the destination is announced. A dialog covers the whole UI tile while
 * it is up, so the one surface that keeps reading through a menu is the log
 * pane: the destination goes there when it is set, and is not repeated in the
 * prompts, where it only ever fought the folder's own path for the header. */
void log_dest(void)
{
    char msg[MSGN];
    if (!g_dset) {
        return;
    }
    snprintf(msg, MSGN, "destination: %s %s", g_dlabel, g_dpath);
    script_log(msg);
}

int ensure_dest(void)
{
    if (g_dset) {
        return 1;
    }
    if (!gui_confirm("No destination folder is set yet.\nPick one now?")) {
        return 0;
    }
    if (!pick_mount(SIDE_DST)) {
        return 0;
    }
    log_dest();
    return 1;
}

/* Are both sides the same filesystem? Two handles on one title's archive are
 * two views of the same tree, so a rename can move things between them and a
 * copy into a subfolder of the source must be refused. */
int same_fs(void)
{
    if (!g_dset) {
        return 0;
    }
    if (g_sdom == DOM_SD && g_ddom == DOM_SD) {
        return 1;
    }
    return g_sdom == DOM_SAV && g_ddom == DOM_SAV && g_stitle == g_dtitle && g_skind == g_dkind;
}

/* ---- the operations themselves ------------------------------------------- */

/* The promptless core of copy and move: whoever calls it has already asked the
 * user and set up the progress layers. 0 on success. */
int do_transfer(char* path, int isdir, int move)
{
    char name[NAMEN];
    char dest[PATHN];
    basename_of(path, name, NAMEN);
    path_join(g_dpath, name, dest, PATHN);

    /* Inside one filesystem a move is a rename: no bytes travel at all. Not
     * when something is already at the destination, though — neither platform's
     * rename replaces an existing entry, so that case takes the long way and
     * overwrites file by file, which is what the user was asked about. */
    if (move && same_fs() && !dom_exists(g_ddom, g_dh, dest)) {
        int res = dom_rename(g_sdom, g_sh, path, dest);
        if (res == 0) {
            res    = dom_commit(g_sdom, g_sh);
            g_done = g_done + 1;
        }
        return res;
    }

    int res = copy_tree(g_sdom, g_sh, path, g_ddom, g_dh, dest, isdir, 0);
    if (res != 0 || !move) {
        return res;
    }

    res = delete_tree(g_sdom, g_sh, path, isdir, 0);
    dom_commit(g_sdom, g_sh);
    return res;
}

int do_delete(char* path, int isdir)
{
    int res = delete_tree(g_sdom, g_sh, path, isdir, 0);
    dom_commit(g_sdom, g_sh);
    return res;
}

/* Everything a copy or a move has to refuse before it starts. 1 if it may go
 * ahead. */
int transfer_allowed(char* path, int isdir)
{
    char name[NAMEN];
    char dest[PATHN];
    char msg[MSGN];
    basename_of(path, name, NAMEN);
    path_join(g_dpath, name, dest, PATHN);

    if (name[0] == '\0' || strcmp(path, g_sroot) == 0) {
        /* a mount point ("/" on an archive, "/" or "sdmc:/" on the card) has no
         * name of its own to give the copy */
        gui_message("This folder is a mount point and has no\nname to copy under. Open it and use\n\"Select several items...\" instead.");
        return 0;
    }
    if (same_fs() && strcmp(path, dest) == 0) {
        gui_message("The source and the destination folder\nare the same place.");
        return 0;
    }
    if (same_fs() && isdir && is_within(path, g_dpath)) {
        gui_message("A folder cannot be copied into itself.");
        return 0;
    }
    if (g_ddom == DOM_SAV && !isdir && g_sdom == DOM_SD) {
        int size = dom_file_size(DOM_SD, -1, path);
        if (size > COPY_MAX) {
            gui_message("That file is too big to pass through\nthe script's memory (16 MB limit for\nsave archives).");
            return 0;
        }
    }

    if (dom_exists(g_ddom, g_dh, dest)) {
        sprintf(msg, "%s is already in the destination.\n\nOverwrite it? Folders are merged,\nfiles are replaced.", name);
        if (!gui_confirm(msg)) {
            return 0;
        }
    }
    return 1;
}

void report(int res, char* what)
{
    char msg[MSGN];
    if (res == 0) {
        sprintf(msg, "%s: done.", what);
    }
    else if (res == -5) {
        sprintf(msg, "%s: a file was too big to pass\nthrough the script's memory.", what);
    }
    else if (res == -7) {
        sprintf(msg, "%s: the tree is deeper than %d folders.", what, MAXDEPTH);
    }
    else {
        sprintf(msg, "%s: failed (%d).", what, res);
    }
    script_log(msg);
    gui_message(msg);
}

void op_transfer(char* path, int isdir, int move)
{
    char name[NAMEN];
    char msg[MSGN];
    char sizeText[SIZEN];
    basename_of(path, name, NAMEN);

    if (!ensure_dest() || !transfer_allowed(path, isdir)) {
        return;
    }

    gui_status("Measuring...");
    g_walk_files = 0;
    g_walk_dirs  = 0;
    g_walk_bytes = 0;
    count_tree(g_sdom, g_sh, path, isdir, 0);
    fmt_size(g_walk_bytes, sizeText, SIZEN);

    if (g_sdom == DOM_SD) {
        sprintf(msg, "%s\n%s\n\nfrom %s\nto %s\n\n%d file(s), %s. Go ahead?", move ? "Move" : "Copy", name, g_slabel, g_dlabel, g_walk_files,
            sizeText);
    }
    else {
        sprintf(msg, "%s\n%s\n\nfrom %s\nto %s\n\n%d file(s). Go ahead?", move ? "Move" : "Copy", name, g_slabel, g_dlabel, g_walk_files);
    }
    if (!gui_confirm(msg)) {
        return;
    }

    sprintf(msg, "%s %s -> %s", move ? "moving" : "copying", path, g_dpath);
    script_log(msg);

    g_layer   = 0;
    g_done    = 0;
    g_skipped = 0;
    progress_begin(0, move ? "Moving" : "Copying", g_walk_files);
    int res = do_transfer(path, isdir, move);
    progress_clear();
    gui_status("");

    report(res, move ? "Move" : "Copy");
}

void op_delete(char* path, int isdir)
{
    char name[NAMEN];
    char msg[MSGN];
    basename_of(path, name, NAMEN);

    if (strcmp(path, g_sroot) == 0) {
        gui_message("The mount point itself cannot be deleted\nfrom here.");
        return;
    }

    gui_status("Measuring...");
    g_walk_files = 0;
    g_walk_dirs  = 0;
    g_walk_bytes = 0;
    count_tree(g_sdom, g_sh, path, isdir, 0);
    gui_status("");

    if (isdir) {
        sprintf(msg, "Delete the folder\n%s\nand everything in it?\n\n%d file(s), %d subfolder(s).", name, g_walk_files, g_walk_dirs);
    }
    else {
        sprintf(msg, "Delete the file\n%s?", name);
    }
    if (!gui_confirm(msg)) {
        return;
    }

    /* the archive is the one place where a wrong answer costs a save */
    if (g_sdom == DOM_SAV) {
        sprintf(msg, "This is inside %s.\nDeleting it changes the game's save\ndata for real. Are you sure?", g_slabel);
        if (!gui_confirm(msg)) {
            return;
        }
    }

    sprintf(msg, "deleting %s", path);
    script_log(msg);

    g_layer   = 0;
    g_done    = 0;
    g_skipped = 0;
    progress_begin(0, "Deleting", g_walk_files + g_walk_dirs + 1);
    int res = do_delete(path, isdir);
    progress_clear();

    if (res == 0) {
        sprintf(msg, "Deleted %d item(s).", g_done);
        script_log(msg);
        gui_message(msg);
    }
    else {
        sprintf(msg, "Delete failed (%d). %d item(s) went,\n%d could not.", res, g_done, g_skipped);
        script_log(msg);
        gui_message(msg);
    }
}

void op_rename(char* path)
{
    char old[NAMEN];
    char input[NAMEN];
    char parent[PATHN];
    char dest[PATHN];
    char msg[MSGN];
    basename_of(path, old, NAMEN);

    if (strcmp(path, g_sroot) == 0) {
        gui_message("The mount point itself cannot be renamed.");
        return;
    }

    sprintf(msg, "New name for %s", old);
    gui_keyboard(input, msg, NAMEN);
    if (input[0] == '\0') {
        return;
    }
    if (strchr(input, '/') != NULL) {
        gui_message("A name cannot contain '/'.");
        return;
    }
    if (strcmp(input, old) == 0) {
        return;
    }

    path_parent(path, parent, PATHN);
    path_join(parent, input, dest, PATHN);
    if (dom_exists(g_sdom, g_sh, dest)) {
        gui_message("Something with that name is already\nin this folder.");
        return;
    }

    int res = dom_rename(g_sdom, g_sh, path, dest);
    if (res == 0) {
        res = dom_commit(g_sdom, g_sh);
    }
    if (res == 0) {
        sprintf(msg, "renamed %s to %s", old, input);
        script_log(msg);
    }
    else {
        report(res, "Rename");
    }
}

void op_newfolder(void)
{
    char input[NAMEN];
    char dest[PATHN];
    char msg[MSGN];

    gui_keyboard(input, "Name for the new folder", NAMEN);
    if (input[0] == '\0') {
        return;
    }
    if (strchr(input, '/') != NULL) {
        gui_message("A name cannot contain '/'.");
        return;
    }

    path_join(g_spath, input, dest, PATHN);
    if (dom_exists(g_sdom, g_sh, dest)) {
        gui_message("Something with that name is already\nin this folder.");
        return;
    }

    int res = dom_mkdir(g_sdom, g_sh, dest);
    if (res == 0) {
        res = dom_commit(g_sdom, g_sh);
    }
    if (res != 0) {
        report(res, "New folder");
        return;
    }
    sprintf(msg, "created %s", dest);
    script_log(msg);
}

void op_properties(char* path, int isdir)
{
    char msg[MSGN];
    char sizeText[SIZEN];

    if (!isdir) {
        int size = dom_file_size(g_sdom, g_sh, path);
        if (size < 0) {
            gui_message("That file could not be read.");
            return;
        }
        fmt_size(size, sizeText, SIZEN);
        sprintf(msg, "%s\n\nFile on %s\nSize: %s\n\n(No timestamps: the script API has\nno stat.)", path, g_slabel, sizeText);
        gui_message(msg);
        return;
    }

    gui_status("Measuring...");
    g_walk_files = 0;
    g_walk_dirs  = 0;
    g_walk_bytes = 0;
    int res      = count_tree(g_sdom, g_sh, path, 1, 0);
    gui_status("");

    if (g_sdom == DOM_SD) {
        fmt_size(g_walk_bytes, sizeText, SIZEN);
        sprintf(msg, "%s\n\nFolder on %s\nSubfolders: %d\nFiles: %d\nTotal size: %s", path, g_slabel, g_walk_dirs, g_walk_files, sizeText);
    }
    else {
        sprintf(msg, "%s\n\nFolder in %s\nSubfolders: %d\nFiles: %d\n\n(No total: sizing a file in an archive\nmeans reading it.)", path,
            g_slabel, g_walk_dirs, g_walk_files);
    }
    if (res != 0) {
        gui_message("Part of that folder could not be read;\nthe counts below are incomplete.");
    }
    gui_message(msg);
}

void op_zip(char* path)
{
    char name[NAMEN];
    char dest[PATHN];
    char zipName[NAMEN + 8];
    char msg[MSGN];

    if (!ensure_dest()) {
        return;
    }
    if (g_sdom != DOM_SD || g_ddom != DOM_SD) {
        gui_message("Zipping works on the SD card only:\nthe native zip goes through stdio,\nwhich does not see save archives.");
        return;
    }

    basename_of(path, name, NAMEN);
    snprintf(zipName, NAMEN + 8, "%s.zip", name);
    path_join(g_dpath, zipName, dest, PATHN);

    if (sd_exists(dest)) {
        sprintf(msg, "%s is already in the destination.\nReplace it?", zipName);
        if (!gui_confirm(msg)) {
            return;
        }
    }

    sprintf(msg, "Zip %s\ninto %s?", name, g_dlabel);
    if (!gui_confirm(msg)) {
        return;
    }

    gui_status("Zipping...");
    progress_note("packing");
    int res = zip_dir(path, dest);
    progress_clear();
    gui_status("");

    if (res == -2) {
        gui_message("Zip cancelled.");
        return;
    }
    report(res, "Zip");
}

void op_unzip(char* path)
{
    char msg[MSGN];

    if (!ensure_dest()) {
        return;
    }
    if (g_sdom != DOM_SD || g_ddom != DOM_SD) {
        gui_message("Unzipping works on the SD card only:\nthe native zip goes through stdio,\nwhich does not see save archives.");
        return;
    }

    sprintf(msg, "Unpack this zip into\n%s\n%s?", g_dlabel, g_dpath);
    if (!gui_confirm(msg)) {
        return;
    }

    gui_status("Unzipping...");
    progress_note("unpacking");
    int res = unzip(path, g_dpath);
    progress_clear();
    gui_status("");

    if (res == -2) {
        gui_message("Unzip cancelled.");
        return;
    }
    report(res, "Unzip");
}

/* Copy, move or delete several items with one confirmation instead of one per
 * item — the thing a single-selection browser is worst at. */
void op_multi(void)
{
    char msg[MSGN];
    char* ops[4];
    int i;

    if (g_ecount == 0) {
        gui_message("This folder is empty.");
        return;
    }

    i = 0;
    while (i < g_ecount) {
        g_esel[i] = 0;
        i         = i + 1;
    }
    if (!gui_pick_many("Select items", g_ename, g_ecount, g_esel)) {
        return;
    }

    int picked = 0;
    i          = 0;
    while (i < g_ecount) {
        if (g_esel[i]) {
            picked = picked + 1;
        }
        i = i + 1;
    }
    if (picked == 0) {
        return;
    }

    ops[0] = "Copy to destination";
    ops[1] = "Move to destination";
    ops[2] = "Delete";
    ops[3] = "Cancel";
    sprintf(msg, "%d item(s): do what?", picked);
    int op = gui_pick_one(msg, ops, 4);
    if (op < 0 || op == 3) {
        return;
    }

    if (op != 2 && !ensure_dest()) {
        return;
    }

    if (op == 2) {
        sprintf(msg, "Delete %d item(s) from\n%s?\n\nFolders go with everything in them.", picked, g_slabel);
        if (!gui_confirm(msg)) {
            return;
        }
        if (g_sdom == DOM_SAV) {
            sprintf(msg, "This is inside %s.\nDeleting changes the game's save data\nfor real. Are you sure?", g_slabel);
            if (!gui_confirm(msg)) {
                return;
            }
        }
    }
    else {
        sprintf(msg, "%s %d item(s)\nfrom %s\nto %s?", op == 0 ? "Copy" : "Move", picked, g_slabel, g_dlabel);
        if (!gui_confirm(msg)) {
            return;
        }
    }

    sprintf(msg, "batch: %d item(s), operation %d", picked, op);
    script_log(msg);

    int ok     = 0;
    int failed = 0;
    int done   = 0;

    g_layer = 1;
    progress_begin(0, "Items", picked);

    i = 0;
    while (i < g_ecount) {
        if (g_esel[i]) {
            char path[PATHN];
            int isdir = g_eisdir[i];
            str_copy(path, g_epath[i], PATHN);
            progress_label(0, g_ename[i]);

            g_walk_files = 0;
            g_walk_dirs  = 0;
            g_walk_bytes = 0;
            count_tree(g_sdom, g_sh, path, isdir, 0);

            g_done    = 0;
            g_skipped = 0;
            progress_begin(1, op == 2 ? "Deleting" : "Copying", op == 2 ? g_walk_files + g_walk_dirs + 1 : g_walk_files);

            int res = 0;
            if (op == 2) {
                res = do_delete(path, isdir);
            }
            else if (transfer_allowed(path, isdir)) {
                res = do_transfer(path, isdir, op == 1);
            }
            else {
                res = -1;
            }

            if (res == 0) {
                ok = ok + 1;
            }
            else {
                failed = failed + 1;
            }

            done = done + 1;
            progress_set(0, done);
        }
        i = i + 1;
    }

    progress_clear();
    g_layer = 0;

    sprintf(msg, "%d item(s) done, %d failed.", ok, failed);
    script_log(msg);
    gui_message(msg);
}

void set_dest_here(void)
{
    char msg[MSGN];

    if (g_sdom == DOM_SD) {
        mount_sd(SIDE_DST, g_spath, g_slabel);
    }
    else {
        /* the destination needs its own handle: the two sides are closed
         * independently, and one of them may be remounted at any time */
        int h = sav_open(g_stitle, g_skind);
        if (h < 0) {
            sprintf(msg, "That archive could not be opened a\nsecond time (%d).", h);
            gui_message(msg);
            return;
        }
        mount_close(SIDE_DST);
        g_ddom   = DOM_SAV;
        g_dh     = h;
        g_dtitle = g_stitle;
        g_dkind  = g_skind;
        str_copy(g_droot, "/", PATHN);
        str_copy(g_dpath, g_spath, PATHN);
        str_copy(g_dlabel, g_slabel, NAMEN);
        g_dset = 1;
    }

    log_dest();
}

void go_to_dest(void)
{
    if (!g_dset) {
        return;
    }
    if (g_ddom == DOM_SD) {
        mount_sd(SIDE_SRC, g_dpath, g_dlabel);
        return;
    }
    if (mount_sav(SIDE_SRC, g_dtitle, g_dkind)) {
        str_copy(g_spath, g_dpath, PATHN);
    }
}

/* ---- menus --------------------------------------------------------------- */

/* The menu for one entry. 1 if the browser should re-list afterwards. */
int item_menu(int index)
{
    char path[PATHN];
    char* opts[MAXOPT];
    int codes[MAXOPT];
    int isdir = g_eisdir[index];
    int n     = 0;
    str_copy(path, g_epath[index], PATHN);

    if (isdir) {
        opts[n]  = "Open";
        codes[n] = OP_OPEN;
        n        = n + 1;
    }
    opts[n]  = "Copy to destination";
    codes[n] = OP_COPY;
    n        = n + 1;
    opts[n]  = "Move to destination";
    codes[n] = OP_MOVE;
    n        = n + 1;
    opts[n]  = "Rename";
    codes[n] = OP_RENAME;
    n        = n + 1;
    opts[n]  = "Delete";
    codes[n] = OP_DELETE;
    n        = n + 1;
    opts[n]  = "Properties";
    codes[n] = OP_PROPS;
    n        = n + 1;
    if (isdir && g_sdom == DOM_SD) {
        opts[n]  = "Zip to destination";
        codes[n] = OP_ZIP;
        n        = n + 1;
    }
    if (!isdir && g_sdom == DOM_SD && ends_with_ci(path, ".zip")) {
        opts[n]  = "Unpack into destination";
        codes[n] = OP_UNZIP;
        n        = n + 1;
    }
    opts[n]  = "Back";
    codes[n] = OP_BACK;
    n        = n + 1;

    int pick = gui_pick_one(g_ename[index], opts, n);
    if (pick < 0) {
        return 0;
    }

    int code = codes[pick];
    if (code == OP_OPEN) {
        str_copy(g_spath, path, PATHN);
    }
    else if (code == OP_COPY) {
        op_transfer(path, isdir, 0);
    }
    else if (code == OP_MOVE) {
        op_transfer(path, isdir, 1);
    }
    else if (code == OP_RENAME) {
        op_rename(path);
    }
    else if (code == OP_DELETE) {
        op_delete(path, isdir);
    }
    else if (code == OP_PROPS) {
        op_properties(path, isdir);
    }
    else if (code == OP_ZIP) {
        op_zip(path);
    }
    else if (code == OP_UNZIP) {
        op_unzip(path);
    }
    return 1;
}

/* Pick one entry and open its menu — how a folder is acted on without being
 * entered, since picking a folder row in the list always opens it. */
int pick_item_menu(void)
{
    if (g_ecount == 0) {
        gui_message("This folder is empty.");
        return 0;
    }
    int pick = gui_pick_one("Act on which item?", g_ename, g_ecount);
    if (pick < 0) {
        return 0;
    }
    return item_menu(pick);
}

/* The menu for the folder on show. 1 if the browser should re-list. */
int folder_menu(void)
{
    char* opts[MAXOPT];
    int codes[MAXOPT];
    char msg[MSGN];
    int n = 0;

    opts[n]  = "Act on one item...";
    codes[n] = OP_ITEM;
    n        = n + 1;
    opts[n]  = "Select several items...";
    codes[n] = OP_MULTI;
    n        = n + 1;
    opts[n]  = "New folder here";
    codes[n] = OP_MKDIR;
    n        = n + 1;
    opts[n]  = "Set this folder as destination";
    codes[n] = OP_SETDST;
    n        = n + 1;
    if (g_dset) {
        opts[n]  = "Go to the destination";
        codes[n] = OP_GODST;
        n        = n + 1;
    }
    opts[n]  = "Copy this folder to destination";
    codes[n] = OP_COPY;
    n        = n + 1;
    opts[n]  = "Properties of this folder";
    codes[n] = OP_PROPS;
    n        = n + 1;
    opts[n]  = "Browse somewhere else...";
    codes[n] = OP_MOUNT;
    n        = n + 1;
    opts[n]  = "Back";
    codes[n] = OP_BACK;
    n        = n + 1;

    /* One line, and only the folder: a dialog's header is a single truncated
     * line, so a second one spilled across the rule under it. Where the
     * destination is instead: log_dest. */
    snprintf(msg, MSGN, "%s", g_spath);

    int pick = gui_pick_one(msg, opts, n);
    if (pick < 0) {
        return 0;
    }

    int code = codes[pick];
    if (code == OP_ITEM) {
        return pick_item_menu();
    }
    if (code == OP_MULTI) {
        op_multi();
    }
    else if (code == OP_MKDIR) {
        op_newfolder();
    }
    else if (code == OP_SETDST) {
        set_dest_here();
    }
    else if (code == OP_GODST) {
        go_to_dest();
    }
    else if (code == OP_COPY) {
        op_transfer(g_spath, 1, 0);
    }
    else if (code == OP_PROPS) {
        op_properties(g_spath, 1);
    }
    else if (code == OP_MOUNT) {
        pick_mount(SIDE_SRC);
    }
    return 1;
}

/* The browse loop. Returns when the user leaves this mount. */
void browse(void)
{
    char status[MSGN];
    char prompt[MSGN];
    char up[PATHN];

    while (1) {
        if (g_dset) {
            snprintf(status, MSGN, "%s | to: %s", g_slabel, g_dlabel);
        }
        else {
            snprintf(status, MSGN, "%s", g_slabel);
        }
        gui_status(status);

        if (!load_entries(g_sdom, g_sh, g_spath)) {
            gui_message("That folder could not be listed.");
            if (strcmp(g_spath, g_sroot) == 0) {
                return;
            }
            path_parent(g_spath, up, PATHN);
            str_copy(g_spath, up, PATHN);
        }
        else {
            int atRoot = strcmp(g_spath, g_sroot) == 0;
            int n      = 0;
            int upRow  = -1;

            g_rows[n] = "[ Folder menu ]";
            n         = n + 1;
            if (!atRoot) {
                g_rows[n] = ".. (up one folder)";
                upRow     = n;
                n         = n + 1;
            }

            int first = n;
            int i     = 0;
            while (i < g_ecount) {
                g_rows[n] = g_ename[i];
                n         = n + 1;
                i         = i + 1;
            }

            if (g_etrunc) {
                snprintf(prompt, MSGN, "%s\n(only the first %d entries)", g_spath, MAXENT);
            }
            else {
                snprintf(prompt, MSGN, "%s", g_spath);
            }

            int pick = gui_pick_one(prompt, g_rows, n);
            if (pick < 0) {
                /* B: up one folder, and out of the mount at its root */
                if (atRoot) {
                    return;
                }
                path_parent(g_spath, up, PATHN);
                str_copy(g_spath, up, PATHN);
            }
            else if (pick == 0) {
                folder_menu();
            }
            else if (pick == upRow) {
                path_parent(g_spath, up, PATHN);
                str_copy(g_spath, up, PATHN);
            }
            else {
                int idx = pick - first;
                if (g_eisdir[idx]) {
                    str_copy(g_spath, g_epath[idx], PATHN);
                }
                else {
                    item_menu(idx);
                }
            }
        }
    }
}

int main(int argc, char** argv)
{
    /* picoc does not promise zeroed globals */
    g_sdom   = DOM_SD;
    g_sh     = -1;
    g_stitle = -1;
    g_skind  = 0;
    g_dset   = 0;
    g_ddom   = DOM_SD;
    g_dh     = -1;
    g_dtitle = -1;
    g_dkind  = 0;
    g_ecount = 0;
    g_etrunc = 0;
    g_layer  = 0;
    g_sroot[0]  = '\0';
    g_spath[0]  = '\0';
    g_slabel[0] = '\0';
    g_droot[0]  = '\0';
    g_dpath[0]  = '\0';
    g_dlabel[0] = '\0';

    /* the SD card's root in this platform's spelling: "/" on 3DS,
     * "sdmc:/" on Switch, told apart by app_root()'s prefix */
    char* root  = app_root();
    g_is_switch = strncmp(root, "sdmc:", 5) == 0;
    if (g_is_switch) {
        str_copy(g_sdroot, "sdmc:/", PATHN);
    }
    else {
        str_copy(g_sdroot, "/", PATHN);
    }
    free(root);

    /* The title highlighted when the script was launched, if any, and which
     * archive of it there is to open: a title the catalog carries for its
     * extdata alone has no save to offer. */
    g_launch      = argv[0][0] != '\0' ? title_find(argv[0]) : -1;
    g_launch_kind = 0;
    if (g_launch >= 0 && !title_has_save(g_launch)) {
        if (title_has_extdata(g_launch)) {
            g_launch_kind = 1;
        }
        else {
            g_launch = -1;
        }
    }

    script_log("browser: started");

    /* One mount at a time: leaving a mount comes back to the picker, a mount
     * that would not open comes back to it too, and backing out of the picker
     * itself ends the run. */
    g_pickcancel = 0;
    while (1) {
        if (pick_mount(SIDE_SRC)) {
            browse();
        }
        else if (g_pickcancel) {
            break;
        }
    }

    free_entries();
    mount_close(SIDE_SRC);
    mount_close(SIDE_DST);
    progress_clear();
    gui_status("");
    return 0;
}
