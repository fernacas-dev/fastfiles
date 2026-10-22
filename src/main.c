#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "input.h"
#include "ui.h"

#define FF_VERSION "1.0.0"

static void print_help(const char *prog)
{
    printf("Usage: %s [OPTIONS] [PATH]\n", prog);
    printf("\nMinimalist Linux file explorer.\n\n");
    printf("Arguments:\n");
    printf("  PATH                 Directory to open (or file to open externally)\n");
    printf("Options:\n");
    printf("  --help               Show this help\n");
    printf("  --version            Show version\n");
    printf("  --config PATH        Use custom config file\n");
}

int main(int argc, char **argv)
{
    const char *start_path = NULL;
    const char *config_override = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("fastfiles %s\n", FF_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "fastfiles: --config requires a path\n");
                return 1;
            }
            config_override = argv[++i];
            continue;
        }
        if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(stderr, "fastfiles: unknown option %s\n", argv[i]);
            return 1;
        }
        if (!start_path) {
            start_path = argv[i];
        } else {
            fprintf(stderr, "fastfiles: unexpected argument %s\n", argv[i]);
            return 1;
        }
    }

    /* If start path is a file, open it externally and use its dir */
    const char *open_file = NULL;
    if (start_path) {
        struct stat st;
        if (stat(start_path, &st) == 0 && !S_ISDIR(st.st_mode)) {
            open_file = start_path;
        }
    }

    struct ff_app app;
    memset(&app, 0, sizeof(app));
    if (ff_app_init(&app, config_override, start_path) != 0) {
        return 1;
    }

    if (open_file) {
        (void)ff_app_open_file_external(open_file);
    }

    /* Event-driven main loop: wait for events, render only when dirty.
       A short timeout keeps cursor blink alive without busy-looping. */
    while (!app.quit) {
        SDL_Event e;
        bool had = SDL_WaitEventTimeout(&e, 250);
        if (had) {
            ff_input_handle_event(&app, &e);
            /* drain coalesced motion/wheel/expose events quickly */
            SDL_Event extra;
            while (SDL_PollEvent(&extra)) {
                ff_input_handle_event(&app, &extra);
            }
        } else {
            /* timeout: only re-render if a blinking widget is visible */
            if (app.focus == FF_FOCUS_PATH || app.focus == FF_FOCUS_SEARCH ||
                app.dialog != FF_DLG_NONE) {
                app.dirty = true;
            }
        }
        if (app.dirty) {
            SDL_GetWindowSize(app.window, &app.win_w, &app.win_h);
            ff_input_clamp_scroll(&app);
            ui_draw(&app);
        }
    }

    ff_app_destroy(&app);
    return 0;
}
