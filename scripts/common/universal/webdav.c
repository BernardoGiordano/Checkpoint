/*
 * webdav.c — WebDAV save sync (Checkpoint script)
 *
 * End-user setup (server URL, app passwords, per-server notes, troubleshooting):
 * scripts/webdav.md, next to the scripting manual in scripts/README.md.
 *
 * Uploads Checkpoint save backups to any WebDAV server the user already runs or
 * pays for — Nextcloud, ownCloud, Synology, Apache mod_dav, `rclone serve
 * webdav`, most "personal cloud" boxes — and downloads them back. One script
 * serves both consoles: every local path is built from app_root() ("/3ds/
 * Checkpoint" or "sdmc:/switch/Checkpoint"), so there is nothing platform-
 * specific to edit.
 *
 * Where googledrive.c needs a Google Cloud project and an OAuth device flow,
 * this needs a URL, a username and a password. That is the whole trade: no
 * account to create, no consent screen, no token to refresh — and no server
 * unless you have one.
 *
 * The menu:
 *   - All titles    — every title that has at least one backup on SD,
 *                     skipping backups already on the server (so a second run
 *                     over Wi-Fi only sends what is new),
 *   - This title    — the title highlighted in Checkpoint when the script ran,
 *   - One backup    — pick one backup folder and send it, existing copy or not,
 *   - Restore       — list what is on the server and unpack one backup into
 *                     Checkpoint's backup folder for that title, ready for
 *                     Checkpoint's own Restore,
 *   - Settings      — server details, passphrase, connection test.
 * Each backup folder travels as a store-only zip, streamed from SD.
 *
 * Server layout:  <base URL>/Checkpoint/<3ds|switch>/<title name>/<backup>.zip
 *
 * On-SD layout (paths relative to app_root()):
 *   config/webdav.vault   the URL, username and password, sealed (device_seal)
 *   config/webdav.json    optional plaintext setup file you drop in yourself:
 *                         {"url": "...", "user": "...", "password": "..."}
 *                         Read once, folded into the vault, then DELETED. It
 *                         exists because a WebDAV URL is often longer than the
 *                         63 characters the 3DS keyboard accepts.
 *
 * Credential storage, and what it is worth. The password is the asset here, and
 * it is never on the card in the clear — device_seal encrypts the vault with a
 * key derived from material only this console's own services can answer for. So
 * a card read on a PC, a config folder the user shares, and a scraper hunting
 * for *.json all come away with nothing, and the vault does not work on a
 * different console.
 *
 * It is NOT protection from other homebrew on this console: neither console
 * isolates homebrew and Checkpoint is open source, so the console-bound half of
 * the key is reproducible by anyone who reads common/script/seal_api.cpp. The
 * passphrase in "Settings" is the half that is a real boundary — it lives
 * nowhere but in the user's head, at the cost of one keyboard prompt per run.
 *
 * The rest of the damage control is the server's job, and worth doing: give
 * this script an app password (Nextcloud, ownCloud and Synology all mint them)
 * rather than your account password, scoped to a share holding nothing else,
 * and revoke it if the console is lost. HTTPS certificates are not verified by
 * Checkpoint's HTTP layer, so treat a hostile network as able to read the
 * traffic and use a server you reach over your own LAN or a trusted link.
 */

#include <checkpoint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* PROPFIND body. One literal: picoc does not concatenate adjacent strings.
 * The default namespace form (no "d:" prefix) is what every server tested
 * accepts, and it keeps the response's own prefix — d:, D: or none — out of the
 * request. */
#define PROPFIND_BODY "<?xml version=\"1.0\" encoding=\"utf-8\"?><propfind xmlns=\"DAV:\"><prop><resourcetype/><getcontentlength/></prop></propfind>"

/* buffer sizes (macros, not expressions, so picoc's array bounds stay literal) */
#define URLN    512  /* the base URL the user typed                          */
#define USERN   128
#define PWN     128
#define AUTHN   512  /* "Authorization: Basic " + base64(user:password)       */
#define URLBUFN 1536 /* a built request URL: base + 3 percent-encoded segments */
#define NAMEN   160  /* a server-side path segment (sanitised title/backup)    */
#define PATHN   512
#define PASSN   65   /* passphrase buffer, terminator included (gui_keyboard)  */
#define VAULTN  2048 /* the vault's JSON document, sized for the worst case the
                      * buffers above allow — every value at full length and
                      * every character escaped — because picoc's whole
                      * interpreter stack is 64 KB and this is one frame. */
#define MAX_PICK 128 /* cap on the pick lists (titles / backups / server rows) */

char g_base[URLN];  /* base collection URL, never a trailing '/' */
char g_user[USERN];
char g_pw[PWN];
char g_auth[AUTHN]; /* built from g_user / g_pw once, per run */

/* The passphrase for this run, "" when the vault has none. Held only in this
 * global for the length of the run — never written anywhere. */
char g_pass[PASSN];

/* paths built once from app_root() so one script serves 3DS and Switch */
char g_cfg_dir[PATHN];     /* <root>/config                */
char g_vault_path[PATHN];  /* <root>/config/webdav.vault   */
char g_plain_path[PATHN];  /* <root>/config/webdav.json    */
char g_tmp_zip[PATHN];     /* <root>/config/_webdav_tmp.zip */
char g_platform[16];       /* "3ds" or "switch": server subfolder */

char g_sync[URLBUFN];      /* <base>/Checkpoint/<platform>, built once per run */

void init_paths(void)
{
    /* picoc does not promise zeroed globals, and every "is there one?" test
     * below is a [0] == '\0' check */
    g_base[0] = '\0';
    g_user[0] = '\0';
    g_pw[0]   = '\0';
    g_auth[0] = '\0';
    g_pass[0] = '\0';
    g_sync[0] = '\0';

    char* root = app_root(); /* malloc'd */
    sprintf(g_cfg_dir, "%s/config", root);
    sprintf(g_vault_path, "%s/config/webdav.vault", root);
    sprintf(g_plain_path, "%s/config/webdav.json", root);
    sprintf(g_tmp_zip, "%s/config/_webdav_tmp.zip", root);
    /* the root always contains "/switch/" or "/3ds/", so the console the script
     * runs on names its own subfolder under Checkpoint on the server */
    if (strstr(root, "/switch") != NULL) {
        strcpy(g_platform, "switch");
    }
    else {
        strcpy(g_platform, "3ds");
    }
    char line[PATHN + 64];
    sprintf(line, "webdav sync (%s)", g_platform);
    script_log(line);
    sprintf(line, "config: %s", g_cfg_dir);
    script_log(line);
    free(root);
}

/* ---- tiny helpers ------------------------------------------------------- */

/* Read a whole SD file into a malloc'd buffer, NUL-terminated one byte past the
 * end so a text caller can treat it as a string. *size gets the byte count,
 * which is what the vault needs: it is binary and full of embedded NULs, so its
 * length cannot be recovered with strlen. NULL (and *size 0) on error. */
char* slurp_n(char* path, int* size)
{
    size[0] = 0;
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    int bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (bytes <= 0) {
        fclose(f);
        return NULL;
    }
    char* buf = malloc(bytes + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    int got = fread(buf, 1, bytes, f);
    fclose(f);
    buf[got] = '\0';
    size[0]  = got;
    return buf;
}

/* slurp_n for text, where the length is the string's own */
char* slurp(char* path)
{
    int n = 0;
    return slurp_n(path, &n);
}

/* Size of an SD file in bytes, or -1. Used only to log what was moved. */
int file_size(char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    int n = ftell(f);
    fclose(f);
    return n;
}

/* Copy string member `key` of object `o` into dest[size], NUL-terminated; dest
 * becomes "" when the key is absent or not a string. Frees the copy
 * json_get_string hands back, and never calls a getter on a value that would
 * ProgramFail (abort the whole script) the way an unguarded access would. */
void jget(struct JSON* o, char* key, char* dest, int size)
{
    dest[0] = '\0';
    if (!json_is_object(o) || !json_object_contains(o, key)) {
        return;
    }
    struct JSON* e = json_object_element(o, key);
    if (!json_is_string(e)) {
        return;
    }
    char* s = json_get_string(e);
    strncpy(dest, s, size - 1);
    dest[size - 1] = '\0';
    free(s);
}

/* Append the verbatim `raw`, then `value` JSON-escaped, to whatever is already
 * in `dst` — refusing (0) rather than truncating if the result would not fit
 * `limit` bytes including the terminator.
 *
 * Two reasons this is not "escape into a scratch buffer": picoc's whole
 * interpreter stack is 64 KB, so a second array the size of a password is not
 * free; and a silently truncated credential would be sealed into the vault and
 * only fail against the server later. Index-only (picoc rejects pointer
 * arithmetic on arrays). */
int json_append(char* dst, char* raw, char* value, int limit)
{
    int j = strlen(dst);
    int i = 0;
    while (raw[i] != '\0') {
        if (j + 1 >= limit) {
            return 0;
        }
        dst[j] = raw[i];
        j      = j + 1;
        i      = i + 1;
    }
    i = 0;
    while (value[i] != '\0') {
        char c   = value[i];
        int need = 1;
        if (c == '\\' || c == '"') {
            need = 2;
        }
        if (j + need >= limit) {
            return 0;
        }
        if (need == 2) {
            dst[j] = '\\';
            j      = j + 1;
        }
        if (c == '\n' || c == '\r' || c == '\t') {
            dst[j] = ' ';
        }
        else {
            dst[j] = c;
        }
        j = j + 1;
        i = i + 1;
    }
    dst[j] = '\0';
    return 1;
}

/* Drop one trailing '/' from `s`, in place. A WebDAV collection's href always
 * carries one, and the name is what comes before it. */
void rstrip_slash(char* s)
{
    int n = strlen(s);
    if (n > 0 && s[n - 1] == '/') {
        s[n - 1] = '\0';
    }
}

/* Copy the final path component of `full` (everything after the last '/') into
 * dst[size]. read_directory returns full paths and a WebDAV href is a full
 * server path, so this recovers the folder's or file's own name. Index-only. */
void basename_of(char* full, char* dst, int size)
{
    int last = -1;
    int i    = 0;
    while (full[i] != '\0') {
        if (full[i] == '/') {
            last = i;
        }
        i = i + 1;
    }
    int start = last + 1;
    int j     = 0;
    while (full[start + j] != '\0' && j < size - 1) {
        dst[j] = full[start + j];
        j      = j + 1;
    }
    dst[j] = '\0';
}

/* ---- strings the protocol needs ----------------------------------------- */

/* base64 of `src` into dst[size]. There is no base64 in the interpreter's
 * stdlib and HTTP Basic is defined in terms of it, so here it is: 40 bytes of
 * credential per run, not a hot loop. Refuses to emit a half quad rather than
 * truncate — a clipped Authorization header is a 401 nobody can explain. */
void b64_encode(char* src, char* dst, int size)
{
    char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int n     = strlen(src);
    int i     = 0;
    int j     = 0;
    while (i < n) {
        int b0   = src[i] & 0xFF;
        int b1   = 0;
        int b2   = 0;
        int have = 1;
        if (i + 1 < n) {
            b1   = src[i + 1] & 0xFF;
            have = 2;
        }
        if (i + 2 < n) {
            b2   = src[i + 2] & 0xFF;
            have = 3;
        }
        if (j + 4 >= size) {
            break;
        }
        dst[j] = tbl[(b0 >> 2) & 0x3F];
        j      = j + 1;
        dst[j] = tbl[((b0 << 4) | (b1 >> 4)) & 0x3F];
        j      = j + 1;
        if (have > 1) {
            dst[j] = tbl[((b1 << 2) | (b2 >> 6)) & 0x3F];
        }
        else {
            dst[j] = '=';
        }
        j = j + 1;
        if (have > 2) {
            dst[j] = tbl[b2 & 0x3F];
        }
        else {
            dst[j] = '=';
        }
        j = j + 1;
        i = i + 3;
    }
    dst[j] = '\0';
}

/* value of one hex digit, -1 if it is not one */
int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Percent-decode `src` into dst[size]. The inverse of url_encode, which the API
 * has no binding for and every WebDAV listing needs: hrefs come back encoded,
 * so "Pok%c3%a9mon" has to become the folder name again before it can be
 * matched against a title. A stray '%' is passed through rather than eaten.
 * Index-only, and never reads past the terminator. */
void pct_decode(char* src, char* dst, int size)
{
    int i = 0;
    int j = 0;
    while (src[i] != '\0' && j < size - 1) {
        int done = 0;
        if (src[i] == '%' && src[i + 1] != '\0') {
            int hi = hexval(src[i + 1]);
            if (hi >= 0 && src[i + 2] != '\0') {
                int lo = hexval(src[i + 2]);
                if (lo >= 0) {
                    dst[j] = hi * 16 + lo;
                    i      = i + 3;
                    j      = j + 1;
                    done   = 1;
                }
            }
        }
        if (!done) {
            dst[j] = src[i];
            i      = i + 1;
            j      = j + 1;
        }
    }
    dst[j] = '\0';
}

/* Index of `pat` in `s` at or after `from`, -1 if absent. strstr would do this,
 * but it answers with a pointer into the middle of the buffer and picoc has no
 * pointer arithmetic to walk on from there, so the whole XML scan below is
 * written in indices. */
int find_from(char* s, char* pat, int from)
{
    int n = strlen(s);
    int m = strlen(pat);
    int i = from;
    if (i < 0) {
        i = 0;
    }
    while (i + m <= n) {
        int k = 0;
        while (k < m && s[i + k] == pat[k]) {
            k = k + 1;
        }
        if (k == m) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

/* Is the tag containing position `at` a closing tag? "href>" matches inside
 * </d:href> just as happily as inside <d:href>, and taking the text after a
 * closing tag would hand back the whitespace up to the next element. */
int is_close_tag(char* s, int at)
{
    int i = at;
    while (i >= 0 && s[i] != '<') {
        i = i - 1;
    }
    return i >= 0 && s[i + 1] == '/';
}

/* Copy `src` into dst[size] as one safe path segment for a remote store: the
 * characters Windows shares, macOS and several WebDAV front ends refuse become
 * '_', and trailing dots and spaces (which Windows silently drops, making a
 * folder that can never be matched again) are trimmed. A name that sanitises to
 * nothing becomes "untitled" rather than an empty path segment. Index-only. */
void sanitize_name(char* src, char* dst, int size)
{
    int i = 0;
    int j = 0;
    while (src[i] != '\0' && j < size - 1) {
        char c = src[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            dst[j] = '_';
        }
        else if (c < 32) {
            dst[j] = '_';
        }
        else {
            dst[j] = c;
        }
        i = i + 1;
        j = j + 1;
    }
    while (j > 0 && (dst[j - 1] == '.' || dst[j - 1] == ' ')) {
        j = j - 1;
    }
    dst[j] = '\0';
    if (dst[0] == '\0') {
        strcpy(dst, "untitled");
    }
}

/* Append one path segment to the URL already in `dst`, percent-encoded, with
 * the separating '/'. 0 (and `dst` untouched) if it would not fit `limit`
 * bytes: a truncated URL is a request against the wrong collection, which on a
 * PUT would write the file into the wrong place rather than fail. */
int url_add(char* dst, char* segment, int limit)
{
    char* enc = url_encode(segment); /* malloc'd; encodes '/' too, which is why
                                      * this takes one segment at a time */
    int have  = strlen(dst);
    int ok    = 0;
    if (have + 1 + strlen(enc) + 1 <= limit) {
        dst[have]     = '/';
        dst[have + 1] = '\0';
        strcat(dst, enc);
        ok = 1;
    }
    free(enc);
    return ok;
}

/* ---- credential vault --------------------------------------------------- */

/* One line of plain English for a device_seal / device_unseal result code, so
 * the user is told which of the several very different failures happened (a
 * mistyped passphrase and a vault from another console are both -5, and only
 * the user knows which). `dst` needs 160 bytes. */
void seal_error_text(int rc, char* dst)
{
    if (rc == -5) {
        strcpy(dst, "Wrong passphrase, or this vault was\nsealed on a different console.");
    }
    else if (rc == -4) {
        strcpy(dst, "This console would not give up a key\nand the vault has no passphrase.");
    }
    else if (rc == -2) {
        strcpy(dst, "This vault was written by a newer\nversion of Checkpoint.");
    }
    else if (rc == -3) {
        strcpy(dst, "Out of memory.");
    }
    else if (rc == -6) {
        strcpy(dst, "This console would not produce the\nrandom bytes needed to seal a vault.");
    }
    else {
        sprintf(dst, "The vault could not be read (%d).", rc);
    }
}

/* Seal g_base / g_user / g_pw under g_pass and write config/webdav.vault.
 * 1 on success. Called on first setup, when the server details change, and
 * whenever the passphrase changes. */
int vault_write(void)
{
    char json[VAULTN];
    strcpy(json, "{");
    int ok = json_append(json, "\"url\":\"", g_base, VAULTN);
    ok     = ok && json_append(json, "\",\"user\":\"", g_user, VAULTN);
    ok     = ok && json_append(json, "\",\"password\":\"", g_pw, VAULTN);
    ok     = ok && json_append(json, "\"}", "", VAULTN);
    if (!ok) {
        script_log("the server details do not fit the vault document buffer");
        gui_message("Those server details are too long to\nstore. This is a bug — please report it.");
        return 0;
    }

    char* blob = NULL;
    int bn     = 0;
    gui_status("Sealing the server details...");
    int rc = device_seal(json, strlen(json), g_pass, &blob, &bn);
    if (rc != 0) {
        char msg[160];
        seal_error_text(rc, msg);
        script_log("device_seal failed");
        gui_message(msg);
        return 0;
    }

    sd_mkdirs(g_cfg_dir);
    FILE* f = fopen(g_vault_path, "wb");
    if (f == NULL) {
        free(blob);
        script_log("could not open webdav.vault for writing");
        gui_message("Could not write config/webdav.vault.");
        return 0;
    }
    int wrote = fwrite(blob, 1, bn, f);
    fclose(f);
    free(blob);
    if (wrote != bn) {
        script_log("webdav.vault was written short");
        gui_message("config/webdav.vault was written short.\nThe SD card may be full.");
        return 0;
    }
    if (g_pass[0] != '\0') {
        script_log("vault sealed (console key + passphrase)");
    }
    else {
        script_log("vault sealed (console key)");
    }
    return 1;
}

/* Open config/webdav.vault into g_base / g_user / g_pw, prompting for the
 * passphrase if the vault says it has one.
 *
 * 1 = loaded, 0 = there is no vault yet, -1 = there is one but it could not be
 * opened. The caller must keep -1 and 0 apart: writing a fresh vault over one
 * whose passphrase was merely mistyped would throw away working settings. */
int vault_read(void)
{
    int n     = 0;
    char* raw = slurp_n(g_vault_path, &n);
    if (raw == NULL) {
        return 0;
    }

    int needs = seal_needs_passphrase(raw, n);
    if (needs < 0) {
        free(raw);
        script_log("webdav.vault is not a sealed file");
        gui_message("config/webdav.vault is damaged.\nDelete it to set the server up again.");
        return -1;
    }

    /* Up to three goes at the passphrase: a typo on an on-screen keyboard is not
     * the same event as a wrong passphrase, and making the user relaunch the
     * script for each attempt would be its own reason to turn the feature off. */
    char* json = NULL;
    int jn     = 0;
    int rc     = 0;
    int tries  = 0;
    while (tries < 3) {
        g_pass[0] = '\0';
        if (needs == 1) {
            gui_keyboard(g_pass, "WebDAV passphrase", PASSN);
            if (g_pass[0] == '\0') {
                free(raw);
                script_log("cancelled at the passphrase prompt");
                return -1;
            }
        }

        gui_status("Opening the credential vault...");
        rc = device_unseal(raw, n, g_pass, &json, &jn);
        if (rc == 0) {
            break;
        }
        tries = tries + 1;
        /* Only a rejected key is worth retrying; every other code means the
         * passphrase was never the problem. */
        if (rc != -5 || needs != 1 || tries == 3) {
            break;
        }
        gui_message("That passphrase did not open the vault.\nTry again.");
    }
    free(raw);
    if (rc != 0) {
        char msg[160];
        seal_error_text(rc, msg);
        script_log("device_unseal failed");
        gui_message(msg);
        return -1;
    }

    struct JSON* v = json_new();
    json_parse(v, json);
    free(json);
    jget(v, "url", g_base, URLN);
    jget(v, "user", g_user, USERN);
    jget(v, "password", g_pw, PWN);
    json_delete(v);

    if (g_base[0] == '\0') {
        script_log("the vault opened but carries no url");
        gui_message("The vault opened but has no server URL.\nDelete config/webdav.vault to set the\nserver up again.");
        return -1;
    }
    script_log("vault opened");
    return 1;
}

/* Trim whatever the user typed or pasted into a base URL this script can build
 * on: no trailing '/', and a scheme, because curl treats a bare host as a
 * relative path and fails with something unreadable. 1 if it looks usable. */
int normalize_base(void)
{
    int n = strlen(g_base);
    while (n > 0 && (g_base[n - 1] == '/' || g_base[n - 1] == ' ')) {
        n         = n - 1;
        g_base[n] = '\0';
    }
    if (strncmp(g_base, "http://", 7) != 0 && strncmp(g_base, "https://", 8) != 0) {
        script_log("the server URL has no http:// or https:// scheme");
        gui_message("The server URL must start with\nhttp:// or https://");
        return 0;
    }
    return 1;
}

/* Ask for a passphrase twice and leave it in g_pass on agreement. 1 = a
 * passphrase was set, 0 = the user backed out (g_pass untouched).
 *
 * The minimum length is enforced rather than warned about: a four-character
 * passphrase is guessed offline in no time at all, and a user who typed one
 * would walk away believing the vault was protected by it. */
int ask_new_passphrase(void)
{
    char first[PASSN];
    char again[PASSN];
    gui_keyboard(first, "New passphrase (8+ characters)", PASSN);
    if (first[0] == '\0') {
        return 0;
    }
    if (strlen(first) < 8) {
        gui_message("That passphrase is too short.\nUse at least 8 characters — a short one\nis guessed in seconds by anyone holding\nthe file.");
        return 0;
    }
    gui_keyboard(again, "Type it again", PASSN);
    if (strcmp(first, again) != 0) {
        gui_message("The two passphrases did not match.\nNothing was changed.");
        return 0;
    }
    strcpy(g_pass, first);
    return 1;
}

/* Read config/webdav.json, the plaintext setup file. 1 when it was there and
 * carried a URL. Absent is not an error — the keyboard path is the other way
 * in, and on Switch it is the comfortable one. */
int import_plain_config(void)
{
    char* raw = slurp(g_plain_path);
    if (raw == NULL) {
        return 0;
    }
    struct JSON* root = json_new();
    json_parse(root, raw);
    free(raw);
    if (!json_is_valid(root) || !json_is_object(root)) {
        json_delete(root);
        script_log("webdav.json is not valid JSON");
        gui_message("config/webdav.json is not valid JSON.\nIt was ignored.");
        return 0;
    }
    jget(root, "url", g_base, URLN);
    jget(root, "user", g_user, USERN);
    jget(root, "password", g_pw, PWN);
    json_delete(root);

    if (g_base[0] == '\0') {
        script_log("webdav.json has no url");
        gui_message("config/webdav.json has no \"url\".");
        return 0;
    }
    script_log("server details read from webdav.json");
    return 1;
}

/* Type the three fields in by hand. 1 when a URL was entered.
 *
 * The 3DS keyboard stops at 63 characters and a Nextcloud dav URL is often
 * longer than that, so the file route is offered first — and the length limit
 * is stated rather than discovered by a URL that silently loses its tail. */
int ask_server_details(void)
{
    gui_message("Enter your WebDAV server details.\n\nOn 3DS the keyboard stops at 63\ncharacters. For a longer URL, put\n{\"url\":\"...\",\"user\":\"...\",\n\"password\":\"...\"} in\nconfig/webdav.json and run this again.");

    char buf[URLN];
    gui_keyboard(buf, "WebDAV URL (https://...)", URLN);
    if (buf[0] == '\0') {
        return 0;
    }
    strncpy(g_base, buf, URLN - 1);
    g_base[URLN - 1] = '\0';

    gui_keyboard(buf, "Username", USERN);
    strncpy(g_user, buf, USERN - 1);
    g_user[USERN - 1] = '\0';

    gui_keyboard(buf, "Password or app password", PWN);
    strncpy(g_pw, buf, PWN - 1);
    g_pw[PWN - 1] = '\0';
    return 1;
}

/* No vault yet (or the user asked to change servers): collect the details from
 * whichever source is there, seal them, and delete the plaintext. 1 on success. */
int vault_setup(void)
{
    g_base[0] = '\0';
    g_user[0] = '\0';
    g_pw[0]   = '\0';

    int have = import_plain_config();
    if (!have) {
        have = ask_server_details();
    }
    if (!have) {
        script_log("no server details entered");
        return 0;
    }
    if (!normalize_base()) {
        return 0;
    }

    /* The one moment where asking costs the user nothing they were not already
     * doing, so the honest trade-off is put to them here rather than buried in
     * a menu. Both branches are real answers — the default is not a mistake. */
    g_pass[0] = '\0';
    if (gui_confirm("Protect the saved password with a\npassphrase, typed on every run?\n\nWithout one it is still encrypted and\ntied to this console, but other homebrew\nhere could unseal it.")) {
        ask_new_passphrase();
    }

    if (!vault_write()) {
        return 0;
    }

    /* The plaintext goes now that the vault holds it. Announced, because a
     * config file quietly vanishing from the SD card looks like a bug. */
    if (sd_exists(g_plain_path)) {
        remove(g_plain_path);
        script_log("config/webdav.json deleted (its contents are in the vault)");
        gui_message("Server details moved into\nconfig/webdav.vault (encrypted, tied to\nthis console).\n\nconfig/webdav.json has been deleted —\nit held your password in the clear.");
    }
    return 1;
}

/* Forget the server locally: the vault file and the copies in memory. */
void vault_forget(void)
{
    if (sd_exists(g_vault_path)) {
        remove(g_vault_path);
    }
    g_base[0] = '\0';
    g_user[0] = '\0';
    g_pw[0]   = '\0';
    g_auth[0] = '\0';
    g_pass[0] = '\0';
}

/* Open the vault, or set one up. 1 when g_base / g_user / g_pw are usable. */
int ensure_configured(void)
{
    int have = vault_read();
    if (have < 0) {
        /* The vault is there but shut. Deliberately no fallback to a fresh
         * setup: that would write over a vault whose passphrase the user simply
         * mistyped, and the message from vault_read already said so. */
        return 0;
    }
    if (have == 0) {
        return vault_setup();
    }
    return normalize_base();
}

/* ---- webdav ------------------------------------------------------------- */

/* "Authorization: Basic base64(user:password)". Built once per run: the header
 * goes into every request, and re-encoding it per call would put the password
 * on the interpreter stack in a dozen more places. */
void build_auth(void)
{
    char pair[USERN + PWN + 2];
    sprintf(pair, "%s:%s", g_user, g_pw);
    char enc[AUTHN];
    b64_encode(pair, enc, AUTHN);
    sprintf(g_auth, "Authorization: Basic %s", enc);
}

/* One request carrying the auth header (plus `extra`, "" for none) whose body
 * is not wanted. Returns the HTTP status, or the binding's negative code. */
int dav_call(char* method, char* url, char* extra, char* body)
{
    char h[AUTHN + 128];
    if (extra[0] != '\0') {
        sprintf(h, "%s\n%s", g_auth, extra);
    }
    else {
        strcpy(h, g_auth);
    }
    char* out = NULL;
    int n     = 0;
    int st    = web_request(method, url, h, body, strlen(body), &out, &n, NULL);
    if (out != NULL) {
        free(out);
    }
    return st;
}

/* One line of plain English for a status this script could not use. `dst` needs
 * 200 bytes. Written once because every entry point — test, upload, restore —
 * hits the same handful of causes, and "HTTP 401" is not an answer. */
void dav_explain(int st, char* dst)
{
    if (st == 401) {
        strcpy(dst, "The server refused the username or\npassword (401).\n\nIf your server offers app passwords,\nuse one here.");
    }
    else if (st == 403) {
        strcpy(dst, "The server accepted the login but\nforbade the request (403). Check that\nthis account may write to that folder.");
    }
    else if (st == 404) {
        strcpy(dst, "The server has nothing at that URL (404).\nCheck the path — for Nextcloud it ends\nwith /remote.php/dav/files/<username>");
    }
    else if (st == 405) {
        strcpy(dst, "The server answered, but that URL does\nnot speak WebDAV (405).");
    }
    else if (st == 301 || st == 302 || st == 307 || st == 308) {
        strcpy(dst, "The server redirected the request.\nUse the address it redirects to —\nuploads do not follow redirects.");
    }
    else if (st == 507) {
        strcpy(dst, "The server is out of space (507).");
    }
    else if (st == -1) {
        strcpy(dst, "No HTTP support is available.");
    }
    else if (st == -2) {
        strcpy(dst, "The answer did not fit in memory.");
    }
    else if (st < 0) {
        sprintf(dst, "The server could not be reached\n(transfer error %d). Check Wi-Fi and\nthe address.", st);
    }
    else {
        sprintf(dst, "The server answered HTTP %d.", st);
    }
}

/* Create the collection at `url` (find-or-create: 405 is "it is already
 * there"). 1 when the collection exists afterwards. */
int dav_mkcol(char* url)
{
    int st = dav_call("MKCOL", url, "", "");
    if (st == 201 || st == 405 || st == 200 || st == 204) {
        return 1;
    }
    char line[128];
    sprintf(line, "MKCOL failed (%d)", st);
    script_log(line);
    return 0;
}

/* PROPFIND `url` at `depth` ("0" for the collection itself, "1" for its
 * children). Returns the malloc'd response body (caller free()s) and writes the
 * status to *status; NULL when there is no body. */
char* dav_propfind(char* url, char* depth, int* status)
{
    char h[AUTHN + 128];
    sprintf(h, "%s\nDepth: %s\nContent-Type: application/xml; charset=utf-8", g_auth, depth);
    char* out = NULL;
    int n     = 0;
    char* body = PROPFIND_BODY;
    status[0]  = web_request("PROPFIND", url, h, body, strlen(body), &out, &n, NULL);
    return out;
}

/* Names of the entries directly inside collection `url`, as malloc'd strings in
 * `names` (the caller free()s each). Returns the count, or -1 if the listing
 * itself failed — which the callers must keep apart from 0, since "the server
 * is unreachable" and "nothing uploaded yet" want different words.
 *
 * The parse is a scan for href elements rather than a real XML parse: the
 * interpreter has no XML anywhere, and a multistatus body's structure is fixed
 * enough that the names are exactly the hrefs. A Depth: 1 listing also contains
 * the collection the request asked about, which is why the name that matches
 * the requested URL's own last segment is dropped — comparing names rather than
 * trusting it to come first, since the order is the server's business. */
int dav_list(char* url, char** names, int max)
{
    /* the requested collection's own name, decoded the same way the hrefs are */
    char selfPath[URLBUFN];
    pct_decode(url, selfPath, URLBUFN);
    rstrip_slash(selfPath);
    char selfName[NAMEN];
    basename_of(selfPath, selfName, NAMEN);

    int st     = 0;
    char* body = dav_propfind(url, "1", &st);
    if (st != 207 || body == NULL) {
        if (body != NULL) {
            free(body);
        }
        char line[96];
        sprintf(line, "PROPFIND failed (%d)", st);
        script_log(line);
        return -1;
    }

    int count = 0;
    int pos   = 0;
    while (count < max) {
        int h = find_from(body, "href>", pos);
        if (h < 0) {
            break;
        }
        pos = h + 5;
        if (is_close_tag(body, h)) {
            continue;
        }
        int end = find_from(body, "<", pos);
        if (end < 0) {
            break;
        }
        int start = pos;
        int len   = end - start;
        pos       = end;
        if (len <= 0 || len >= URLBUFN) {
            continue;
        }

        char raw[URLBUFN];
        int k = 0;
        while (k < len) {
            raw[k] = body[start + k];
            k      = k + 1;
        }
        raw[len] = '\0';

        char dec[URLBUFN];
        pct_decode(raw, dec, URLBUFN);
        rstrip_slash(dec);
        char base[NAMEN];
        basename_of(dec, base, NAMEN);
        if (base[0] != '\0' && strcmp(base, selfName) != 0) {
            names[count] = strdup(base);
            count        = count + 1;
        }
    }
    free(body);
    return count;
}

/* Is `name` one of the `count` strings in `names`? */
int name_in(char** names, int count, char* name)
{
    int i = 0;
    while (i < count) {
        if (strcmp(names[i], name) == 0) {
            return 1;
        }
        i = i + 1;
    }
    return 0;
}

void free_names(char** names, int count)
{
    int i = 0;
    while (i < count) {
        free(names[i]);
        i = i + 1;
    }
}

/* Upload the local file at `path` to `url`. 1 on success. The bytes stream
 * straight from SD (they never enter the interpreter heap) and drive the
 * reserved innermost progress bar. */
int dav_put(char* path, char* url)
{
    char h[AUTHN + 64];
    sprintf(h, "%s\nContent-Type: application/zip", g_auth);
    char* out = NULL;
    int n     = 0;
    int st    = web_upload_file("PUT", url, h, path, &out, &n, NULL);
    if (out != NULL) {
        free(out);
    }
    if (st == 200 || st == 201 || st == 204) {
        return 1;
    }
    char line[96];
    /* negative = transport error, positive = the status the server sent */
    sprintf(line, "  upload rejected (code %d)", st);
    script_log(line);
    return 0;
}

/* Download `url` into the local file `path`. 1 on success.
 *
 * Unlike the upload, the body does come through memory — there is no
 * download-to-file binding — so a very large backup can fail with -2 on a 3DS.
 * That is reported as what it is rather than as a network error. */
int dav_get(char* url, char* path)
{
    char* out = NULL;
    int n     = 0;
    progress_note("downloading");
    int st = web_request("GET", url, g_auth, "", 0, &out, &n, NULL);
    if (st != 200 || out == NULL || n <= 0) {
        if (out != NULL) {
            free(out);
        }
        char msg[200];
        if (st == -2) {
            strcpy(msg, "That backup is too big to download on\nthis console.");
        }
        else {
            dav_explain(st, msg);
        }
        char line[96];
        sprintf(line, "  download failed (code %d)", st);
        script_log(line);
        gui_message(msg);
        return 0;
    }

    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        free(out);
        script_log("  could not open the temporary file for writing");
        gui_message("Could not write to the SD card.");
        return 0;
    }
    int wrote = fwrite(out, 1, n, f);
    fclose(f);
    free(out);
    if (wrote != n) {
        remove(path);
        script_log("  the temporary file was written short");
        gui_message("The download could not be saved.\nThe SD card may be full.");
        return 0;
    }
    return 1;
}

/* PROPFIND the base URL and say what happened, in words. 1 when the server
 * answered as a WebDAV collection. */
int dav_test(void)
{
    gui_status("Testing the connection...");
    script_log("testing the connection");
    int st     = 0;
    char* body = dav_propfind(g_base, "0", &st);
    if (body != NULL) {
        free(body);
    }
    char line[96];
    sprintf(line, "PROPFIND on the base url answered %d", st);
    script_log(line);
    if (st == 207) {
        gui_message("The server answered. The URL, username\nand password all work.");
        return 1;
    }
    char msg[200];
    dav_explain(st, msg);
    gui_message(msg);
    return 0;
}

/* Build <base>/Checkpoint/<platform> into g_sync, creating both collections.
 * 1 when the destination is ready. */
int ensure_sync_root(void)
{
    char url[URLBUFN];
    strncpy(url, g_base, URLBUFN - 1);
    url[URLBUFN - 1] = '\0';

    progress_note("preparing the server folder");
    if (!url_add(url, "Checkpoint", URLBUFN)) {
        gui_message("The server URL is too long.");
        return 0;
    }
    if (!dav_mkcol(url)) {
        gui_message("Could not create the \"Checkpoint\"\nfolder on the server.");
        return 0;
    }
    /* every console keeps its backups in its own subfolder, so a 3DS and a
     * Switch syncing to the same server never mix titles up */
    if (!url_add(url, g_platform, URLBUFN)) {
        gui_message("The server URL is too long.");
        return 0;
    }
    if (!dav_mkcol(url)) {
        char msg[128];
        sprintf(msg, "Could not create the\n\"Checkpoint/%s\" folder on the server.", g_platform);
        gui_message(msg);
        return 0;
    }
    strcpy(g_sync, url);
    return 1;
}

/* <sync root>/<title> into dst. 1 on success. */
int title_url(char* titleName, char* dst, int limit)
{
    char safe[NAMEN];
    sanitize_name(titleName, safe, NAMEN);
    strncpy(dst, g_sync, limit - 1);
    dst[limit - 1] = '\0';
    return url_add(dst, safe, limit);
}

/* ---- upload ------------------------------------------------------------- */

/* Zip one backup folder and PUT it under the title's collection. 1 ok.
 *
 * Progress contract: the caller owns the item bar and its label names the
 * title; zip_dir and web_upload_file drive the reserved innermost bar
 * themselves and label it by phase ("zip", "upload"). Between those phases the
 * same bar shows this function's progress_note, so the row always says what is
 * happening — never a bare track. No bar restates what another bar says. */
int upload_backup(char* titleName, char* backupPath, char* backupName, char* titleUrl)
{
    char st[512];
    sprintf(st, "%s / %s", titleName, backupName);
    gui_status(st);
    sprintf(st, "> %s / %s", titleName, backupName);
    script_log(st);

    progress_note(backupName);
    if (zip_dir(backupPath, g_tmp_zip) != 0) {
        remove(g_tmp_zip);
        script_log("  zip failed (out of space, or cancelled)");
        return 0;
    }
    int zbytes = file_size(g_tmp_zip);
    if (zbytes > 0) {
        sprintf(st, "  zipped %d KB", zbytes / 1024);
        script_log(st);
    }

    char url[URLBUFN];
    strncpy(url, titleUrl, URLBUFN - 1);
    url[URLBUFN - 1] = '\0';
    char zipName[NAMEN];
    char safe[NAMEN];
    sanitize_name(backupName, safe, NAMEN - 8);
    sprintf(zipName, "%s.zip", safe);

    int ok = 0;
    if (url_add(url, zipName, URLBUFN)) {
        ok = dav_put(g_tmp_zip, url);
    }
    else {
        script_log("  the backup's URL would be too long");
    }
    remove(g_tmp_zip);

    if (ok) {
        script_log("  uploaded");
    }
    else {
        script_log("  UPLOAD FAILED");
    }
    return ok;
}

/* Upload every backup folder inside a title's save-backup dir, skipping the
 * ones already on the server. `lvl` is the bar depth this call owns: 1 under
 * the "Titles" bar of an all-titles run, 0 when this title is the whole job, so
 * an unused outer slot is never drawn. */
int upload_title(int idx, int* done, int* skipped, int* failed, int lvl)
{
    char* base          = title_backup_path(idx, 0); /* trailing slash */
    char* name          = title_name(idx);
    struct directory* d = read_directory(base);
    if (d == NULL || d->count == 0) {
        if (d != NULL) {
            delete_directory(d);
        }
        free(base);
        free(name);
        return 0;
    }

    char st[512];
    sprintf(st, "== %s (%d backup(s))", name, d->count);
    script_log(st);

    char turl[URLBUFN];
    if (!title_url(name, turl, URLBUFN) || !dav_mkcol(turl)) {
        script_log("  could not create the title's folder on the server");
        *failed = *failed + d->count;
        delete_directory(d);
        free(base);
        free(name);
        return 0;
    }

    /* One listing per title, so a re-run over Wi-Fi costs one request per title
     * instead of one upload per backup. A listing that fails is treated as
     * empty: uploading a second copy is a wasted transfer, never a lost save. */
    char* remote[MAX_PICK];
    int rn = dav_list(turl, remote, MAX_PICK);
    if (rn < 0) {
        rn = 0;
    }

    int i;
    progress_begin(lvl, name, d->count);
    for (i = 0; i < d->count; i++) {
        char bname[NAMEN];
        basename_of(d->files[i], bname, NAMEN);

        char safe[NAMEN];
        char zipName[NAMEN];
        sanitize_name(bname, safe, NAMEN - 8);
        sprintf(zipName, "%s.zip", safe);

        if (name_in(remote, rn, zipName)) {
            sprintf(st, "- %s / %s (already there)", name, bname);
            script_log(st);
            *skipped = *skipped + 1;
        }
        else if (upload_backup(name, d->files[i], bname, turl)) {
            *done = *done + 1;
        }
        else {
            *failed = *failed + 1;
        }
        /* set after the item, so the bar reads "k of n done" rather than
         * sitting at 0 for the whole of the first upload */
        progress_set(lvl, i + 1);
    }
    progress_end(lvl);

    free_names(remote, rn);
    delete_directory(d);
    free(base);
    free(name);
    return 1;
}

/* Does title `idx` have at least one backup folder on SD? */
int title_has_backups(int idx)
{
    char* base          = title_backup_path(idx, 0);
    struct directory* d = read_directory(base);
    int has             = (d != NULL && d->count > 0);
    if (d != NULL) {
        delete_directory(d);
    }
    free(base);
    return has;
}

/* Fill `names` (malloc'd) and `idxs` with the titles that have backups on SD.
 * Returns the count. */
int collect_titles_with_backups(char** names, int* idxs, int max)
{
    int total = titles_count();
    int n     = 0;
    int i;
    progress_note("looking for titles with backups");
    for (i = 0; i < total && n < max; i++) {
        if (title_has_save(i) && title_has_backups(i)) {
            idxs[n]  = i;
            names[n] = title_name(i); /* malloc'd */
            n        = n + 1;
        }
    }
    return n;
}

/* "One backup": pick a title that has backups, then pick one of its backups.
 * Always uploads, replacing any copy already on the server — this is the way
 * to push a backup the bulk runs would skip. */
void upload_one_backup(int* done, int* failed)
{
    int idxs[MAX_PICK];
    char* names[MAX_PICK];
    int n = collect_titles_with_backups(names, idxs, MAX_PICK);
    if (n == 0) {
        script_log("no title on this console has a backup to upload");
        gui_message("No backups found on the SD card to\nupload.");
        return;
    }

    int tpick = gui_pick_one("Pick a title", names, n);
    if (tpick >= 0) {
        int idx             = idxs[tpick];
        char* base          = title_backup_path(idx, 0);
        struct directory* d = read_directory(base);
        if (d != NULL && d->count > 0) {
            char* bnames[MAX_PICK];
            int bn = d->count < MAX_PICK ? d->count : MAX_PICK;
            int k;
            for (k = 0; k < bn; k++) {
                char tmp[NAMEN];
                basename_of(d->files[k], tmp, NAMEN);
                bnames[k] = strdup(tmp);
            }
            int bpick = gui_pick_one("Pick a backup", bnames, bn);
            if (bpick >= 0) {
                char turl[URLBUFN];
                if (title_url(names[tpick], turl, URLBUFN) && dav_mkcol(turl)) {
                    if (upload_backup(names[tpick], d->files[bpick], bnames[bpick], turl)) {
                        *done = *done + 1;
                    }
                    else {
                        *failed = *failed + 1;
                    }
                }
                else {
                    script_log("could not create the title's folder on the server");
                    gui_message("Could not create that title's folder on\nthe server.");
                    *failed = *failed + 1;
                }
            }
            free_names(bnames, bn);
        }
        if (d != NULL) {
            delete_directory(d);
        }
        free(base);
    }

    free_names(names, n);
}

/* ---- restore ------------------------------------------------------------ */

/* Drop everything in `names` that is not a .zip, freeing what it drops, and
 * return the new count. A collection or a stray file in the title's folder on
 * the server would otherwise be offered as a backup and fail at the unzip. */
int keep_zips(char** names, int count)
{
    int kept = 0;
    int i    = 0;
    while (i < count) {
        int len   = strlen(names[i]);
        int isZip = 0;
        if (len > 4 && names[i][len - 4] == '.') {
            char c1 = names[i][len - 3];
            char c2 = names[i][len - 2];
            char c3 = names[i][len - 1];
            if ((c1 == 'z' || c1 == 'Z') && (c2 == 'i' || c2 == 'I') && (c3 == 'p' || c3 == 'P')) {
                isZip = 1;
            }
        }
        if (isZip) {
            names[kept] = names[i];
            kept        = kept + 1;
        }
        else {
            free(names[i]);
        }
        i = i + 1;
    }
    return kept;
}

/* A folder under the title's local backup path that does not exist yet: `name`,
 * else "name (2)", "name (3)"... Restoring the same backup twice must not
 * quietly merge two save sets into one folder. */
void free_backup_dir(char* base, char* name, char* dst)
{
    sprintf(dst, "%s%s", base, name);
    int n = 2;
    while (sd_exists(dst) && n < 100) {
        sprintf(dst, "%s%s (%d)", base, name, n);
        n = n + 1;
    }
}

/* Download one backup zip and unpack it into Checkpoint's backup folder for
 * that title. 1 on success. */
int restore_backup(int idx, char* titleName, char* titleUrl, char* zipName)
{
    char st[512];
    sprintf(st, "> %s / %s", titleName, zipName);
    script_log(st);
    sprintf(st, "%s / %s", titleName, zipName);
    gui_status(st);

    char url[URLBUFN];
    strncpy(url, titleUrl, URLBUFN - 1);
    url[URLBUFN - 1] = '\0';
    if (!url_add(url, zipName, URLBUFN)) {
        script_log("  the backup's URL would be too long");
        return 0;
    }

    if (!dav_get(url, g_tmp_zip)) {
        return 0;
    }
    int zbytes = file_size(g_tmp_zip);
    if (zbytes > 0) {
        sprintf(st, "  downloaded %d KB", zbytes / 1024);
        script_log(st);
    }

    /* the folder name is the zip's name without ".zip" (keep_zips has already
     * made sure there is one) */
    char folder[NAMEN];
    strncpy(folder, zipName, NAMEN - 1);
    folder[NAMEN - 1] = '\0';
    int len           = strlen(folder);
    if (len > 4) {
        folder[len - 4] = '\0';
    }

    char* base = title_backup_path(idx, 0); /* trailing slash */
    char dest[PATHN];
    free_backup_dir(base, folder, dest);
    free(base);

    if (sd_mkdirs(dest) != 0) {
        remove(g_tmp_zip);
        script_log("  could not create the backup folder on SD");
        gui_message("Could not create the backup folder on\nthe SD card.");
        return 0;
    }

    progress_note(folder);
    int rc = unzip(g_tmp_zip, dest);
    remove(g_tmp_zip);
    if (rc != 0) {
        script_log("  unzip failed (damaged file, out of space, or cancelled)");
        gui_message("The downloaded backup could not be\nunpacked.");
        return 0;
    }

    sprintf(st, "  unpacked into %s", dest);
    script_log(st);
    return 1;
}

/* "Restore": list the titles on the server, match them back to this console's
 * catalog, then pick one of that title's backups.
 *
 * Matching is by the same sanitised name the upload used, so a title uploaded
 * from this console always matches itself. A folder on the server with no title
 * installed here is left out of the list rather than shown and refused: there
 * is nowhere to put its save. */
void restore_flow(int* done, int* failed)
{
    gui_status("Reading the server...");
    progress_note("listing the server");
    char* remote[MAX_PICK];
    int rn = dav_list(g_sync, remote, MAX_PICK);
    if (rn < 0) {
        gui_message("Could not list the backups on the\nserver.");
        return;
    }
    if (rn == 0) {
        script_log("the server holds no backups for this console yet");
        gui_message("There are no backups on the server for\nthis console yet.");
        return;
    }

    /* intersect the server's folders with the catalog */
    int idxs[MAX_PICK];
    char* names[MAX_PICK];
    int n     = 0;
    int total = titles_count();
    int i;
    for (i = 0; i < total && n < MAX_PICK; i++) {
        char* tname = title_name(i);
        char safe[NAMEN];
        sanitize_name(tname, safe, NAMEN);
        if (title_has_save(i) && name_in(remote, rn, safe)) {
            idxs[n]  = i;
            names[n] = tname; /* malloc'd, handed over */
            n        = n + 1;
        }
        else {
            free(tname);
        }
    }
    free_names(remote, rn);

    if (n == 0) {
        script_log("no title on the server is installed on this console");
        gui_message("The server has backups, but none of\nthose titles are installed on this\nconsole.");
        return;
    }

    int tpick = gui_pick_one("Restore from server", names, n);
    if (tpick < 0) {
        free_names(names, n);
        return;
    }

    char turl[URLBUFN];
    if (!title_url(names[tpick], turl, URLBUFN)) {
        free_names(names, n);
        gui_message("The server URL is too long.");
        return;
    }

    gui_status("Reading the server...");
    progress_note("listing the backups");
    char* zips[MAX_PICK];
    int zn = dav_list(turl, zips, MAX_PICK);
    if (zn > 0) {
        zn = keep_zips(zips, zn);
    }
    if (zn <= 0) {
        free_names(names, n);
        if (zn < 0) {
            gui_message("Could not list that title's backups on\nthe server.");
        }
        else {
            gui_message("That title has no backups on the server.");
        }
        return;
    }

    int zpick = gui_pick_one("Pick a backup", zips, zn);
    if (zpick >= 0) {
        char confirm[320];
        sprintf(confirm, "Download\n%s\ninto Checkpoint's backup folder for\n%s?\n\nNothing is written to the game's save\nuntil you restore it in Checkpoint.", zips[zpick], names[tpick]);
        if (gui_confirm(confirm)) {
            if (restore_backup(idxs[tpick], names[tpick], turl, zips[zpick])) {
                *done = *done + 1;
                gui_message("Downloaded. It is now in Checkpoint's\nbackup list for that title — pick it\nthere and press Restore to write it to\nthe game.");
            }
            else {
                *failed = *failed + 1;
            }
        }
    }

    free_names(zips, zn);
    free_names(names, n);
}

/* ---- settings ----------------------------------------------------------- */

/* The Settings menu. Returns 1 when the saved server is gone and the run should
 * stop. */
int settings_menu(void)
{
    char* opts[5];
    opts[0] = "Test connection";
    opts[1] = "Change server details";
    opts[2] = "Set or change passphrase";
    opts[3] = "Remove passphrase";
    opts[4] = "Forget this server";
    int c   = gui_pick_one("Settings", opts, 5);
    if (c < 0) {
        return 0;
    }

    if (c == 0) {
        dav_test();
        return 0;
    }

    if (c == 1) {
        char oldBase[URLN];
        char oldUser[USERN];
        char oldPw[PWN];
        char oldPass[PASSN];
        strcpy(oldBase, g_base);
        strcpy(oldUser, g_user);
        strcpy(oldPw, g_pw);
        strcpy(oldPass, g_pass);
        if (!vault_setup()) {
            /* put back what is still sealed on the card */
            strcpy(g_base, oldBase);
            strcpy(g_user, oldUser);
            strcpy(g_pw, oldPw);
            strcpy(g_pass, oldPass);
            return 0;
        }
        build_auth();
        dav_test();
        return 0;
    }

    if (c == 2) {
        char saved[PASSN];
        strcpy(saved, g_pass);
        if (!ask_new_passphrase()) {
            return 0;
        }
        if (vault_write()) {
            gui_message("Passphrase set. You will be asked for it\neach time this script runs.\n\nThere is no way to recover it: if you\nforget it, forget the server here and\nenter its details again.");
        }
        else {
            /* the vault on the card still wants the old passphrase */
            strcpy(g_pass, saved);
        }
        return 0;
    }

    if (c == 3) {
        if (g_pass[0] == '\0') {
            gui_message("This vault has no passphrase.");
            return 0;
        }
        if (!gui_confirm("Remove the passphrase?\n\nThe vault stays encrypted and tied to\nthis console, but other homebrew on this\nconsole would be able to unseal it.")) {
            return 0;
        }
        char saved[PASSN];
        strcpy(saved, g_pass);
        g_pass[0] = '\0';
        if (!vault_write()) {
            strcpy(g_pass, saved);
        }
        else {
            gui_message("Passphrase removed.");
        }
        return 0;
    }

    if (!gui_confirm("Forget this server?\n\nThe saved URL and password are deleted\nfrom this console. Backups already on\nthe server are left alone.")) {
        return 0;
    }
    vault_forget();
    script_log("saved server details deleted");
    gui_message("Forgotten. Nothing was changed on the\nserver — if this console may have been\nlost, revoke the app password there too.");
    return 1;
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char** argv)
{
    init_paths();

    if (!ensure_configured()) {
        return 1;
    }
    build_auth();

    /* selected_title() / argv[0] carry the highlighted title's id (empty if
     * none). Offer "This title" only when there is one. */
    char* selId = (argc > 0) ? argv[0] : "";
    int haveSel = selId != NULL && selId[0] != '\0';

    char* opts[5];
    int nopts        = 0;
    opts[nopts]      = "Upload all titles";
    nopts            = nopts + 1;
    int thisTitleRow = -1;
    if (haveSel) {
        thisTitleRow = nopts;
        opts[nopts]  = "Upload this title";
        nopts        = nopts + 1;
    }
    int oneBackupRow = nopts;
    opts[nopts]      = "Upload one backup...";
    nopts            = nopts + 1;
    int restoreRow   = nopts;
    opts[nopts]      = "Restore from server...";
    nopts            = nopts + 1;
    int settingsRow  = nopts;
    opts[nopts]      = "Settings...";
    nopts            = nopts + 1;

    /* The mode picker comes before the server is touched, so reaching Settings
     * (in particular the connection test, and forgetting a server) costs no
     * round trip and works even when the server itself is misbehaving. */
    int choice = gui_pick_one("WebDAV sync", opts, nopts);
    if (choice < 0) {
        script_log("cancelled at the mode picker");
        return 0;
    }
    char pick[64];
    sprintf(pick, "mode: %s", opts[choice]);
    script_log(pick);

    if (choice == settingsRow) {
        settings_menu();
        return 0;
    }

    gui_status("Preparing the server folder...");
    if (!ensure_sync_root()) {
        return 1;
    }

    int done    = 0;
    int skipped = 0;
    int failed  = 0;

    if (choice == restoreRow) {
        restore_flow(&done, &failed);
    }
    else if (choice == 0) {
        /* All titles. Counted up front so the outer bar has a real total
         * instead of crawling towards a number that keeps moving. */
        int idxs[MAX_PICK];
        char* names[MAX_PICK];
        int n = collect_titles_with_backups(names, idxs, MAX_PICK);
        char line[96];
        sprintf(line, "all titles: %d have backups to upload", n);
        script_log(line);
        if (n == 0) {
            gui_message("No backups found on the SD card to\nupload.");
        }

        int i;
        progress_begin(0, "Titles", n);
        for (i = 0; i < n; i++) {
            upload_title(idxs[i], &done, &skipped, &failed, 1);
            progress_set(0, i + 1);
        }
        progress_end(0);
        free_names(names, n);
    }
    else if (choice == thisTitleRow) {
        int idx = title_find(selId);
        if (idx >= 0) {
            upload_title(idx, &done, &skipped, &failed, 0);
        }
        else {
            script_log("the selected title is not in the catalog");
            gui_message("The selected title was not found.");
        }
    }
    else if (choice == oneBackupRow) {
        upload_one_backup(&done, &failed);
    }

    progress_clear();

    char summary[160];
    if (choice == restoreRow) {
        sprintf(summary, "Done. Restored %d, failed %d.", done, failed);
    }
    else if (skipped > 0) {
        sprintf(summary, "Done. Uploaded %d, skipped %d already\non the server, failed %d.", done, skipped, failed);
    }
    else {
        sprintf(summary, "Done. Uploaded %d, failed %d.", done, failed);
    }
    script_log(summary);
    gui_message(summary);
    return failed ? 1 : 0;
}
