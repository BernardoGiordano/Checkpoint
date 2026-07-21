/*
 * sharkive.c — Checkpoint cheat manager (Switch)
 *
 * Recreates the cheats feature Checkpoint used to embed, as a script:
 *   - downloads the cheat database (switch.json) from Sharkive's latest release
 *     and lets you update it later from the menu,
 *   - lists your installed games that have cheats,
 *   - resolves the build id (nested in the database, or typed in for a flat one),
 *   - lets you tick the cheats you want (already-enabled ones are pre-checked),
 *   - writes /atmosphere/contents/<TITLEID>/cheats/<BUILDID>.txt for Atmosphere.
 *
 * The database lives at sdmc:/switch/Checkpoint/cheats.json — the same path the
 * old built-in feature used, so an existing hand-made file keeps working. No
 * hash check is done: "Update" always fetches the latest Sharkive build, and
 * you may drop in your own cheats.json instead.
 *
 * Database shapes (uppercase 16-hex title ids):
 *   sharkive switch DB (preferred, carries the build id):
 *     { "<TITLEID>": { "<BUILDID>": { "<cheat name>": ["line", ...] } } }
 *   flat DB (build id asked via keyboard):
 *     { "<TITLEID>": { "<cheat name>": ["line", ...] } }
 *
 * The build id must match the running executable's (first 8 bytes of the main
 * NSO build id, 16 hex chars) or Atmosphere ignores the file.
 */

#include <checkpoint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHEATS_JSON "sdmc:/switch/Checkpoint/cheats.json"
#define CHEATS_TMP  "sdmc:/switch/Checkpoint/cheats.json.tmp"
#define SHARKIVE_URL "https://github.com/FlagBrew/Sharkive/releases/latest/download/switch.json"

char* readWholeFile(char* path)
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

/* Reads and validates the database. Returns a JSON object, or NULL when the
 * file is missing or not a JSON object. */
struct JSON* loadDatabase()
{
    char* data = readWholeFile(CHEATS_JSON);
    if (data == NULL) {
        return NULL;
    }
    struct JSON* root = json_new();
    json_parse(root, data);
    free(data);
    if (!json_is_valid(root) || !json_is_object(root)) {
        json_delete(root);
        return NULL;
    }
    return root;
}

/* Fetches the latest switch.json from Sharkive and replaces the local database.
 * The download is validated as a JSON object before it overwrites a file that
 * may already be good, and it is written to a temp file then renamed so a
 * half-finished write never clobbers the database. Returns 1 on success. */
int downloadDatabase()
{
    gui_status("Downloading cheats from Sharkive...");
    char* body = NULL;
    int size   = 0;
    int status = web_get(&body, &size, SHARKIVE_URL);

    if (status != 200 || body == NULL || size == 0) {
        char msg[192];
        if (status < 0) {
            sprintf(msg, "Download failed (network error %d).\nCheck your Wi-Fi connection and try again.", status);
        }
        else {
            sprintf(msg, "Download failed (server returned HTTP %d).\nPlease try again later.", status);
        }
        if (body != NULL) {
            free(body);
        }
        gui_message(msg);
        return 0;
    }

    gui_status("Checking the download...");
    struct JSON* check = json_new();
    json_parse(check, body);
    int valid = json_is_valid(check) && json_is_object(check);
    json_delete(check);
    if (!valid) {
        free(body);
        gui_message("The downloaded file is not a valid cheat database.\nYour existing database was kept.");
        return 0;
    }

    gui_status("Saving to the SD card...");
    FILE* f = fopen(CHEATS_TMP, "wb");
    if (f == NULL) {
        free(body);
        gui_message("Could not write to the SD card.");
        return 0;
    }
    int wrote = fwrite(body, 1, size, f);
    fclose(f);
    free(body);
    if (wrote != size) {
        remove(CHEATS_TMP);
        gui_message("Write failed (SD card full?).\nYour existing database was kept.");
        return 0;
    }

    remove(CHEATS_JSON);
    if (rename(CHEATS_TMP, CHEATS_JSON) != 0) {
        remove(CHEATS_TMP);
        gui_message("Could not replace the existing database file.");
        return 0;
    }
    script_log("sharkive: database updated");
    return 1;
}

/* Marks selected[k] = 1 for every cheat whose "[name]" header appears in an
 * already-written cheat file, so re-running preserves earlier picks. */
void preselectFromExisting(char* outPath, char** names, int count, int* selected)
{
    FILE* f = fopen(outPath, "r");
    if (f == NULL) {
        return;
    }
    char line[512];
    while (fgets(line, 512, f) != NULL) {
        int len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            len = len - 1;
            line[len] = '\0';
        }
        if (len > 1 && line[0] == '[' && line[len - 1] == ']') {
            line[len - 1] = '\0';
            /* drop the leading '[' by shifting in place: picoc rejects
             * pointer arithmetic on an array (`line + 1` = invalid operation) */
            int x;
            for (x = 1; x < len; x++) {
                line[x - 1] = line[x];
            }
            int k;
            for (k = 0; k < count; k++) {
                if (strcmp(names[k], line) == 0) {
                    selected[k] = 1;
                }
            }
        }
    }
    fclose(f);
}

/* Writes the picked cheats to the Atmosphere cheat file. With nothing picked
 * the file is removed so Atmosphere stops loading stale cheats for that game. */
void writeCheatFile(char* outDir, char* outPath, struct JSON* titleCheats, char** cheatNames, int* selected, int count)
{
    gui_status("Writing cheats...");
    if (sd_mkdirs(outDir) != 0) {
        gui_message("Could not create the cheats folder under /atmosphere/contents.");
        return;
    }
    FILE* f = fopen(outPath, "w");
    if (f == NULL) {
        gui_message("Could not open the cheat file for writing.");
        return;
    }
    int written = 0;
    int k;
    for (k = 0; k < count; k++) {
        if (selected[k] == 0) {
            continue;
        }
        fprintf(f, "[%s]\n", cheatNames[k]);
        struct JSON* lines = json_object_element(titleCheats, cheatNames[k]);
        int m              = json_array_size(lines);
        int j;
        for (j = 0; j < m; j++) {
            struct JSON* el = json_array_element(lines, j);
            if (json_is_string(el)) {
                char* s = json_get_string(el);
                fprintf(f, "%s\n", s);
                free(s);
            }
        }
        fprintf(f, "\n");
        written = written + 1;
    }
    fclose(f);

    if (written == 0) {
        remove(outPath);
        gui_message("All cheats disabled for this game.");
        return;
    }
    char msg[224];
    sprintf(msg, "%d cheat(s) saved.", written);
    gui_message(msg);
}

/* One pass of the apply flow: pick a game, resolve the build id, tick cheats. */
void applyCheats(struct JSON* root)
{
    gui_status("Matching installed games...");
    int total    = titles_count();
    char** ids   = malloc(sizeof(char*) * total);
    char** names = malloc(sizeof(char*) * total);
    char* sel    = selected_title();
    int n        = 0;
    int i;
    for (i = 0; i < total; i++) {
        char* id = title_id(i);
        if (json_object_contains(root, id)) {
            /* the Switch catalog repeats a title once per user profile;
             * keep the first occurrence only */
            int dup = 0;
            int d;
            for (d = 0; d < n; d++) {
                if (strcmp(ids[d], id) == 0) {
                    dup = 1;
                }
            }
            if (dup) {
                free(id);
                continue;
            }
            ids[n]   = id;
            names[n] = title_name(i);
            /* offer the game highlighted in Checkpoint first */
            if (n > 0 && sel != NULL && strcmp(id, sel) == 0) {
                char* t  = ids[n];
                ids[n]   = ids[0];
                ids[0]   = t;
                t        = names[n];
                names[n] = names[0];
                names[0] = t;
            }
            n = n + 1;
        }
        else {
            free(id);
        }
    }
    if (sel != NULL) {
        free(sel);
    }

    if (n == 0) {
        gui_message("None of your installed games have cheats in the database.");
        free(ids);
        free(names);
        return;
    }

    int pick = gui_pick_one("Select a game", names, n);
    if (pick >= 0) {
        char* id                = ids[pick];
        struct JSON* titleEntry = json_object_element(root, id);
        int entryCount          = json_array_size(titleEntry);
        if (entryCount == 0) {
            gui_message("This game has no cheats in the database.");
        }
        else {
            /* resolve the build id level. sharkive switch DB nests
             * titleid -> buildid -> cheats; a flat DB goes straight to cheats
             * (values are arrays of lines), so the build id is asked instead. */
            struct JSON* titleCheats;
            char buildId[64];
            char* firstKey    = json_object_key(titleEntry, 0);
            struct JSON* first = json_object_element(titleEntry, firstKey);
            free(firstKey);
            int ok = 1;

            if (json_is_object(first)) {
                int b = 0;
                if (entryCount > 1) {
                    char** buildIds = malloc(sizeof(char*) * entryCount);
                    int q;
                    for (q = 0; q < entryCount; q++) {
                        buildIds[q] = json_object_key(titleEntry, q);
                    }
                    b = gui_pick_one("Select the build id (must match your game version)", buildIds, entryCount);
                    for (q = 0; q < entryCount; q++) {
                        free(buildIds[q]);
                    }
                    free(buildIds);
                    if (b < 0) {
                        ok = 0;
                    }
                }
                if (ok) {
                    char* key = json_object_key(titleEntry, b);
                    sprintf(buildId, "%s", key);
                    titleCheats = json_object_element(titleEntry, key);
                    free(key);
                }
            }
            else {
                buildId[0] = '\0';
                gui_keyboard(buildId, "Build id (16 hex chars)", 33);
                if (strlen(buildId) == 0) {
                    gui_message("A build id is required: Atmosphere matches the cheat file name against the running game.");
                    ok = 0;
                }
                titleCheats = titleEntry;
            }

            if (ok) {
                int count = json_array_size(titleCheats);
                if (count == 0) {
                    gui_message("This build has no cheats in the database.");
                }
                else {
                    char** cheatNames = malloc(sizeof(char*) * count);
                    int* selected     = malloc(sizeof(int) * count);
                    int k;
                    for (k = 0; k < count; k++) {
                        cheatNames[k] = json_object_key(titleCheats, k);
                        selected[k]   = 0;
                    }
                    char outDir[128];
                    char outPath[160];
                    sprintf(outDir, "sdmc:/atmosphere/contents/%s/cheats", id);
                    sprintf(outPath, "%s/%s.txt", outDir, buildId);
                    preselectFromExisting(outPath, cheatNames, count, selected);
                    if (gui_pick_many("Select cheats (already-enabled ones are checked)", cheatNames, count, selected) == 1) {
                        writeCheatFile(outDir, outPath, titleCheats, cheatNames, selected, count);
                    }
                    for (k = 0; k < count; k++) {
                        free(cheatNames[k]);
                    }
                    free(cheatNames);
                    free(selected);
                }
            }
        }
    }

    int j;
    for (j = 0; j < n; j++) {
        free(ids[j]);
        free(names[j]);
    }
    free(ids);
    free(names);
}

int main(int argc, char** argv)
{
    struct JSON* root = loadDatabase();
    if (root == NULL) {
        if (gui_confirm("No cheat database found.\nDownload the latest cheats from Sharkive now?\n(about 1 MB, Wi-Fi required)") == 0) {
            return 0;
        }
        if (!downloadDatabase()) {
            return 1;
        }
        root = loadDatabase();
        if (root == NULL) {
            gui_message("The database was downloaded but could not be read.");
            return 1;
        }
    }

    while (1) {
        char* menu[3];
        menu[0]     = "Apply cheats to a game";
        menu[1]     = "Update the cheat database";
        menu[2]     = "Exit";
        int choice = gui_pick_one("Sharkive cheats", menu, 3);

        if (choice < 0 || choice == 2) {
            json_delete(root);
            return 0;
        }
        if (choice == 1) {
            if (downloadDatabase()) {
                json_delete(root);
                root = loadDatabase();
                if (root == NULL) {
                    gui_message("The update was saved but the database could not be reloaded.");
                    return 1;
                }
                gui_message("Cheat database updated to the latest Sharkive release.");
            }
            continue;
        }
        applyCheats(root);
    }
}
