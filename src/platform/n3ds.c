#include "platform/n3ds.h"

#include <3ds.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_PATH FOXRV_3DS_DIR "engine.ini"

const char *n3ds_config_path(void)
{
    SDL_PathInfo info;
    if (SDL_GetPathInfo(CONFIG_PATH, &info))
        return CONFIG_PATH;
    size_t size;
    void *defaults = SDL_LoadFile("romfs:/engine.ini", &size);
    if (!defaults || !SDL_CreateDirectory(FOXRV_3DS_DIR) || !SDL_SaveFile(CONFIG_PATH, defaults, size))
        SDL_Log("config: can't create %s: %s", CONFIG_PATH, SDL_GetError());
    SDL_free(defaults);
    return CONFIG_PATH;
}

#define CONSOLE_COLUMNS 40          /* the bottom screen's 320 px in 8 px characters */

/* Prints `text` broken at spaces to fit the console's lines. */
static void print_wrapped(const char *text)
{
    int col = 0;
    while (*text) {
        if (*text == '\n') {
            putchar('\n');
            col = 0;
            text++;
            continue;
        }
        int word = (int)strcspn(text, " \n");
        if (col > 0 && col + word > CONSOLE_COLUMNS) {
            if (col < CONSOLE_COLUMNS)  /* a full line already moved to the next one */
                putchar('\n');
            col = 0;
        }
        printf("%.*s", word, text);
        col += word;
        text += word;
        if (*text == ' ') {
            if (col < CONSOLE_COLUMNS)
                putchar(' ');
            col++;
            text++;
        }
    }
}

void n3ds_fatal(const char *message)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    printf("FoxRV\n\n");
    print_wrapped(message);
    printf("\n\nPress START to quit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
        gspWaitForVBlank();
    }
    gfxExit();
}
