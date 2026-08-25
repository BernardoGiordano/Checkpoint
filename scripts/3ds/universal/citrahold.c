/*
 * citrahold.c — Citrahold-compatible save and extdata sync for Checkpoint.
 *
 * Install on a 3DS at /3ds/Checkpoint/scripts/universal/citrahold.c.
 * When bundled with Checkpoint, setup instructions live in scripts/citrahold.md.
 * The first run selects Official or Custom mode. The selection is persisted;
 * Configuration is the place to change modes later.
 *
 * State and temporary request files are stored under app_root()/config.
 * A passphrase is optional, but device_seal encrypts the state in either case.
 * Debug logging is disabled by default and never records credentials or save
 * contents.
 *
 * Upload payloads are written to a temporary SD-card file and streamed through
 * Checkpoint's native HTTP helper. Downloads are enumerated and then received
 * file-by-file with HTTP ranges, keeping Picoc response allocations bounded.
 * Temporary upload files are unlinked on startup and after each request;
 * incomplete download folders are removed before returning to the menu.
 */

#include <checkpoint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define URLN 512
#define TOKENN 96
#define PASSN 64
#define BODYN 24000
#define DOWNLOAD_RESPONSE_LIMIT 65536
#define DOWNLOAD_CHUNK 524288
#define B64_CHUNK 1536
#define UPLOAD_ERR_READ 1
#define UPLOAD_ERR_TEMP 2
#define UPLOAD_STATUS_CLEANUP -4
#define PATHN 1024
#define IDN 128
#define TITLEIDN 17
#define MAXMAP 16
#define MAXREMOTE 32
#define MAXPICK 64
#define STATE_TEXT_LIMIT 16384
#define ROWN 256
#define CONFIRMN (PATHN + IDN + 256)

#define OFFICIAL_URL "https://api.citrahold.com"
#define SCRIPT_VERSION "review-cleanup"

struct mapping {
    int type;
    char title[TITLEIDN];
    char game[IDN];
};

struct remote_cache {
    char games[MAXREMOTE][IDN];
    int count;
};

char g_config_dir[PATHN];
char g_vault[PATHN];
char g_log_dir[PATHN];
char g_log_path[PATHN];
char g_mode[16];
char g_url[URLN];
char g_official_token[TOKENN];
char g_custom_token[TOKENN];
char g_pass[PASSN];
int g_delete_after;
int g_debug;

struct mapping g_maps[MAXMAP];
int g_map_count;
struct remote_cache g_remote_save;
struct remote_cache g_remote_extdata;

char g_upload_payload[PATHN];
char g_vault_tmp[PATHN];
char g_vault_old[PATHN];
int g_upload_error;
int g_upload_total;
int g_upload_done;

struct remote_cache* remote_cache_for_type(int type)
{
    if (type == 0) return &g_remote_save;
    return &g_remote_extdata;
}

void log_debug(char* message)
{
    if (g_debug) {
        FILE* f;
        sd_mkdirs(g_log_dir);
        f = fopen(g_log_path, "ab");
        if (f != NULL) {
            fputs(message, f);
            fputc('\n', f);
            fclose(f);
        }
    }
    script_log(message);
}

void init_paths(void)
{
    char* root = app_root();
    snprintf(g_config_dir, PATHN, "%s/config", root);
    snprintf(g_vault, PATHN, "%s/config/citrahold.vault", root);
    snprintf(g_log_dir, PATHN, "%s/logs/citrahold", root);
    snprintf(g_log_path, PATHN, "%s/logs/citrahold/citrahold.log", root);
    snprintf(g_upload_payload, PATHN, "%s/config/citrahold-upload-payload.json", root);
    snprintf(g_vault_tmp, PATHN, "%s/config/citrahold.vault.tmp", root);
    snprintf(g_vault_old, PATHN, "%s/config/citrahold.vault.old", root);
    sd_mkdirs(g_log_dir);
    free(root);
    /* A power loss can leave the streamed request body behind. Never retain it. */
    unlink(g_upload_payload);
    /* 3DS rename() does not replace an existing entry; recover an interrupted swap. */
    if (!sd_exists(g_vault) && sd_exists(g_vault_old)) rename(g_vault_old, g_vault);
    if (sd_exists(g_vault)) unlink(g_vault_old);
    unlink(g_vault_tmp);
    g_mode[0] = '\0';
    g_url[0] = '\0';
    g_official_token[0] = '\0';
    g_custom_token[0] = '\0';
    g_pass[0] = '\0';
    g_delete_after = 0;
    g_debug = 0;
    g_map_count = 0;
    g_remote_save.count = 0;
    g_remote_extdata.count = 0;
}

char* slurp_n(char* path, int* size)
{
    FILE* f;
    int bytes;
    int got;
    char* data;

    size[0] = 0;
    f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (bytes <= 0 || bytes > BODYN) {
        fclose(f);
        return NULL;
    }
    data = malloc(bytes + 1);
    if (data == NULL) {
        fclose(f);
        return NULL;
    }
    got = fread(data, 1, bytes, f);
    fclose(f);
    data[got] = '\0';
    size[0] = got;
    return data;
}

int append_text(char* dst, char* src, int limit)
{
    int at = strlen(dst);
    int i = 0;
    while (src[i] != '\0') {
        if (at + 1 >= limit) return 0;
        dst[at] = src[i];
        at = at + 1;
        i = i + 1;
    }
    dst[at] = '\0';
    return 1;
}

int append_json_string(char* dst, char* value, int limit)
{
    int at = strlen(dst);
    int i = 0;
    while (value[i] != '\0') {
        char c = value[i];
        if (c == '\\' || c == '"') {
            if (at + 2 >= limit) return 0;
            dst[at] = '\\';
            dst[at + 1] = c;
            at = at + 2;
        }
        else {
            if (at + 1 >= limit) return 0;
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            dst[at] = c;
            at = at + 1;
        }
        i = i + 1;
    }
    dst[at] = '\0';
    return 1;
}

int line_value(char* text, char* key, char* out, int outn)
{
    char* p = text;
    int keyn = strlen(key);
    int i;
    while (p != NULL && p[0] != '\0') {
        if (strncmp(p, key, keyn) == 0 && p[keyn] == '=') {
            p = &p[keyn + 1];
            i = 0;
            while (p[i] != '\0' && p[i] != '\n' && i < outn - 1) {
                out[i] = p[i];
                i = i + 1;
            }
            out[i] = '\0';
            return 1;
        }
        p = strchr(p, '\n');
        if (p != NULL) p = &p[1];
    }
    out[0] = '\0';
    return 0;
}

void parse_maps(char* text)
{
    char* p = text;
    char line[PATHN];
    char* a;
    char* b;
    char* c;
    int n;

    g_map_count = 0;
    while (p != NULL && g_map_count < MAXMAP) {
        p = strstr(p, "map=");
        if (p == NULL) break;
        n = 0;
        while (p[n] != '\0' && p[n] != '\n' && n < PATHN - 1) {
            line[n] = p[n];
            n = n + 1;
        }
        line[n] = '\0';
        a = strtok(line, "|");
        b = strtok(NULL, "|");
        c = strtok(NULL, "|");
        if (a != NULL && b != NULL && c != NULL) {
            if (strncmp(a, "map=save", 8) == 0 || strncmp(a, "map=extdata", 11) == 0) {
                g_maps[g_map_count].type = (strncmp(a, "map=extdata", 11) == 0) ? 1 : 0;
                strncpy(g_maps[g_map_count].title, b, TITLEIDN - 1);
                g_maps[g_map_count].title[TITLEIDN - 1] = '\0';
                strncpy(g_maps[g_map_count].game, c, IDN - 1);
                g_maps[g_map_count].game[IDN - 1] = '\0';
                g_map_count = g_map_count + 1;
            }
        }
        p = strchr(p, '\n');
        if (p != NULL) p = &p[1];
    }
}

void parse_remote_games(char* text)
{
    char* p = text;
    char line[PATHN];
    char* a;
    char* b;
    struct remote_cache* cache;
    int n;

    g_remote_save.count = 0;
    g_remote_extdata.count = 0;
    while (p != NULL) {
        p = strstr(p, "remote=");
        if (p == NULL) break;
        n = 0;
        while (p[n] != '\0' && p[n] != '\n' && n < PATHN - 1) {
            line[n] = p[n];
            n = n + 1;
        }
        line[n] = '\0';
        a = strtok(line, "|");
        b = strtok(NULL, "|");
        if (a != NULL && b != NULL) {
            if (strcmp(a, "remote=save") == 0) {
                cache = remote_cache_for_type(0);
            }
            else if (strcmp(a, "remote=extdata") == 0) {
                cache = remote_cache_for_type(1);
            }
            else cache = NULL;
            if (cache != NULL && cache->count < MAXREMOTE) {
                strncpy(cache->games[cache->count], b, IDN - 1);
                cache->games[cache->count][IDN - 1] = '\0';
                cache->count = cache->count + 1;
            }
        }
        p = strchr(p, '\n');
        if (p != NULL) p = &p[1];
    }
}

int state_from_plain(char* plain)
{
    char value[URLN];
    if (!line_value(plain, "mode", g_mode, 16)) return 0;
    line_value(plain, "url", g_url, URLN);
    line_value(plain, "official_token", g_official_token, TOKENN);
    line_value(plain, "custom_token", g_custom_token, TOKENN);
    line_value(plain, "delete_after", value, 16);
    g_delete_after = (strcmp(value, "1") == 0);
    line_value(plain, "debug", value, 16);
    g_debug = (strcmp(value, "1") == 0);
    parse_maps(plain);
    parse_remote_games(plain);
    return 1;
}

int state_to_plain(char* plain, int limit)
{
    int i;
    char line[PATHN];
    plain[0] = '\0';
    if (!append_text(plain, "mode=", limit) || !append_text(plain, g_mode, limit) || !append_text(plain, "\nurl=", limit) || !append_json_string(plain, g_url, limit)) return 0;
    if (!append_text(plain, "\nofficial_token=", limit) || !append_json_string(plain, g_official_token, limit)) return 0;
    if (!append_text(plain, "\ncustom_token=", limit) || !append_json_string(plain, g_custom_token, limit)) return 0;
    sprintf(line, "\ndelete_after=%d\ndebug=%d\n", g_delete_after, g_debug);
    if (!append_text(plain, line, limit)) return 0;
    for (i = 0; i < g_map_count; i++) {
        sprintf(line, "map=%s|%s|%s\n", g_maps[i].type ? "extdata" : "save", g_maps[i].title, g_maps[i].game);
        if (!append_text(plain, line, limit)) return 0;
    }
    for (i = 0; i < g_remote_save.count; i++) {
        sprintf(line, "remote=save|%s\n", g_remote_save.games[i]);
        if (!append_text(plain, line, limit)) return 0;
    }
    for (i = 0; i < g_remote_extdata.count; i++) {
        sprintf(line, "remote=extdata|%s\n", g_remote_extdata.games[i]);
        if (!append_text(plain, line, limit)) return 0;
    }
    return 1;
}

void seal_error(int rc)
{
    char message[160];
    if (rc == -5) strcpy(message, "Wrong passphrase, wrong console, or damaged state.");
    else if (rc == -2) strcpy(message, "State was sealed by a newer Checkpoint.");
    else if (rc == -3) strcpy(message, "Not enough memory to open the state.");
    else if (rc == -4) strcpy(message, "The console has no available key source.");
    else strcpy(message, "Could not open encrypted Citrahold state.");
    script_log(message);
    gui_message(message);
}

int state_write(void)
{
    char* plain = NULL;
    char* blob = NULL;
    int blob_size = 0;
    int rc;
    int moved_old = 0;
    FILE* f;

    plain = malloc(STATE_TEXT_LIMIT);
    if (plain == NULL) {
        gui_message("Not enough memory to prepare encrypted state.");
        return 0;
    }
    if (!state_to_plain(plain, STATE_TEXT_LIMIT)) {
        free(plain);
        gui_message("Citrahold settings are too large.");
        return 0;
    }
    rc = device_seal(plain, strlen(plain), g_pass, &blob, &blob_size);
    free(plain);
    if (rc != 0) {
        seal_error(rc);
        return 0;
    }
    sd_mkdirs(g_config_dir);
    unlink(g_vault_tmp);
    f = fopen(g_vault_tmp, "wb");
    if (f == NULL) {
        free(blob);
        gui_message("Could not write the encrypted state.");
        return 0;
    }
    if (fwrite(blob, 1, blob_size, f) != blob_size) {
        fclose(f);
        unlink(g_vault_tmp);
        free(blob);
        gui_message("The encrypted state was written short.");
        return 0;
    }
    if (fclose(f) != 0) {
        unlink(g_vault_tmp);
        free(blob);
        gui_message("Could not commit the encrypted state.");
        return 0;
    }
    /* Keep the last good vault recoverable while installing the closed file. */
    if (sd_exists(g_vault_old) && unlink(g_vault_old) != 0) {
        unlink(g_vault_tmp);
        free(blob);
        gui_message("Could not prepare the encrypted state.");
        return 0;
    }
    if (sd_exists(g_vault)) {
        if (rename(g_vault, g_vault_old) != 0) {
            unlink(g_vault_tmp);
            free(blob);
            gui_message("Could not preserve the encrypted state.");
            return 0;
        }
        moved_old = 1;
    }
    if (rename(g_vault_tmp, g_vault) != 0) {
        if (moved_old) rename(g_vault_old, g_vault);
        unlink(g_vault_tmp);
        free(blob);
        gui_message("Could not commit the encrypted state.");
        return 0;
    }
    unlink(g_vault_old);
    free(blob);
    return 1;
}

/* Returns 1 when loaded, 0 when absent, -1 on failure, and -2 on cancellation. */
int state_read(void)
{
    char* blob;
    char* plain = NULL;
    int blob_size;
    int plain_size = 0;
    int needs;
    int rc;

    blob = slurp_n(g_vault, &blob_size);
    if (blob == NULL) return 0;
    needs = seal_needs_passphrase(blob, blob_size);
    if (needs < 0) {
        free(blob);
        gui_message("The Citrahold state is not a valid sealed file.");
        return -1;
    }
    if (needs) {
        gui_keyboard(g_pass, "Enter Citrahold passphrase", PASSN);
        if (g_pass[0] == '\0') {
            free(blob);
            return -2;
        }
    }
    rc = device_unseal(blob, blob_size, g_pass, &plain, &plain_size);
    free(blob);
    if (rc != 0 || plain == NULL) {
        seal_error(rc);
        return -1;
    }
    if (!state_from_plain(plain)) {
        free(plain);
        gui_message("The Citrahold state is missing its mode.");
        return -1;
    }
    free(plain);
    return 1;
}

int choose_mode(void)
{
    char* choices[2];
    int pick;
    choices[0] = "Official Citrahold";
    choices[1] = "Custom Citrahold server";
    pick = gui_pick_one("Select Citrahold server mode", choices, 2);
    if (pick < 0) return 0;
    if (pick == 0) {
        strcpy(g_mode, "official");
        strcpy(g_url, OFFICIAL_URL);
    }
    else {
        strcpy(g_mode, "custom");
        gui_keyboard(g_url, "Enter Citrahold server URL", URLN);
        if (g_url[0] == '\0') return 0;
        while (g_url[strlen(g_url) - 1] == '/') g_url[strlen(g_url) - 1] = '\0';
    }
    return 1;
}

int valid_game_id(char* game)
{
    int i = 0;
    if (game[0] == '\0' || strlen(game) >= IDN) return 0;
    while (game[i] != '\0') {
        if (game[i] == '/' || game[i] == '\\' || game[i] == '"' || game[i] == '\n' || game[i] == '\r' || game[i] == '|') return 0;
        if (game[i] == '.' && game[i + 1] == '.') return 0;
        i = i + 1;
    }
    return 1;
}

int valid_backup_name(char* name)
{
    int i = 0;
    int length = strlen(name);
    if (length <= 0 || length >= IDN) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    if (name[0] == ' ' || name[length - 1] == ' ' || name[length - 1] == '.') return 0;
    while (name[i] != '\0') {
        char c = name[i];
        if ((unsigned char)c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return 0;
        if (c == '.' && name[i + 1] == '.') return 0;
        i = i + 1;
    }
    return 1;
}

char* active_token(void)
{
    if (strcmp(g_mode, "official") == 0) return g_official_token;
    return g_custom_token;
}

char* active_url(void)
{
    if (strcmp(g_mode, "official") == 0) return OFFICIAL_URL;
    return g_url;
}

void api_request_setup(char* endpoint, char* url, int url_size, char* headers)
{
    snprintf(url, url_size, "%s%s", active_url(), endpoint);
    strcpy(headers, "Content-Type: application/json");
}

void api_request_add_headers(char* headers, char* extra_headers, int headers_size)
{
    if (extra_headers[0] != '\0') {
        strncat(headers, "\n", headers_size - strlen(headers) - 1);
        strncat(headers, extra_headers, headers_size - strlen(headers) - 1);
    }
}

void api_request_log(char* endpoint, int status)
{
    char line[96];
    sprintf(line, "HTTP %d %s", status, endpoint);
    script_log(line);
}

int api_call(char* endpoint, char* body, char** out, int* out_size)
{
    char url[URLN];
    char headers[128];
    char* response_headers = NULL;
    int status;
    api_request_setup(endpoint, url, URLN, headers);
    status = web_request("POST", url, headers, body, strlen(body), out, out_size, &response_headers);
    if (response_headers != NULL) free(response_headers);
    api_request_log(endpoint, status);
    return status;
}

int json_string_field(struct JSON* root, char* key, char* dst, int size)
{
    struct JSON* field;
    char* value;
    if (!json_object_contains(root, key)) return 0;
    field = json_object_element(root, key);
    if (!json_is_string(field)) return 0;
    value = json_get_string(field);
    strncpy(dst, value, size - 1);
    dst[size - 1] = '\0';
    free(value);
    return 1;
}

int verify_token(void)
{
    char body[512];
    char* out = NULL;
    int n = 0;
    int status;
    char user[IDN];
    gui_status("Verifying Citrahold token...");
    sprintf(body, "{\"token\":\"%s\"}", active_token());
    status = api_call("/getUserID", body, &out, &n);
    if (status != 200 || out == NULL) {
        if (out != NULL) free(out);
        gui_message("The Citrahold token was rejected.");
        return 0;
    }
    {
        struct JSON* root = json_new();
        json_parse(root, out);
        user[0] = '\0';
        if (!json_string_field(root, "userID", user, IDN)) {
            json_delete(root);
            free(out);
            gui_message("The server returned an invalid token response.");
            return 0;
        }
        json_delete(root);
    }
    free(out);
    {
        char line[180];
        sprintf(line, "Authenticated as %s", user);
        gui_message(line);
    }
    return 1;
}

/* Returns 1 on success, 0 on user cancellation, and -1 on operational failure. */
int configure_first_run(void)
{
    char token[TOKENN];
    if (!choose_mode()) return 0;
    gui_keyboard(token, "Full token or shorthand token", TOKENN);
    if (token[0] == '\0') return 0;
    if (strlen(token) < 16) {
        char url[URLN];
        char body[256];
        char* out = NULL;
        int out_size = 0;
        int status;
        struct JSON* root;
        char exchanged[TOKENN];
        sprintf(url, "%s/getToken", active_url());
        sprintf(body, "{\"shorthandToken\":\"%s\"}", token);
        gui_status("Exchanging Citrahold token...");
        status = web_request("POST", url, "Content-Type: application/json", body, strlen(body), &out, &out_size, NULL);
        if (status != 200 || out == NULL) {
            if (out != NULL) free(out);
            gui_message("The shorthand token was rejected.");
            return -1;
        }
        root = json_new();
        if (root == NULL) {
            free(out);
            gui_message("Could not allocate the token response.");
            return -1;
        }
        json_parse(root, out);
        exchanged[0] = '\0';
        if (!json_is_valid(root) || !json_is_object(root) || !json_string_field(root, "token", exchanged, TOKENN)) {
            json_delete(root);
            free(out);
            gui_message("The server returned an invalid token response.");
            return -1;
        }
        json_delete(root);
        free(out);
        strcpy(token, exchanged);
    }
    if (strcmp(g_mode, "official") == 0) strcpy(g_official_token, token);
    else strcpy(g_custom_token, token);
    if (!verify_token()) {
        if (strcmp(g_mode, "official") == 0) g_official_token[0] = '\0';
        else g_custom_token[0] = '\0';
        return -1;
    }
    if (gui_confirm("Add a passphrase? Without one, same-console homebrew can derive the vault key.")) {
        gui_keyboard(g_pass, "Enter passphrase", PASSN);
        if (g_pass[0] == '\0') return 0;
    }
    return state_write() ? 1 : -1;
}

/* Like api_call(), with extra newline-separated request headers. The caller
 * owns response_headers and must free it when it is non-NULL. */
int api_call_headers(char* endpoint, char* extra_headers, char* body, char** out, int* out_size, char** response_headers)
{
    char url[URLN];
    char headers[256];
    int status;
    api_request_setup(endpoint, url, URLN, headers);
    api_request_add_headers(headers, extra_headers, 256);
    status = web_request("POST", url, headers, body, strlen(body), out, out_size, response_headers);
    api_request_log(endpoint, status);
    return status;
}

int api_upload_file(char* endpoint, char* path, char** out, int* out_size)
{
    char url[URLN];
    char headers[128];
    char* response_headers = NULL;
    int cleanup;
    int status;
    api_request_setup(endpoint, url, URLN, headers);
    /*
     * Hold-B is observed before the next statement after this native call.
     * Keep unlink in the same expression so the plaintext is removed first.
     * Both conditional arms are deliberately identical: picoc has ?: but no
     * comma operator, and the condition sequences the upload before either arm.
    */
    cleanup = (status = web_upload_file("POST", url, headers, path, out, out_size, &response_headers)) ? unlink(path) : unlink(path);
    if (cleanup != 0) status = UPLOAD_STATUS_CLEANUP;
    if (response_headers != NULL) free(response_headers);
    api_request_log(endpoint, status);
    return status;
}

int server_test(void)
{
    char* out = NULL;
    int n = 0;
    int status;
    gui_status("Testing Citrahold server...");
    status = api_call("/areyouawake", "{}", &out, &n);
    if (out != NULL) free(out);
    if (status == 200) {
        gui_message("Citrahold server is online.");
        return 1;
    }
    gui_message("Citrahold server could not be reached.");
    return 0;
}

void show_server_info(void)
{
    char* out = NULL;
    int n = 0;
    int status;
    int i;
    struct JSON* root;
    struct JSON* list;
    struct JSON* item;
    char* text;

    gui_status("Reading Citrahold server info...");
    printf("Citrahold server: %s\n", active_url());
    log_debug("starting server information query");
    status = api_call("/softwareVersion", "{}", &out, &n);
    if (status != 200 || out == NULL) {
        log_debug("server information query failed");
        printf("Could not read Citrahold version or message of the day (HTTP %d).\n", status);
        if (out != NULL) free(out);
        return;
    }
    root = json_new();
    json_parse(root, out);
    if (json_is_valid(root) && json_object_contains(root, "3ds")) {
        list = json_object_element(root, "3ds");
        if (json_is_array(list) && json_array_size(list) > 0) {
            item = json_array_element(list, 0);
            if (json_is_string(item)) {
                text = json_get_string(item);
                printf("Server 3DS version: %s\n", text);
                free(text);
            }
        }
    }
    if (json_is_valid(root) && json_object_contains(root, "motd3ds")) {
        list = json_object_element(root, "motd3ds");
        if (json_is_array(list)) {
            for (i = 0; i < json_array_size(list); i++) {
                item = json_array_element(list, i);
                if (json_is_string(item)) {
                    text = json_get_string(item);
                    if (text[0] != '\0') printf("Message: %s\n", text);
                    free(text);
                }
            }
        }
    }
    json_delete(root);
    free(out);
}

void cache_remote_games(int type, char* names, int count)
{
    struct remote_cache* cache = remote_cache_for_type(type);
    int old_count = cache->count;
    int kept = 0;
    int i;
    int changed = 0;
    if (count > MAXREMOTE) {
        gui_message(type ? "The extdata Game ID cache is full; some IDs were not saved." : "The save Game ID cache is full; some IDs were not saved.");
    }
    for (i = 0; i < count && kept < MAXREMOTE; i++) {
        char* game = &names[i * IDN];
        if (game[0] != '\0' && valid_game_id(game)) {
            if (kept >= old_count || strcmp(cache->games[kept], game) != 0) changed = 1;
            kept = kept + 1;
        }
    }
    if (old_count != kept) changed = 1;
    cache->count = 0;
    for (i = 0; i < count && cache->count < MAXREMOTE; i++) {
        char* game = &names[i * IDN];
        if (game[0] != '\0' && valid_game_id(game)) {
            strcpy(cache->games[cache->count], game);
            cache->count = cache->count + 1;
        }
    }
    if (changed) state_write();
    printf("Cached %d remote %s Game ID%s.\n", kept, type ? "extdata" : "save", kept == 1 ? "" : "s");
}

/*
 * Keep the result buffer as a pointer parameter.  Picoc copies array-typed
 * parameters into a fixed 256-byte temporary buffer, so a MAXPICK x IDN
 * parameter asserts before this function can run.
 */
int remote_games(int type, char* names)
{
    char body[512];
    char* out = NULL;
    int n = 0;
    int status;
    int count = 0;
    int i;
    struct JSON* root;
    struct JSON* games;
    struct JSON* item;
    char endpoint[32];

    gui_status("Reading Citrahold Game IDs...");
    sprintf(body, "{\"token\":\"%s\"}", active_token());
    strcpy(endpoint, type ? "/getExtdata" : "/getSaves");
    log_debug("requesting remote game list");
    status = api_call(endpoint, body, &out, &n);
    log_debug("remote game list request returned");
    if (status != 200 || out == NULL || n < 0 || n > BODYN - 1) {
        if (out != NULL) free(out);
        return -1;
    }
    root = json_new();
    if (root == NULL) {
        free(out);
        return -1;
    }
    json_parse(root, out);
    log_debug("remote game list parsed");
    if (!json_is_valid(root) || !json_object_contains(root, "games")) {
        json_delete(root);
        free(out);
        return -1;
    }
    games = json_object_element(root, "games");
    if (games == NULL || !json_is_array(games)) {
        json_delete(root);
        free(out);
        return -1;
    }
    log_debug("remote game list array validated");
    count = json_array_size(games);
    if (count > MAXPICK) count = MAXPICK;
    for (i = 0; i < count; i++) {
        item = json_array_element(games, i);
        if (json_is_string(item)) {
            char* s = json_get_string(item);
            strncpy(&names[i * IDN], s, IDN - 1);
            names[i * IDN + IDN - 1] = '\0';
            free(s);
        }
        else {
            names[i * IDN] = '\0';
        }
    }
    printf("Remote %s Game IDs: %d\n", type ? "extdata" : "save", count);
    for (i = 0; i < count; i++) printf("  %s\n", &names[i * IDN]);
    cache_remote_games(type, names, count);
    json_delete(root);
    free(out);
    return count;
}

int remote_has(char* names, int count, char* game)
{
    int i;
    for (i = 0; i < count; i++) if (strcmp(&names[i * IDN], game) == 0) return 1;
    return 0;
}

int remote_timestamp(int type, char* game, char* result, int size)
{
    char body[512];
    char* out = NULL;
    int n = 0;
    int status;
    struct JSON* root;
    char endpoint[48];
    result[0] = '\0';
    gui_status("Reading remote backup time...");
    sprintf(body, "{\"token\":\"%s\",\"game\":\"%s\"}", active_token(), game);
    strcpy(endpoint, type ? "/getExtdataLastUpdated" : "/getSavesLastUpdated");
    status = api_call(endpoint, body, &out, &n);
    if (status != 200 || out == NULL) {
        if (out != NULL) free(out);
        return 0;
    }
    root = json_new();
    json_parse(root, out);
    if (!json_string_field(root, "lastModified", result, size)) {
        json_delete(root);
        free(out);
        return 0;
    }
    json_delete(root);
    free(out);
    return 1;
}

int write_text(FILE* out, char* text)
{
    if (fputs(text, out) < 0) {
        g_upload_error = UPLOAD_ERR_TEMP;
        return 0;
    }
    return 1;
}

int write_json_string(FILE* out, char* value)
{
    int i;
    char c;
    if (fputc('"', out) == EOF) return 0;
    for (i = 0; value[i] != '\0'; i++) {
        c = value[i];
        if (c == '"' || c == '\\') {
            if (fputc('\\', out) == EOF || fputc(c, out) == EOF) return 0;
        }
        else if (c == '\n') {
            if (!write_text(out, "\\n")) return 0;
        }
        else if (c == '\r') {
            if (!write_text(out, "\\r")) return 0;
        }
        else if (c == '\t') {
            if (!write_text(out, "\\t")) return 0;
        }
        else if ((unsigned char)c < 32 || fputc(c, out) == EOF) return 0;
    }
    return fputc('"', out) != EOF;
}

int write_base64_file(FILE* out, char* path)
{
    FILE* in;
    unsigned char* raw;
    char* encoded;
    int read;
    int raw_pos;
    int encoded_pos;
    int a;
    int b;
    int c;
    in = fopen(path, "rb");
    if (in == NULL) {
        g_upload_error = UPLOAD_ERR_READ;
        printf("Upload could not read: %s\n", path);
        return 0;
    }
    raw = malloc(B64_CHUNK);
    encoded = malloc(B64_CHUNK / 3 * 4);
    if (raw == NULL || encoded == NULL) {
        if (raw != NULL) free(raw);
        if (encoded != NULL) free(encoded);
        fclose(in);
        g_upload_error = UPLOAD_ERR_TEMP;
        return 0;
    }
    while ((read = fread(raw, 1, B64_CHUNK, in)) > 0) {
        raw_pos = 0;
        encoded_pos = 0;
        while (raw_pos < read) {
            a = raw[raw_pos++];
            b = raw_pos < read ? raw[raw_pos++] : -1;
            c = raw_pos < read ? raw[raw_pos++] : -1;
            encoded[encoded_pos++] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(a >> 2) & 63];
            encoded[encoded_pos++] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((a & 3) << 4) | ((b < 0 ? 0 : b) >> 4)];
            encoded[encoded_pos++] = (b < 0) ? '=' : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((b & 15) << 2) | ((c < 0 ? 0 : c) >> 6)];
            encoded[encoded_pos++] = (c < 0) ? '=' : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[c & 63];
        }
        if (fwrite(encoded, 1, encoded_pos, out) != encoded_pos) {
            g_upload_error = UPLOAD_ERR_TEMP;
            free(raw);
            free(encoded);
            fclose(in);
            return 0;
        }
        g_upload_done = g_upload_done + read;
        progress_set(0, g_upload_done);
    }
    if (ferror(in)) {
        g_upload_error = UPLOAD_ERR_READ;
        free(raw);
        free(encoded);
        fclose(in);
        return 0;
    }
    free(raw);
    free(encoded);
    fclose(in);
    return 1;
}

int is_regular_file(char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

int measure_upload_tree(char* root, int* total)
{
    struct directory* d;
    FILE* f;
    int i;
    int size;
    char child[PATHN];
    if (is_regular_file(root)) {
        f = fopen(root, "rb");
        if (f == NULL || fseek(f, 0, SEEK_END) != 0) {
            if (f != NULL) fclose(f);
            g_upload_error = UPLOAD_ERR_READ;
            return 0;
        }
        size = ftell(f);
        fclose(f);
        if (size < 0 || size > 2147483647 - *total) {
            g_upload_error = UPLOAD_ERR_READ;
            return 0;
        }
        *total = *total + size;
        return 1;
    }
    d = read_directory(root);
    if (d == NULL) {
        g_upload_error = UPLOAD_ERR_READ;
        return 0;
    }
    for (i = 0; i < d->count; i++) {
        strncpy(child, d->files[i], PATHN - 1);
        child[PATHN - 1] = '\0';
        if (!measure_upload_tree(child, total)) {
            delete_directory(d);
            return 0;
        }
    }
    delete_directory(d);
    return 1;
}

int write_upload_entry(FILE* out, char* remote_path, char* local_path, int dummy)
{
    if (!write_text(out, "[") || !write_json_string(out, remote_path) || !write_text(out, ",")) return 0;
    if (dummy) {
        if (!write_json_string(out, "citraholdDirectoryDummy")) return 0;
    }
    else {
        if (fputc('"', out) == EOF || !write_base64_file(out, local_path) || fputc('"', out) == EOF) return 0;
    }
    return write_text(out, "]");
}

void relative_path(char* full, char* base, char* out, int size)
{
    int n = strlen(base);
    while (n > 1 && base[n - 1] == '/') n = n - 1;
    if (strncmp(full, base, n) == 0 && full[n] == '/') {
        int i = 0;
        while (full[n + 1 + i] != '\0' && i < size - 1) {
            out[i] = full[n + 1 + i];
            i = i + 1;
        }
        out[i] = '\0';
    }
    else {
        strncpy(out, full, size - 1);
    }
    out[size - 1] = '\0';
}

void parent_path(char* path, char* out, int size)
{
    int i = strlen(path) - 1;
    while (i >= 0 && path[i] != '/') i = i - 1;
    if (i <= 0) {
        strncpy(out, path, size - 1);
        out[size - 1] = '\0';
        return;
    }
    if (i >= size) i = size - 1;
    strncpy(out, path, i);
    out[i] = '\0';
}

int remove_tree(char* path)
{
    struct directory* d;
    int i;
    char child[PATHN];
    if (is_regular_file(path)) return unlink(path) == 0;
    d = read_directory(path);
    if (d == NULL) return 0;
    for (i = 0; i < d->count; i++) {
        strncpy(child, d->files[i], PATHN - 1);
        child[PATHN - 1] = '\0';
        if (!remove_tree(child)) {
            delete_directory(d);
            return 0;
        }
    }
    delete_directory(d);
    return rmdir(path) == 0;
}

int write_tree(FILE* out, char* root, char* base, char* game, int* entries)
{
    struct directory* d;
    int i;
    char rel[PATHN];
    char remote[PATHN];
    char child[PATHN];
    char marker[PATHN];
    int formatted;
    if (is_regular_file(root)) {
        relative_path(root, base, rel, PATHN);
        formatted = snprintf(remote, PATHN, "%s/%s", game, rel);
        if (formatted < 0 || formatted >= PATHN) {
            g_upload_error = UPLOAD_ERR_READ;
            return 0;
        }
        if (*entries > 0 && !write_text(out, ",")) return 0;
        if (!write_upload_entry(out, remote, root, 0)) return 0;
        *entries = *entries + 1;
        return 1;
    }
    d = read_directory(root);
    if (d == NULL) {
        g_upload_error = UPLOAD_ERR_READ;
        printf("Upload could not read folder: %s\n", root);
        return 0;
    }
    if (strcmp(root, base) != 0) {
        relative_path(root, base, rel, PATHN);
        formatted = snprintf(marker, PATHN, "%s/citraholdDirectoryDummy", rel);
        if (formatted < 0 || formatted >= PATHN) {
            delete_directory(d);
            g_upload_error = UPLOAD_ERR_READ;
            return 0;
        }
        formatted = snprintf(remote, PATHN, "%s/%s", game, marker);
        if (formatted < 0 || formatted >= PATHN) {
            delete_directory(d);
            g_upload_error = UPLOAD_ERR_READ;
            return 0;
        }
        if (*entries > 0 && !write_text(out, ",")) {
            delete_directory(d);
            return 0;
        }
        if (!write_upload_entry(out, remote, "", 1)) {
            delete_directory(d);
            return 0;
        }
        *entries = *entries + 1;
    }
    for (i = 0; i < d->count; i++) {
        strncpy(child, d->files[i], PATHN - 1);
        child[PATHN - 1] = '\0';
        if (!write_tree(out, child, base, game, entries)) {
            delete_directory(d);
            return 0;
        }
    }
    delete_directory(d);
    return 1;
}

int write_upload_payload(char* backup, char* game)
{
    FILE* out;
    int entries = 0;
    g_upload_total = 0;
    g_upload_done = 0;
    progress_begin(0, "Scanning backup", 0);
    if (!measure_upload_tree(backup, &g_upload_total)) {
        progress_end(0);
        return 0;
    }
    progress_begin(0, "Preparing upload payload", g_upload_total > 0 ? g_upload_total : 1);
    unlink(g_upload_payload);
    out = fopen(g_upload_payload, "wb");
    if (out == NULL) {
        g_upload_error = UPLOAD_ERR_TEMP;
        progress_end(0);
        return 0;
    }
    if (!write_text(out, "{\"token\":") || !write_json_string(out, active_token()) || !write_text(out, ",\"game\":") || !write_json_string(out, game) || !write_text(out, ",\"multi\":[") || !write_tree(out, backup, backup, game, &entries) || entries == 0 || !write_text(out, "]}")) {
        fclose(out);
        unlink(g_upload_payload);
        progress_end(0);
        return 0;
    }
    if (fclose(out) != 0) {
        g_upload_error = UPLOAD_ERR_TEMP;
        unlink(g_upload_payload);
        progress_end(0);
        return 0;
    }
    progress_set(0, g_upload_total);
    progress_end(0);
    return 1;
}

int choose_title(int type)
{
    char* names[MAXPICK];
    int indexes[MAXPICK];
    int count = 0;
    int total = titles_count();
    int i;
    int pick;
    for (i = 0; i < total && count < MAXPICK; i++) {
        if ((type == 0 && title_has_save(i)) || (type == 1 && title_has_extdata(i))) {
            indexes[count] = i;
            names[count] = title_name(i);
            count = count + 1;
        }
    }
    if (count == 0) {
        gui_message("No compatible titles were found.");
        return -1;
    }
    pick = gui_pick_one(type ? "Select extdata title" : "Select save title", names, count);
    for (i = 0; i < count; i++) free(names[i]);
    if (pick < 0) return -1;
    return indexes[pick];
}

int find_map(int type, char* title_id)
{
    int i;
    for (i = 0; i < g_map_count; i++) if (g_maps[i].type == type && strcmp(g_maps[i].title, title_id) == 0) return i;
    return -1;
}

int find_game_map(int type, char* game)
{
    int i;
    for (i = 0; i < g_map_count; i++) if (g_maps[i].type == type && strcmp(g_maps[i].game, game) == 0) return i;
    return -1;
}

void remove_mapping(int index)
{
    int i;
    for (i = index; i < g_map_count - 1; i++) {
        g_maps[i] = g_maps[i + 1];
    }
    g_map_count = g_map_count - 1;
}

void assignment_row(int type, char* game, char* row, int size)
{
    int map = find_game_map(type, game);
    if (map < 0) {
        snprintf(row, size, "(Unassigned) %s", game);
    }
    else {
        int idx = title_find(g_maps[map].title);
        char* name = NULL;
        if (idx >= 0) name = title_name(idx);
        if (name != NULL) {
            snprintf(row, size, "%s -> %s", game, name);
            free(name);
        }
        else snprintf(row, size, "%s -> linked title", game);
    }
}

int choose_remote_game(int type, char* game)
{
    struct remote_cache* cache = remote_cache_for_type(type);
    char* rows[MAXREMOTE + 1];
    int indexes[MAXREMOTE];
    int count = 0;
    int i;
    int pick;
    for (i = 0; i < cache->count; i++) {
        rows[count] = malloc(IDN + 64);
        assignment_row(type, cache->games[i], rows[count], IDN + 64);
        indexes[count] = i;
        count = count + 1;
    }
    rows[count] = "Enter a Game ID manually";
    pick = gui_pick_one(type ? "Select extdata Game ID" : "Select save Game ID", rows, count + 1);
    for (i = 0; i < count; i++) free(rows[i]);
    if (pick < 0) return 0;
    if (pick == count) {
        gui_keyboard(game, "Enter Citrahold Game ID", IDN);
        return valid_game_id(game);
    }
    strcpy(game, cache->games[indexes[pick]]);
    return 1;
}

void refresh_remote_games(void)
{
    char save_names[MAXPICK * IDN];
    char extdata_names[MAXPICK * IDN];
    int saves = remote_games(0, save_names);
    int extdata = remote_games(1, extdata_names);
    if (saves < 0 && extdata < 0) {
        gui_message("Could not query Citrahold Game IDs.");
        return;
    }
    gui_message("Server Game IDs were saved.\n(Unassigned) entries must be\nlinked to a Checkpoint title.");
}

void mappings_menu(void)
{
    char* choices[4];
    int choice;
    choices[0] = "Link Game ID to title";
    choices[1] = "Unlink Game ID from title";
    choices[2] = "Refresh server Game IDs";
    choices[3] = "Back";
    choice = gui_pick_one("Manage Game IDs", choices, 4);
    if (choice == 0) {
        int type;
        int idx;
        char* title_id_value;
        char game[IDN];
        int old_title;
        int old_game;
        char* type_choices[2];
        type_choices[0] = "Save";
        type_choices[1] = "Extdata";
        type = gui_pick_one("Mapping type", type_choices, 2);
        if (type < 0) return;
        idx = choose_title(type);
        if (idx < 0) return;
        title_id_value = title_id(idx);
        if (choose_remote_game(type, game)) {
            old_title = find_map(type, title_id_value);
            old_game = find_game_map(type, game);
            if (old_title >= 0 && old_title != old_game) {
                remove_mapping(old_title);
                if (old_game > old_title) old_game = old_game - 1;
            }
            if (old_game < 0 && g_map_count < MAXMAP) old_game = g_map_count++;
            if (old_game >= 0) {
                g_maps[old_game].type = type;
                strcpy(g_maps[old_game].title, title_id_value);
                strcpy(g_maps[old_game].game, game);
                if (state_write()) gui_message("Game ID linked to title.");
            }
            else gui_message("Too many Game ID links are configured.");
        }
        else {
            gui_message("Game IDs may not contain slashes, quotes, pipes, or '..'.");
        }
        free(title_id_value);
    }
    else if (choice == 1) {
        char* rows[MAXMAP];
        int i;
        int pick;
        int row_count = g_map_count;
        for (i = 0; i < g_map_count; i++) {
            rows[i] = malloc(IDN + 64);
            assignment_row(g_maps[i].type, g_maps[i].game, rows[i], IDN + 64);
        }
        if (g_map_count > 0) {
            pick = gui_pick_one("Unlink which Game ID?", rows, g_map_count);
            if (pick >= 0) {
                remove_mapping(pick);
                state_write();
            }
        }
        else gui_message("No Game IDs are linked yet.");
        for (i = 0; i < row_count; i++) free(rows[i]);
    }
    else if (choice == 2) refresh_remote_games();
}

int choose_mapping(int type)
{
    char* rows[MAXMAP];
    int indexes[MAXMAP];
    int count = 0;
    int i;
    int pick;
    for (i = 0; i < g_map_count; i++) if (g_maps[i].type == type) {
        rows[count] = malloc(IDN + TITLEIDN + 8);
        sprintf(rows[count], "%s", g_maps[i].game);
        indexes[count] = i;
        count = count + 1;
    }
    if (count == 0) {
        gui_message("No mapping exists for this data type.");
        return -1;
    }
    pick = gui_pick_one(type ? "Select extdata Game ID" : "Select save Game ID", rows, count);
    for (i = 0; i < count; i++) free(rows[i]);
    if (pick < 0) return -1;
    return indexes[pick];
}

int choose_backup(int idx, int type, char* path, int size)
{
    char* base = title_backup_path(idx, type);
    struct directory* d = read_directory(base);
    char* rows[MAXPICK];
    int count;
    int i;
    int pick;
    if (d == NULL || d->count == 0) {
        if (d != NULL) delete_directory(d);
        free(base);
        gui_message("No Checkpoint backups were found.");
        return 0;
    }
    count = d->count < MAXPICK ? d->count : MAXPICK;
    for (i = 0; i < count; i++) {
        rows[i] = malloc(ROWN);
        relative_path(d->files[i], base, rows[i], ROWN);
        rows[i][ROWN - 1] = '\0';
    }
    pick = gui_pick_one("Select Checkpoint backup", rows, count);
    if (pick >= 0) strncpy(path, d->files[pick], size - 1);
    path[size - 1] = '\0';
    for (i = 0; i < count; i++) free(rows[i]);
    delete_directory(d);
    free(base);
    return pick >= 0;
}

void log_transfer_result(int type, char* direction, char* result)
{
    char line[128];
    snprintf(line, 128, "Citrahold %s %s %s.", type ? "extdata" : "save", direction, result);
    script_log(line);
}

int upload_flow(int type)
{
    int map = choose_mapping(type);
    int idx;
    char backup[PATHN];
    char names[MAXPICK * IDN];
    int remote_count;
    char* out = NULL;
    int out_size = 0;
    int status;
    char confirm[PATHN + 128];
    char remote_time[128];
    printf("Starting %s upload...\n", type ? "extdata" : "save");
    log_debug(type ? "starting extdata upload" : "starting save upload");
    if (map < 0) {
        printf("No mapping selected; returning to the Citrahold menu.\n");
        log_transfer_result(type, "upload", "cancelled");
        return 0;
    }
    idx = title_find(g_maps[map].title);
    if (idx < 0) {
        log_transfer_result(type, "upload", "failed: mapped title is unavailable");
        return 0;
    }
    log_debug("upload title selected");
    if (!choose_backup(idx, type, backup, PATHN)) {
        log_transfer_result(type, "upload", "cancelled");
        return 0;
    }
    log_debug("upload backup selected");
    remote_count = remote_games(type, names);
    if (remote_count < 0) {
        printf("Remote Game ID query failed before upload; continuing with unknown remote status.\n");
        log_debug("remote game list unavailable before upload");
    }
    else log_debug("upload remote game list received");
    remote_timestamp(type, g_maps[map].game, remote_time, 128);
    log_debug("upload remote timestamp received");
    if (remote_count < 0) {
        sprintf(confirm, "Upload backup for Game ID:\n%s\n\nRemote copy: unavailable\nRemote time: unknown\n\nContinue?", g_maps[map].game);
    }
    else {
        sprintf(confirm, "Upload backup for Game ID:\n%s\n\nRemote copy: %s\nRemote time: %s\n\nContinue?", g_maps[map].game, remote_has(names, remote_count, g_maps[map].game) ? "exists and will be replaced" : "not present", remote_time[0] == '\0' ? "unknown" : remote_time);
    }
    if (!gui_confirm(confirm)) {
        log_transfer_result(type, "upload", "cancelled");
        return 0;
    }
    log_debug("preparing upload payload");
    g_upload_error = 0;
    if (!write_upload_payload(backup, g_maps[map].game)) {
        printf("Upload payload preparation failed.\n");
        if (g_upload_error == UPLOAD_ERR_READ) gui_message("A backup file or folder\ncould not be read.");
        else gui_message("Could not create the\ntemporary upload payload.");
        log_transfer_result(type, "upload", "failed during payload preparation");
        return 0;
    }
    log_debug("upload payload prepared");
    gui_status("Uploading Citrahold backup...");
    log_debug("sending upload request");
    status = api_upload_file(type ? "/uploadMultiExtdata" : "/uploadMultiSaves", g_upload_payload, &out, &out_size);
    if (out != NULL) free(out);
    printf("Upload response: HTTP %d\n", status);
    if (status == UPLOAD_STATUS_CLEANUP) {
        gui_message("The upload ended, but Checkpoint could not remove the plaintext payload.");
        log_transfer_result(type, "upload", "failed: plaintext cleanup failed");
        return 0;
    }
    if (status != 200 && status != 201) {
        gui_message("Citrahold rejected the upload.");
        log_transfer_result(type, "upload", "failed");
        return 0;
    }
    snprintf(confirm, PATHN + 128, "Upload succeeded. Delete this local Checkpoint backup?\n%s", backup);
    if (g_delete_after && gui_confirm(confirm)) {
        if (!remove_tree(backup)) {
            gui_message("Upload succeeded, but the local backup could not be deleted.");
            script_log("Citrahold upload succeeded, but local backup deletion failed.");
        }
    }
    gui_message("Citrahold upload completed.");
    log_transfer_result(type, "upload", "completed");
    return 1;
}

int safe_remote_path(char* path)
{
    if (path == NULL || path[0] == '\0' || path[0] == '/' || path[0] == '\\') return 0;
    if (strstr(path, "..") != NULL || strchr(path, '\\') != NULL) return 0;
    return 1;
}

int parse_content_range(char* headers, int* start, int* end, int* total)
{
    char* value;
    char* cursor;
    char* stop;
    int parsed_start = 0;
    int parsed_end = 0;
    int parsed_total = 0;
    int valid = 1;
    if (headers == NULL) return 0;
    value = http_header_value(headers, "Content-Range");
    if (value == NULL) return 0;
    if (strncmp(value, "bytes ", 6) != 0) valid = 0;
    if (valid) {
        cursor = &value[6];
        parsed_start = strtol(cursor, &stop, 10);
        if (stop == cursor || parsed_start < 0 || *stop != '-') valid = 0;
    }
    if (valid) {
        cursor = &stop[1];
        parsed_end = strtol(cursor, &stop, 10);
        if (stop == cursor || parsed_end < parsed_start || *stop != '/') valid = 0;
    }
    if (valid) {
        cursor = &stop[1];
        parsed_total = strtol(cursor, &stop, 10);
        if (stop == cursor || parsed_total <= parsed_end || *stop != '\0') valid = 0;
    }
    free(value);
    if (!valid) return 0;
    *start = parsed_start;
    *end = parsed_end;
    *total = parsed_total;
    return 1;
}

int download_body(char* body, int body_size, char* game, char* file)
{
    body[0] = '\0';
    if (!append_text(body, "{\"token\":\"", body_size)) return 0;
    if (!append_json_string(body, active_token(), body_size)) return 0;
    if (!append_text(body, "\",\"game\":\"", body_size)) return 0;
    if (!append_json_string(body, game, body_size)) return 0;
    if (file != NULL) {
        if (!append_text(body, "\",\"file\":\"", body_size)) return 0;
        if (!append_json_string(body, file, body_size)) return 0;
    }
    return append_text(body, "\"}", body_size);
}

int write_download_chunk(char* path, char* data, int bytes, int append)
{
    FILE* f;
    int wrote;
    f = fopen(path, append ? "ab" : "wb");
    if (f == NULL) return 0;
    wrote = fwrite(data, 1, bytes, f);
    if (fclose(f) != 0) return 0;
    return wrote == bytes;
}

int download_remote_file(int type, char* game, char* remote, char* local, int file_number, int file_count)
{
    char body[PATHN * 2 + TOKENN + IDN + 64];
    char headers[96];
    char note[128];
    char* out = NULL;
    char* response_headers = NULL;
    int out_size = 0;
    int status;
    int received = 0;
    int total = 0;
    int range_start;
    int range_end;
    int range_total;
    int first = 1;

    while (first || received < total) {
        sprintf(headers, "Range: bytes=%d-%d", received, received + DOWNLOAD_CHUNK - 1);
        if (!download_body(body, sizeof(body), game, remote)) return 0;
        out = NULL;
        response_headers = NULL;
        out_size = 0;
        sprintf(note, "Downloading file %d of %d", file_number, file_count);
        gui_status("Receiving Citrahold backup...");
        status = api_call_headers(type ? "/downloadExtdata" : "/downloadSaves", headers, body, &out, &out_size, &response_headers);
        if ((status != 206 && status != 200) || out == NULL || out_size <= 0 || out_size > DOWNLOAD_CHUNK) {
            if (out != NULL) free(out);
            if (response_headers != NULL) free(response_headers);
            printf("Download chunk failed: HTTP %d, %d bytes.\n", status, out_size);
            return 0;
        }
        if (status == 200) {
            if (!first || received != 0) {
                free(out);
                if (response_headers != NULL) free(response_headers);
                progress_end(1);
                return 0;
            }
            total = out_size;
        }
        else {
            if (!parse_content_range(response_headers, &range_start, &range_end, &range_total) || range_start != received || range_end - range_start + 1 != out_size || (total != 0 && range_total != total)) {
                free(out);
                if (response_headers != NULL) free(response_headers);
                progress_end(1);
                return 0;
            }
            total = range_total;
        }
        if (first) {
            sprintf(note, "File %d of %d", file_number, file_count);
            progress_begin(1, note, total);
            first = 0;
        }
        if (received + out_size > total || !write_download_chunk(local, out, out_size, received > 0)) {
            free(out);
            if (response_headers != NULL) free(response_headers);
            progress_end(1);
            return 0;
        }
        received = received + out_size;
        progress_set(1, received);
        free(out);
        if (response_headers != NULL) free(response_headers);
        if (status == 200) break;
    }
    progress_end(1);
    return received == total;
}

int download_flow(int type)
{
    int map = choose_mapping(type);
    int idx;
    char* base;
    char backup_name[IDN];
    char temp[PATHN];
    char final_path[PATHN];
    char body[PATHN * 2 + TOKENN + IDN + 64];
    char* out = NULL;
    int out_size = 0;
    int status;
    struct JSON* root;
    struct JSON* files;
    struct JSON* item;
    int i;
    int count;
    char* key;
    char path[PATHN];
    char confirm[CONFIRMN];
    int stale;
    int formatted;

    printf("Starting %s download...\n", type ? "extdata" : "save");
    log_debug(type ? "starting extdata download" : "starting save download");
    if (map < 0) {
        printf("No mapping selected; returning to the Citrahold menu.\n");
        log_transfer_result(type, "download", "cancelled");
        return 0;
    }
    idx = title_find(g_maps[map].title);
    if (idx < 0) {
        log_transfer_result(type, "download", "failed: mapped title is unavailable");
        return 0;
    }
    {
        char names[MAXPICK * IDN];
        int count_remote = remote_games(type, names);
        if (count_remote < 0 || !remote_has(names, count_remote, g_maps[map].game)) {
            gui_message("That Game ID is not present on the server.");
            log_transfer_result(type, "download", "failed: remote Game ID unavailable");
            return 0;
        }
    }
    gui_keyboard(backup_name, "New Checkpoint backup name", IDN);
    if (backup_name[0] == '\0') {
        log_transfer_result(type, "download", "cancelled");
        return 0;
    }
    if (!valid_backup_name(backup_name)) {
        gui_message("Use one safe backup name without slashes, '..', control characters, or FAT-invalid punctuation.");
        script_log("Citrahold download rejected an unsafe local backup name.");
        return 0;
    }
    base = title_backup_path(idx, type);
    if (base[0] == '\0') {
        free(base);
        gui_message("Checkpoint has no backup folder for that title and data type.");
        script_log("Citrahold download failed: no local backup folder.");
        return 0;
    }
    formatted = snprintf(temp, PATHN, "%s%s.citrahold-partial", base, backup_name);
    if (formatted < 0 || formatted >= PATHN) {
        free(base);
        gui_message("The temporary backup path is too long.");
        script_log("Citrahold download failed: temporary path too long.");
        return 0;
    }
    formatted = snprintf(final_path, PATHN, "%s%s", base, backup_name);
    if (formatted < 0 || formatted >= PATHN) {
        free(base);
        gui_message("The final backup path is too long.");
        script_log("Citrahold download failed: final path too long.");
        return 0;
    }
    if (sd_exists(final_path)) {
        free(base);
        gui_message("That Checkpoint backup name already exists.");
        log_transfer_result(type, "download", "failed: backup name already exists");
        return 0;
    }
    stale = sd_exists(temp);
    if (stale) {
        formatted = snprintf(confirm, CONFIRMN, "Download Game ID:\n%s\ninto:\n%s\n\nA stale partial folder at that path will be deleted first. Continue?", g_maps[map].game, temp);
    }
    else {
        formatted = snprintf(confirm, CONFIRMN, "Download Game ID:\n%s\ninto:\n%s\n\nContinue?", g_maps[map].game, temp);
    }
    if (formatted < 0 || formatted >= CONFIRMN) {
        free(base);
        gui_message("The download confirmation is too long.");
        script_log("Citrahold download failed: confirmation path too long.");
        return 0;
    }
    if (!gui_confirm(confirm)) {
        free(base);
        log_transfer_result(type, "download", "cancelled");
        return 0;
    }
    if (stale && !remove_tree(temp)) {
        free(base);
        gui_message("Could not remove the stale temporary download folder.");
        script_log("Citrahold download failed: stale partial cleanup failed.");
        return 0;
    }
    if (sd_mkdirs(temp) != 0) {
        free(base);
        gui_message("Could not create the temporary backup folder.");
        log_transfer_result(type, "download", "failed: temporary folder creation failed");
        return 0;
    }

    if (!download_body(body, sizeof(body), g_maps[map].game, NULL)) {
        remove_tree(temp);
        free(base);
        gui_message("Could not prepare the download request.");
        log_transfer_result(type, "download", "failed: request preparation failed");
        return 0;
    }
    gui_status("Receiving Citrahold backup...");
    progress_begin(0, "Listing Citrahold backup", 0);
    status = api_call(type ? "/downloadExtdata" : "/downloadSaves", body, &out, &out_size);
    progress_end(0);
    if (status != 200 || out == NULL || out_size > DOWNLOAD_RESPONSE_LIMIT - 1) {
        if (out != NULL) free(out);
        remove_tree(temp);
        free(base);
        printf("Download file list failed: HTTP %d, %d bytes.\n", status, out_size);
        gui_message("Citrahold download failed.");
        log_transfer_result(type, "download", "failed while listing files");
        return 0;
    }
    root = json_new();
    if (root == NULL) {
        free(out);
        remove_tree(temp);
        free(base);
        gui_message("Could not allocate the download response.");
        log_transfer_result(type, "download", "failed: response allocation failed");
        return 0;
    }
    json_parse(root, out);
    free(out);
    if (!json_is_valid(root) || !json_is_object(root) || !json_object_contains(root, "files")) {
        json_delete(root);
        remove_tree(temp);
        free(base);
        gui_message("The server returned no files.");
        log_transfer_result(type, "download", "failed: invalid file-list response");
        return 0;
    }
    files = json_object_element(root, "files");
    if (files == NULL || !json_is_array(files)) {
        json_delete(root);
        remove_tree(temp);
        free(base);
        gui_message("The server returned an invalid file list.");
        log_transfer_result(type, "download", "failed: invalid file-list response");
        return 0;
    }
    count = json_array_size(files);
    if (count <= 0) {
        json_delete(root);
        remove_tree(temp);
        free(base);
        gui_message("The remote Game ID contains no files.");
        log_transfer_result(type, "download", "failed: remote backup is empty");
        return 0;
    }
    progress_begin(0, "Files", count);
    for (i = 0; i < count; i++) {
        item = json_array_element(files, i);
        if (item == NULL || !json_is_string(item)) {
            json_delete(root);
            remove_tree(temp);
            free(base);
            progress_end(0);
            gui_message("The server returned an invalid file name.");
            log_transfer_result(type, "download", "failed: invalid remote file name");
            return 0;
        }
        key = json_get_string(item);
        if (!safe_remote_path(key) || strlen(key) >= PATHN - strlen(temp) - 2) {
            free(key);
            json_delete(root);
            remove_tree(temp);
            free(base);
            progress_end(0);
            gui_message("The server returned an unsafe file path.");
            log_transfer_result(type, "download", "failed: unsafe remote file path");
            return 0;
        }
        if (strcmp(key, "citraholdDirectoryDummy") == 0) {
            /* The server uses this marker for an otherwise empty root. */
        }
        else if (strstr(key, "citraholdDirectoryDummy") != NULL) {
            char* marker = strstr(key, "/citraholdDirectoryDummy");
            if (marker != NULL) *marker = '\0';
            formatted = snprintf(path, PATHN, "%s/%s", temp, key);
            if (formatted < 0 || formatted >= PATHN || sd_mkdirs(path) != 0) {
                free(key);
                json_delete(root);
                remove_tree(temp);
                free(base);
                progress_end(0);
                gui_message("Could not create a downloaded backup folder.");
                script_log("Citrahold download failed: local folder creation failed.");
                return 0;
            }
        }
        else {
            formatted = snprintf(path, PATHN, "%s/%s", temp, key);
            if (formatted < 0 || formatted >= PATHN) {
                free(key);
                json_delete(root);
                remove_tree(temp);
                free(base);
                progress_end(0);
                gui_message("The server returned a file path that is too long.");
                script_log("Citrahold download failed: local file path too long.");
                return 0;
            }
            {
                char parent[PATHN];
                parent_path(path, parent, PATHN);
                if (sd_mkdirs(parent) != 0 || !download_remote_file(type, g_maps[map].game, key, path, i + 1, count)) {
                    free(key);
                    json_delete(root);
                    remove_tree(temp);
                    free(base);
                    progress_end(0);
                    gui_message("Citrahold download failed.");
                    log_transfer_result(type, "download", "failed while receiving a file");
                    return 0;
                }
            }
        }
        free(key);
        progress_set(0, i + 1);
    }
    progress_end(0);
    json_delete(root);
    if (rename(temp, final_path) != 0) {
        remove_tree(temp);
        free(base);
        gui_message("Download completed, but the backup could not be finalized.");
        log_transfer_result(type, "download", "failed while finalizing the backup");
        return 0;
    }
    free(base);
    gui_message("Download completed and added to Checkpoint's backup list.");
    log_transfer_result(type, "download", "completed");
    return 1;
}

void configuration_menu(void)
{
    char* options[8];
    int choice;
    options[0] = "Switch server mode";
    options[1] = "Authenticate active server";
    options[2] = "Test active server";
    options[3] = "Delete after upload";
    options[4] = "Debug logging";
    options[5] = "Set/change passphrase";
    options[6] = "Remove passphrase";
    options[7] = "Back";
    choice = gui_pick_one("Citrahold Configuration", options, 8);
    if (choice == 0) {
        if (choose_mode()) {
            if (state_write()) gui_message("Server mode saved.");
        }
    }
    else if (choice == 1) {
        char token[TOKENN];
        char old_token[TOKENN];
        strcpy(old_token, active_token());
        gui_keyboard(token, "Full Citrahold token", TOKENN);
        if (token[0] != '\0') {
            if (strcmp(g_mode, "official") == 0) strcpy(g_official_token, token);
            else strcpy(g_custom_token, token);
            if (verify_token()) {
                if (!state_write()) {
                    if (strcmp(g_mode, "official") == 0) strcpy(g_official_token, old_token);
                    else strcpy(g_custom_token, old_token);
                    gui_message("The previous token was retained.");
                }
            }
            else {
                if (strcmp(g_mode, "official") == 0) strcpy(g_official_token, old_token);
                else strcpy(g_custom_token, old_token);
                gui_message("The previous token was retained.");
            }
        }
    }
    else if (choice == 2) server_test();
    else if (choice == 3) {
        g_delete_after = !g_delete_after;
        state_write();
        gui_message(g_delete_after ? "Delete-after-upload enabled." : "Delete-after-upload disabled.");
    }
    else if (choice == 4) {
        g_debug = !g_debug;
        state_write();
        gui_message(g_debug ? "Debug logging enabled." : "Debug logging disabled.");
    }
    else if (choice == 5) {
        gui_keyboard(g_pass, "Enter new passphrase", PASSN);
        if (g_pass[0] != '\0' && state_write()) gui_message("Passphrase saved.");
    }
    else if (choice == 6) {
        if (g_pass[0] == '\0') {
            gui_message("This state has no passphrase.");
        }
        else if (gui_confirm("Remove the passphrase? Same-console homebrew can then derive the vault key.")) {
            g_pass[0] = '\0';
            state_write();
        }
    }
}

void upload_menu(void)
{
    char* options[3];
    int choice;
    options[0] = "Upload save backup";
    options[1] = "Upload extdata backup";
    options[2] = "Back";
    choice = gui_pick_one("Upload", options, 3);
    if (choice == 0) upload_flow(0);
    else if (choice == 1) upload_flow(1);
}

void download_menu(void)
{
    char* options[3];
    int choice;
    options[0] = "Download save backup";
    options[1] = "Download extdata backup";
    options[2] = "Back";
    choice = gui_pick_one("Download", options, 3);
    if (choice == 0) download_flow(0);
    else if (choice == 1) download_flow(1);
}

int main(int argc, char** argv)
{
    char* options[4];
    int choice;
    int loaded;
    printf("Citrahold script revision: %s\n", SCRIPT_VERSION);
    init_paths();
    loaded = state_read();
    if (loaded == 0) {
        loaded = configure_first_run();
        if (loaded < 0) return 1;
        if (loaded == 0) return 0;
    }
    else if (loaded == -2) return 0;
    else if (loaded < 0) return 1;
    if (g_mode[0] == '\0') return 1;
    show_server_info();
    while (1) {
        printf("Citrahold menu ready. Hold B to exit.\n");
        options[0] = "Upload";
        options[1] = "Download";
        options[2] = "Manage Game IDs";
        options[3] = "Configuration";
        choice = gui_pick_one("Citrahold", options, 4);
        if (choice < 0) {
            printf("Citrahold script cancelled.\n");
            progress_clear();
            return 0;
        }
        if (choice == 0) upload_menu();
        else if (choice == 1) download_menu();
        else if (choice == 2) mappings_menu();
        else if (choice == 3) configuration_menu();
        progress_clear();
        printf("Action finished. Returning to the Citrahold menu.\n");
    }
}
