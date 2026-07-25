/*
 * googledrive.c — Google Drive save sync (Checkpoint script)
 *
 * Uploads Checkpoint save backups to the user's own Google Drive. One script
 * serves both consoles: every path is built from app_root() ("/3ds/Checkpoint"
 * or "sdmc:/switch/Checkpoint"), so there is nothing platform-specific to edit.
 *
 * First run signs the console in with the OAuth 2.0 Device Authorization flow
 * (shows a code to type at google.com/device); later runs silently refresh the
 * stored token. Then a menu offers three granularities:
 *   - All titles   — every title that has at least one backup on SD,
 *   - This title   — the title highlighted in Checkpoint when the script ran,
 *   - One backup   — pick a single backup folder and upload just that.
 * Each backup folder is zipped (store-only) and uploaded whole with a resumable
 * PUT; re-uploading a backup of the same name updates the Drive file in place.
 * Drive layout:  My Drive / Checkpoint / <3ds|switch> / <title name> / <backup name>.zip
 *
 * Scope is drive.file: the app can only ever touch files it creates itself, so
 * it can never see the rest of the user's Drive. The consent screen stays benign.
 *
 * On-SD layout (paths relative to app_root()):
 *   config/client_secret.json   (you copy this in from the Google Cloud Console)
 *   config/gdrive_token.json     (written here: {"refresh_token":...})
 *
 * Only <checkpoint.h>, <stdio.h>, <stdlib.h>, <string.h>, <unistd.h> (sleep).
 */

#include <checkpoint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_CODE_URL "https://oauth2.googleapis.com/device/code"
#define TOKEN_URL       "https://oauth2.googleapis.com/token"
#define FILES_URL       "https://www.googleapis.com/drive/v3/files"
#define UPLOAD_URL      "https://www.googleapis.com/upload/drive/v3/files"

/* pre-encoded scope "https://www.googleapis.com/auth/drive.file" — the ':' and
 * '/' have to be percent-encoded in a form body, and this is a constant. */
#define SCOPE_ENC "https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fdrive.file"

/* buffer sizes (macros, not expressions, so picoc's array bounds stay literal) */
#define IDN   256
#define SECN  256
#define TOKN  2560
#define AUTHN 2624   /* "Authorization: Bearer " + up to TOKN of access token */
#define PATHN 512
#define MAX_PICK 128 /* cap on the "One backup" pick lists (titles / backups) */

char g_id[IDN];         /* installed.client_id     */
char g_secret[SECN];    /* installed.client_secret */
char g_access[TOKN];    /* current access token    */
char g_auth[AUTHN];     /* "Authorization: Bearer <access>" request header */

/* paths built once from app_root() so one script serves 3DS and Switch */
char g_cfg_dir[PATHN];     /* <root>/config                     */
char g_secret_path[PATHN]; /* <root>/config/client_secret.json  */
char g_token_path[PATHN];  /* <root>/config/gdrive_token.json   */
char g_tmp_zip[PATHN];     /* <root>/config/_gdrive_tmp.zip     */
char g_platform[16];       /* "3ds" or "switch": Drive subfolder */

void init_paths(void)
{
    char* root = app_root(); /* malloc'd */
    sprintf(g_cfg_dir, "%s/config", root);
    sprintf(g_secret_path, "%s/config/client_secret.json", root);
    sprintf(g_token_path, "%s/config/gdrive_token.json", root);
    sprintf(g_tmp_zip, "%s/config/_gdrive_tmp.zip", root);
    /* the root always contains "/switch/" or "/3ds/", so the console the
     * script runs on names its own subfolder under Checkpoint on Drive */
    if (strstr(root, "/switch") != NULL) {
        strcpy(g_platform, "switch");
    }
    else {
        strcpy(g_platform, "3ds");
    }
    char line[PATHN + 64];
    sprintf(line, "google drive sync (%s)", g_platform);
    script_log(line);
    sprintf(line, "config: %s", g_cfg_dir);
    script_log(line);
    free(root);
}

/* ---- tiny helpers ------------------------------------------------------- */

/* read a whole SD file into a malloc'd NUL-terminated buffer; NULL on error */
char* slurp(char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    char* buf = malloc(size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    int got = fread(buf, 1, size, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* Copy string member `key` of object `o` into dest[size], NUL-terminated; dest
 * becomes "" when the key is absent or not a string. Frees the copy
 * json_get_string hands back, so there is no leak — and, crucially, never calls
 * json_object_element / json_get_string on a value that would ProgramFail
 * (abort the whole script) the way an unguarded access would. */
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

/* integer member `key`, or `fallback` if absent/not an int */
int jget_int(struct JSON* o, char* key, int fallback)
{
    if (!json_is_object(o) || !json_object_contains(o, key)) {
        return fallback;
    }
    struct JSON* e = json_object_element(o, key);
    if (!json_is_int(e)) {
        return fallback;
    }
    return json_get_int(e);
}

/* does object `o` contain member `key`? (safe on an invalid/parse-failed json) */
int jhas(struct JSON* o, char* key)
{
    return json_is_object(o) && json_object_contains(o, key);
}

/* Escape `src` into `dst[size]` for a JSON string value: backslash and double
 * quote get a leading backslash; the few control chars a game title might carry
 * are folded to spaces so the body always stays valid JSON. Index-only (picoc
 * rejects pointer arithmetic on arrays). */
void json_escape(char* src, char* dst, int size)
{
    int i = 0;
    int j = 0;
    while (src[i] != '\0' && j < size - 2) {
        char c = src[i];
        if (c == '\\' || c == '"') {
            dst[j] = '\\';
            j = j + 1;
            dst[j] = c;
            j = j + 1;
        }
        else if (c == '\n' || c == '\r' || c == '\t') {
            dst[j] = ' ';
            j = j + 1;
        }
        else {
            dst[j] = c;
            j = j + 1;
        }
        i = i + 1;
    }
    dst[j] = '\0';
}

/* Escape `src` into `dst[size]` for a Drive query single-quoted literal:
 * backslash and single quote get a leading backslash (so a title like
 * "Luigi's Mansion" can't break the q= string). Index-only. */
void q_escape(char* src, char* dst, int size)
{
    int i = 0;
    int j = 0;
    while (src[i] != '\0' && j < size - 2) {
        char c = src[i];
        if (c == '\\' || c == '\'') {
            dst[j] = '\\';
            j = j + 1;
        }
        dst[j] = c;
        j = j + 1;
        i = i + 1;
    }
    dst[j] = '\0';
}

/* Copy the final path component of `full` (everything after the last '/') into
 * dst[size]. read_directory returns full paths, so this recovers the backup
 * folder's own name. Index-only. */
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

/* Copy `src` into `dst[size]` without a leading scheme / "www." so the long
 * "https://www.google.com/device" shows as the short "google.com/device" — which
 * Google still honours (it redirects). Keeps the verification URL inside the
 * message card, whose text wraps only on spaces and would otherwise clip a long
 * space-less URL. Index-only. */
void shorten_url(char* src, char* dst, int size)
{
    int off = 0;
    if (strncmp(src, "https://", 8) == 0) {
        off = 8;
    }
    else if (strncmp(src, "http://", 7) == 0) {
        off = 7;
    }
    if (src[off] == 'w' && src[off + 1] == 'w' && src[off + 2] == 'w' && src[off + 3] == '.') {
        off = off + 4;
    }
    int i = 0;
    while (src[off + i] != '\0' && i < size - 1) {
        dst[i] = src[off + i];
        i = i + 1;
    }
    dst[i] = '\0';
    if (dst[0] == '\0') { /* nothing left after trimming → fall back to the raw url */
        strncpy(dst, src, size - 1);
        dst[size - 1] = '\0';
    }
}

/* POST an x-www-form-urlencoded body and return the parsed JSON reply (caller
 * json_delete's). On a network failure the reply is an empty/invalid object, so
 * every jhas()/jget() below simply reports "field not present" — no crash. */
struct JSON* post_form(char* url, char* form)
{
    char* out = NULL;
    char* rh  = NULL;
    int n     = 0;
    web_request("POST", url, "Content-Type: application/x-www-form-urlencoded",
        form, strlen(form), &out, &n, &rh);
    struct JSON* j = json_new();
    if (out != NULL) {
        json_parse(j, out);
        free(out);
    }
    if (rh != NULL) {
        free(rh);
    }
    return j;
}

/* ---- auth --------------------------------------------------------------- */

/* Load client_id / client_secret from client_secret.json. Google nests them
 * under "installed"; some exports are flat, so fall back to the root. */
int load_client_secret(void)
{
    g_id[0]     = '\0';
    g_secret[0] = '\0';

    char* raw = slurp(g_secret_path);
    if (raw == NULL) {
        script_log("no client_secret.json — see the setup guide");
        gui_message("Missing config/client_secret.json.\nCreate a Google OAuth client of type\n\"TVs and Limited Input devices\" and copy its\nJSON to config/client_secret.json under the\nCheckpoint folder.");
        return 0;
    }
    struct JSON* root = json_new();
    json_parse(root, raw);
    free(raw);
    if (!json_is_valid(root) || !json_is_object(root)) {
        json_delete(root);
        script_log("client_secret.json is not valid JSON");
        gui_message("client_secret.json is not valid JSON.");
        return 0;
    }
    struct JSON* ins = root;
    if (jhas(root, "installed")) {
        struct JSON* e = json_object_element(root, "installed");
        if (json_is_object(e)) {
            ins = e;
        }
    }
    jget(ins, "client_id", g_id, IDN);
    jget(ins, "client_secret", g_secret, SECN);
    json_delete(root);

    if (g_id[0] == '\0') {
        script_log("client_secret.json has no client_id");
        gui_message("client_secret.json has no client_id.");
        return 0;
    }
    script_log("client secret loaded");
    return 1;
}

/* Exchange a stored refresh token for a fresh access token. 1 on success. */
int refresh_token(char* rtoken)
{
    char form[4096]; /* holds id + secret + a (possibly long) refresh token */
    sprintf(form,
        "client_id=%s&client_secret=%s&refresh_token=%s&grant_type=refresh_token",
        g_id, g_secret, rtoken);
    struct JSON* j = post_form(TOKEN_URL, form);
    int ok         = jhas(j, "access_token");
    if (ok) {
        jget(j, "access_token", g_access, TOKN);
    }
    json_delete(j);
    return ok;
}

/* First-run sign-in via the OAuth device flow. 1 on success (and the refresh
 * token is written to gdrive_token.json for silent re-auth next time). */
int device_flow(void)
{
    char form[1024];
    sprintf(form, "client_id=%s&scope=%s", g_id, SCOPE_ENC);
    struct JSON* d = post_form(DEVICE_CODE_URL, form);
    if (!jhas(d, "device_code")) {
        json_delete(d);
        script_log("device-code request returned no device_code");
        gui_message("Could not start Google sign-in.\nCheck Wi-Fi and that the Drive API is\nenabled for your Google project.");
        return 0;
    }

    char dcode[512];
    char ucode[64];
    char vurl[256];
    jget(d, "device_code", dcode, 512);
    jget(d, "user_code", ucode, 64);
    /* Google names it verification_url in some responses, verification_uri in
     * others — read whichever is present. */
    if (jhas(d, "verification_url")) {
        jget(d, "verification_url", vurl, 256);
    }
    else {
        jget(d, "verification_uri", vurl, 256);
    }
    if (vurl[0] == '\0') {
        sprintf(vurl, "google.com/device");
    }
    int interval = jget_int(d, "interval", 5);
    int expires  = jget_int(d, "expires_in", 900);
    json_delete(d);
    if (interval < 5) {
        interval = 5; /* keep hold-B cancel latency low: sleep() isn't sampled */
    }
    if (expires > 1800) {
        expires = 1800;
    }

    /* Short display form of the URL so it fits the message card (which wraps
     * only on spaces and would clip the full https://www... string). */
    char vshort[128];
    shorten_url(vurl, vshort, 128);

    /* Every line stays short so StringUtils::wrap keeps this exact layout
     * instead of re-flowing (or clipping) a too-wide line. */
    char intro[700];
    sprintf(intro, "Sign in to Google Drive:\n1. On a phone or PC, open:\n%s\n2. Enter this code:\n%s\nKeep this open while it waits.", vshort, ucode);

    /* The log pane keeps both of these on screen for the whole poll, so the URL
     * and the code are still readable after the card is dismissed. */
    char logline[320];
    sprintf(logline, "sign-in url: %s", vurl);
    script_log(logline);
    sprintf(logline, "sign-in code: %s", ucode);
    script_log(logline);

    gui_message(intro);

    /* The status footer is a single cramped line shared with the script name
     * and cancel hint, so keep it to a short reminder of the code (the modal
     * above already carried the full instructions). */
    char status[128];
    sprintf(status, "code %s  at  %s", ucode, vshort);
    gui_status(status);

    char pollform[3072];
    int waited = 0;
    while (waited < expires) {
        sleep(interval);
        waited = waited + interval;

        sprintf(pollform, "client_id=%s&client_secret=%s&device_code=%s&grant_type=urn:ietf:params:oauth:grant-type:device_code", g_id, g_secret, dcode);
        struct JSON* t = post_form(TOKEN_URL, pollform);

        if (jhas(t, "access_token")) {
            jget(t, "access_token", g_access, TOKN);
            char rtok[TOKN];
            jget(t, "refresh_token", rtok, TOKN);
            json_delete(t);

            sd_mkdirs(g_cfg_dir);
            FILE* f = fopen(g_token_path, "w");
            if (f != NULL) {
                fprintf(f, "{\"refresh_token\":\"%s\"}", rtok);
                fclose(f);
                script_log("signed in; refresh token saved");
            }
            else {
                script_log("signed in, but the token file could not be written");
            }
            gui_message("Signed in to Google Drive.");
            return 1;
        }

        char err[64];
        jget(t, "error", err, 64);
        json_delete(t);
        if (strcmp(err, "authorization_pending") == 0) {
            /* not authorized yet — keep polling. One line per poll would bury
             * the url and the code the user still needs to read. */
        }
        else if (strcmp(err, "slow_down") == 0) {
            interval = interval + 5; /* Google asked us to back off */
            sprintf(logline, "google asked us to slow down; polling every %ds", interval);
            script_log(logline);
        }
        else {
            /* access_denied, expired_token, invalid_client, ... */
            char m[128];
            if (err[0] != '\0') {
                sprintf(m, "Google sign-in failed: %s", err);
            }
            else {
                sprintf(m, "Google sign-in failed.");
            }
            script_log(m);
            gui_message(m);
            return 0;
        }
    }
    sprintf(logline, "sign-in timed out after %ds", waited);
    script_log(logline);
    gui_message("Google sign-in timed out. Try again.");
    return 0;
}

/* Sign in: silent refresh if we have a stored token, else the device flow. */
int ensure_signed_in(void)
{
    if (!load_client_secret()) {
        return 0;
    }
    char* tj = slurp(g_token_path);
    if (tj != NULL) {
        struct JSON* j = json_new();
        json_parse(j, tj);
        free(tj);
        int ok = 0;
        if (jhas(j, "refresh_token")) {
            char rtok[TOKN];
            jget(j, "refresh_token", rtok, TOKN);
            gui_status("Signing in to Google Drive...");
            script_log("refreshing the stored access token");
            ok = refresh_token(rtok);
        }
        json_delete(j);
        if (ok) {
            script_log("signed in (stored token)");
            return 1; /* silent re-auth worked */
        }
        /* refresh failed (revoked / rotated out) → fall through to device flow */
        script_log("stored token rejected — signing in again");
    }
    return device_flow();
}

/* ---- drive -------------------------------------------------------------- */

void build_auth_headers(void)
{
    sprintf(g_auth, "Authorization: Bearer %s", g_access);
}

/* Run a Drive files.list query and return the first files[0].id as a malloc'd
 * string ("" if none / on error); the caller free()s it. `query` is the raw
 * (un-encoded) Drive q-string; this url-encodes it. */
char* drive_query_id(char* query)
{
    char url[2600];
    char* out = NULL;
    char* rh  = NULL;
    int n     = 0;

    char* qe = url_encode(query);
    sprintf(url, "%s?q=%s&fields=files(id)&spaces=drive", FILES_URL, qe);
    free(qe);

    web_request("GET", url, g_auth, "", 0, &out, &n, &rh);
    if (rh != NULL) {
        free(rh);
    }
    struct JSON* j = json_new();
    if (out != NULL) {
        json_parse(j, out);
        free(out);
    }
    char id[256];
    id[0] = '\0';
    if (jhas(j, "files")) {
        struct JSON* files = json_object_element(j, "files");
        if (json_is_array(files) && json_array_size(files) > 0) {
            struct JSON* f0 = json_array_element(files, 0);
            jget(f0, "id", id, 256);
        }
    }
    json_delete(j);
    return strdup(id);
}

/* find-or-create a folder named `name` under `parentId` ("root" for the Drive
 * root). Returns a malloc'd id, or a malloc'd "" on failure — always non-NULL;
 * the caller free()s it. */
char* ensure_folder(char* name, char* parentId)
{
    char qname[512];
    q_escape(name, qname, 512);
    char q[768];
    sprintf(q, "name='%s' and mimeType='application/vnd.google-apps.folder' and '%s' in parents and trashed=false", qname, parentId);
    char* id = drive_query_id(q);
    if (id[0] != '\0') {
        return id;
    }
    free(id);

    /* not found → create it */
    char jname[512];
    json_escape(name, jname, 512);
    char body[700];
    sprintf(body, "{\"name\":\"%s\",\"mimeType\":\"application/vnd.google-apps.folder\",\"parents\":[\"%s\"]}", jname, parentId);
    char h[AUTHN + 40];
    sprintf(h, "%s\nContent-Type: application/json", g_auth);

    char* out = NULL;
    char* rh  = NULL;
    int n     = 0;
    web_request("POST", FILES_URL, h, body, strlen(body), &out, &n, &rh);
    if (rh != NULL) {
        free(rh);
    }
    struct JSON* c = json_new();
    if (out != NULL) {
        json_parse(c, out);
        free(out);
    }
    char cid[256];
    jget(c, "id", cid, 256);
    json_delete(c);

    /* only the create path logs: "found it" is the common case and would drown
     * the transcript in one line per title */
    char line[576];
    if (cid[0] != '\0') {
        sprintf(line, "drive: created folder \"%s\"", name);
    }
    else {
        sprintf(line, "drive: could not create folder \"%s\"", name);
    }
    script_log(line);
    return strdup(cid);
}

/* id of a file named `name` directly under `folderId`, or malloc'd "" if none */
char* find_existing_file(char* name, char* folderId)
{
    char qname[512];
    q_escape(name, qname, 512);
    char q[768];
    sprintf(q, "name='%s' and '%s' in parents and trashed=false", qname, folderId);
    return drive_query_id(q);
}

/* Upload local `zipPath` into `folderId` as `zipName` with a resumable session:
 * PATCH an existing file of the same name in place, else POST a new one, then
 * PUT the bytes (streamed from SD). 1 on success. */
int upload_zip(char* zipPath, char* zipName, char* folderId)
{
    char* existing = find_existing_file(zipName, folderId);

    char h[AUTHN + 128];
    sprintf(h, "%s\nContent-Type: application/json; charset=UTF-8\nX-Upload-Content-Type: application/zip", g_auth);

    char url[2600];
    char body[700];
    char* out = NULL;
    char* rh  = NULL;
    int n     = 0;

    if (existing[0] != '\0') {
        /* update in place — metadata stays as-is, so an empty JSON body */
        script_log("  replacing the copy already on Drive");
        sprintf(url, "%s/%s?uploadType=resumable", UPLOAD_URL, existing);
        sprintf(body, "{}");
        web_request("PATCH", url, h, body, strlen(body), &out, &n, &rh);
    }
    else {
        char jname[512];
        json_escape(zipName, jname, 512);
        sprintf(url, "%s?uploadType=resumable", UPLOAD_URL);
        sprintf(body, "{\"name\":\"%s\",\"parents\":[\"%s\"]}", jname, folderId);
        web_request("POST", url, h, body, strlen(body), &out, &n, &rh);
    }
    free(existing);
    if (out != NULL) {
        free(out);
    }

    /* the resumable session URI comes back as a Location response header */
    char loc[2048];
    loc[0] = '\0';
    if (rh != NULL) {
        char* lv = http_header_value(rh, "Location");
        if (lv != NULL) {
            strncpy(loc, lv, sizeof loc - 1);
            loc[sizeof loc - 1] = '\0';
            free(lv);
        }
        free(rh);
    }
    if (loc[0] == '\0') {
        script_log("  no resumable upload session (token expired? quota?)");
        return 0;
    }

    out = NULL;
    rh  = NULL;
    n   = 0;
    int put = web_upload_file("PUT", loc, "Content-Type: application/zip", zipPath, &out, &n, &rh);
    if (out != NULL) {
        free(out);
    }
    if (rh != NULL) {
        free(rh);
    }
    int ok = (put == 200 || put == 201);
    if (!ok) {
        char line[96];
        /* negative = transport error, positive = the HTTP status Drive sent */
        sprintf(line, "  upload rejected (code %d)", put);
        script_log(line);
    }
    return ok;
}

/* Size of an SD file in bytes, or -1. Used only to log what was zipped. */
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

/* Zip one backup folder and upload it under the title's Drive folder. 1 ok.
 *
 * Progress contract: the caller owns the item bar (layer 1) and only its label
 * mentions the title/backup. zip_dir and web_upload_file drive the reserved
 * innermost bar themselves and label it by phase ("zip", "upload"), so nothing
 * here restates what a bar above it already says. */
int sync_backup(char* titleName, char* backupPath, char* backupName, char* rootId)
{
    char st[512];
    sprintf(st, "%s / %s", titleName, backupName);
    gui_status(st);

    sprintf(st, "> %s / %s", titleName, backupName);
    script_log(st);

    if (zip_dir(backupPath, g_tmp_zip) != 0) {
        remove(g_tmp_zip);
        script_log("  zip failed (out of space, or cancelled)");
        return 0;
    }
    int zkb = 0;
    int zbytes = file_size(g_tmp_zip);
    if (zbytes > 0) {
        zkb = zbytes / 1024;
    }
    sprintf(st, "  zipped %d KB", zkb);
    script_log(st);

    char* folderId = ensure_folder(titleName, rootId);
    int ok         = 0;
    if (folderId[0] == '\0') {
        script_log("  could not create the title's Drive folder");
    }
    else {
        char zipName[320];
        sprintf(zipName, "%s.zip", backupName);
        ok = upload_zip(g_tmp_zip, zipName, folderId);
    }
    free(folderId);
    remove(g_tmp_zip);

    if (ok) {
        script_log("  uploaded");
    }
    else {
        script_log("  UPLOAD FAILED");
    }
    return ok;
}

/* Upload every backup folder inside a title's save-backup dir. `lvl` is the
 * bar depth this call owns: 1 under the "Titles" bar of an all-titles run, 0
 * when this title is the whole job, so an unused outer slot is never drawn. */
int sync_title(int idx, char* rootId, int* done, int* failed, int lvl)
{
    char* base = title_backup_path(idx, 0); /* trailing slash */
    char* name = title_name(idx);
    struct directory* d = read_directory(base);
    if (d == NULL) {
        free(base);
        free(name);
        return 0;
    }
    char st[512];
    sprintf(st, "== %s (%d backup(s))", name, d->count);
    script_log(st);

    int i;
    progress_begin(lvl, name, d->count);
    for (i = 0; i < d->count; i++) {
        char bname[256];
        basename_of(d->files[i], bname, 256);
        /* The item bar names the backup being worked on; its count is the
         * position within this title. The phase bar under it stays "zip" /
         * "upload", so no two bars ever say the same thing. */
        sprintf(st, "%s: %s", name, bname);
        progress_label(lvl, st);
        if (sync_backup(name, d->files[i], bname, rootId)) {
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
    delete_directory(d);
    free(base);
    free(name);
    return 1;
}

/* Does title `idx` have at least one backup folder on SD? */
int title_has_backups(int idx)
{
    char* base = title_backup_path(idx, 0);
    struct directory* d = read_directory(base);
    int has = (d != NULL && d->count > 0);
    if (d != NULL) {
        delete_directory(d);
    }
    free(base);
    return has;
}

/* "One backup": pick a title that has backups, then pick one of its backups. */
void sync_one_backup(char* rootId, int* done, int* failed)
{
    int total = titles_count();
    int idxs[MAX_PICK];
    char* names[MAX_PICK];
    int n = 0;
    int i;
    for (i = 0; i < total && n < MAX_PICK; i++) {
        if (title_has_save(i) && title_has_backups(i)) {
            idxs[n]  = i;
            names[n] = title_name(i); /* malloc'd */
            n++;
        }
    }
    if (n == 0) {
        script_log("no title on this console has a backup to upload");
        gui_message("No backups found on the SD card to upload.");
        return;
    }

    int tpick = gui_pick_one("Pick a title", names, n);
    if (tpick >= 0) {
        int idx    = idxs[tpick];
        char* base = title_backup_path(idx, 0);
        struct directory* d = read_directory(base);
        if (d != NULL && d->count > 0) {
            char* bnames[MAX_PICK];
            int bn = d->count < MAX_PICK ? d->count : MAX_PICK;
            int k;
            for (k = 0; k < bn; k++) {
                char tmp[256];
                basename_of(d->files[k], tmp, 256);
                bnames[k] = strdup(tmp);
            }
            int bpick = gui_pick_one("Pick a backup", bnames, bn);
            if (bpick >= 0) {
                /* One item, but still an item bar: without it the only bar on
                 * screen would be the phase bar, with nothing naming what is
                 * being uploaded. */
                char st[512];
                sprintf(st, "%s: %s", names[tpick], bnames[bpick]);
                progress_begin(0, st, 1);
                if (sync_backup(names[tpick], d->files[bpick], bnames[bpick], rootId)) {
                    *done = *done + 1;
                }
                else {
                    *failed = *failed + 1;
                }
                progress_set(0, 1);
                progress_end(0);
            }
            for (k = 0; k < bn; k++) {
                free(bnames[k]);
            }
        }
        if (d != NULL) {
            delete_directory(d);
        }
        free(base);
    }

    for (i = 0; i < n; i++) {
        free(names[i]);
    }
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char** argv)
{
    init_paths();

    if (!ensure_signed_in()) {
        return 1;
    }
    build_auth_headers();

    gui_status("Preparing the Drive folder...");
    script_log("preparing Checkpoint/<console> on Drive");
    char* cpId = ensure_folder("Checkpoint", "root");
    if (cpId[0] == '\0') {
        gui_message("Signed in, but could not create the\n\"Checkpoint\" folder in Google Drive.");
        free(cpId);
        return 1;
    }

    /* every console keeps its backups in its own subfolder, so a 3DS and a
     * Switch syncing to the same account never mix titles up */
    char* rootId = ensure_folder(g_platform, cpId);
    free(cpId);
    if (rootId[0] == '\0') {
        char msg[128];
        sprintf(msg, "Signed in, but could not create the\n\"Checkpoint/%s\" folder in Google Drive.", g_platform);
        gui_message(msg);
        free(rootId);
        return 1;
    }

    /* selected_title() / argv[0] carry the highlighted title's id (empty if
     * none). Offer "This title" only when there is one. */
    char* selId = (argc > 0) ? argv[0] : "";
    int haveSel = selId != NULL && selId[0] != '\0';

    char* opts[3];
    int nopts        = 0;
    opts[nopts++]    = "All titles";
    int thisTitleRow = -1;
    if (haveSel) {
        thisTitleRow  = nopts;
        opts[nopts++] = "This title";
    }
    int oneBackupRow = nopts;
    opts[nopts++]    = "One backup...";

    int choice = gui_pick_one("Sync to Google Drive", opts, nopts);
    if (choice < 0) {
        script_log("cancelled at the mode picker");
        free(rootId);
        return 0;
    }
    char pick[64];
    sprintf(pick, "mode: %s", opts[choice]);
    script_log(pick);

    int done   = 0;
    int failed = 0;

    if (choice == 0) {
        /* All titles. Counted up front so the outer bar has a real total
         * instead of crawling towards a number that keeps moving. */
        int total = titles_count();
        int i;
        int eligible = 0;
        for (i = 0; i < total; i++) {
            if (title_has_save(i) && title_has_backups(i)) {
                eligible = eligible + 1;
            }
        }
        char line[96];
        sprintf(line, "all titles: %d of %d have backups to upload", eligible, total);
        script_log(line);

        progress_begin(0, "Titles", eligible);
        int seen = 0;
        for (i = 0; i < total; i++) {
            if (title_has_save(i) && title_has_backups(i)) {
                sync_title(i, rootId, &done, &failed, 1);
                seen = seen + 1;
                progress_set(0, seen);
            }
        }
        progress_end(0);
    }
    else if (choice == thisTitleRow) {
        int idx = title_find(selId);
        if (idx >= 0) {
            sync_title(idx, rootId, &done, &failed, 0);
        }
        else {
            script_log("the selected title is not in the catalog");
            gui_message("The selected title was not found.");
        }
    }
    else if (choice == oneBackupRow) {
        sync_one_backup(rootId, &done, &failed);
    }

    free(rootId);

    progress_clear();

    char summary[128];
    sprintf(summary, "Done. Uploaded %d, failed %d.", done, failed);
    script_log(summary);
    gui_message(summary);
    return failed ? 1 : 0;
}
