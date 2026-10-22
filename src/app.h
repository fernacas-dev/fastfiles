#ifndef FF_APP_H
#define FF_APP_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "font.h"
#include "fs.h"

enum ff_focus {
    FF_FOCUS_FILES = 0,
    FF_FOCUS_PATH,
    FF_FOCUS_SEARCH,
    FF_FOCUS_DIALOG
};

enum ff_dialog {
    FF_DLG_NONE = 0,
    FF_DLG_NEW_FOLDER,
    FF_DLG_RENAME,
    FF_DLG_CONFIRM_DELETE,
    FF_DLG_ERROR,
    FF_DLG_PROPERTIES
};

struct ff_history {
    char **items;
    size_t count;
    size_t cap;
    size_t pos; /* index of current */
};

struct ff_clipboard {
    char **paths;
    size_t count;
    bool cut;
};

struct ff_sidebar_item {
    char label[32];
    char path[1024];
};

struct ff_app {
    SDL_Window *window;
    SDL_Renderer *renderer;
    ff_font_t *font;
    ff_font_t *font_small;

    struct ff_config cfg;

    char *current_path;

    struct ff_entry *entries;
    size_t entry_count;

    size_t *selected; /* indices into entries */
    size_t sel_count;
    size_t sel_cap;
    size_t cursor; /* keyboard cursor index */

    float scroll_offset;

    bool show_hidden;
    char search[256];

    struct ff_history history;
    struct ff_clipboard clipboard;

    enum ff_focus focus;
    enum ff_focus prev_focus;
    enum ff_dialog dialog;
    char dialog_input[512];
    char dialog_msg[1024];

    char path_edit[4096];
    bool path_editing;

    bool menu_open;
    int menu_x;
    int menu_y;
    int menu_target; /* entry index or -1 for empty space */

    char status_msg[512];

    struct ff_sidebar_item sidebar[8];
    size_t sidebar_count;

    bool dirty;
    bool quit;
    int win_w;
    int win_h;
};

int ff_app_init(struct ff_app *app, const char *config_path_override, const char *start_path);
void ff_app_destroy(struct ff_app *app);
void ff_app_mark_dirty(struct ff_app *app);

int ff_app_navigate(struct ff_app *app, const char *path);
int ff_app_back(struct ff_app *app);
int ff_app_forward(struct ff_app *app);
int ff_app_up(struct ff_app *app);
int ff_app_home(struct ff_app *app);
int ff_app_refresh(struct ff_app *app);

void ff_app_clear_selection(struct ff_app *app);
void ff_app_select_only(struct ff_app *app, size_t idx);
void ff_app_select_all(struct ff_app *app);
bool ff_app_is_selected(const struct ff_app *app, size_t idx);
const struct ff_entry *ff_app_cursor_entry(const struct ff_app *app);

void ff_app_clipboard_copy(struct ff_app *app);
void ff_app_clipboard_cut(struct ff_app *app);
int ff_app_clipboard_paste(struct ff_app *app);

int ff_app_create_folder(struct ff_app *app, const char *name);
int ff_app_rename_index(struct ff_app *app, size_t idx, const char *new_name);
/* Deletes currently selected entries (recursive dir delete). Returns 0 ok. */
int ff_app_delete_selected(struct ff_app *app);
int ff_app_open_index(struct ff_app *app, size_t idx);
int ff_app_open_file_external(const char *path);

void ff_app_set_status(struct ff_app *app, const char *msg);
void ff_app_set_error(struct ff_app *app, const char *title, const char *detail);

void ff_app_sidebar_init(struct ff_app *app);
const char *ff_find_font(void);

#endif
