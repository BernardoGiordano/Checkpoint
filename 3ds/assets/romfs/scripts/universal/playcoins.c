/*
 * playcoins.c — set the console's Play Coins from a script.
 */

#include <checkpoint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Home Menu shared extdata (holds /gamecoin.dat). Low 32 bits = extdata id, high
 * 32 bits = the shared-extdata archive magic. */
#define GAMECOIN_EXTDATA "00048000F000000B"
#define GAMECOIN_FILE    "/gamecoin.dat"
#define COIN_OFFSET      4   /* little-endian u16 count */
#define COIN_MAX         300 /* hardware cap */

int main(int argc, char** argv)
{
    /* Pick the target value: the maximum, or a custom amount. */
    char* choices[2];
    choices[0] = "Set to maximum (300)";
    choices[1] = "Enter a custom value";
    int pick   = gui_pick_one("Play Coins", choices, 2);
    if (pick < 0) {
        return 0; /* cancelled */
    }

    int coins = COIN_MAX;
    if (pick == 1) {
        /* Numeric keypad; the API validates the range, so no clamping here. */
        coins = gui_numpad("Play Coins", 0, COIN_MAX);
        if (coins < 0) {
            return 0; /* cancelled */
        }
    }

    char prompt[64];
    sprintf(prompt, "Set Play Coins to %d?", coins);
    if (!gui_confirm(prompt)) {
        return 0;
    }

    gui_status("Opening Play Coin data...");
    int h = sav_open_shared(GAMECOIN_EXTDATA);
    if (h < 0) {
        script_log("playcoins: sav_open_shared failed");
        gui_message("Could not open the Play Coin data.");
        return 1;
    }

    /* Read the whole file so the write-back preserves everything but the count
     * (sav_write replaces the entire file). */
    char* data;
    int size;
    int res = sav_read(h, GAMECOIN_FILE, &data, &size);
    if (res != 0 || size < COIN_OFFSET + 2) {
        printf("sav_read failed: res=%d size=%d\n", res, size);
        gui_message("Could not read gamecoin.dat.");
        sav_close(h);
        return 1;
    }

    /* Patch the little-endian u16 count in place. */
    data[COIN_OFFSET]     = (char)(coins & 0xFF);
    data[COIN_OFFSET + 1] = (char)((coins >> 8) & 0xFF);

    res = sav_write(h, GAMECOIN_FILE, data, size);
    free(data);
    if (res != 0) {
        printf("sav_write failed: %d\n", res);
        gui_message("Could not write gamecoin.dat.");
        sav_close(h);
        return 1;
    }

    /* Commit + close, exactly as Checkpoint does after a restore. commit is a
     * no-op on extdata, but calling it keeps the pattern correct for any archive. */
    res = sav_commit(h);
    if (res != 0) {
        printf("sav_commit failed: %d\n", res);
        gui_message("Could not commit the Play Coin data.");
        sav_close(h);
        return 1;
    }
    sav_close(h);

    char done[64];
    sprintf(done, "Play Coins set to %d.", coins);
    script_log("playcoins: done");
    gui_message(done);
    return 0;
}
