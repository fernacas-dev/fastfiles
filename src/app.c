#include "app.h"
#include "fs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

const char *ff_find_font(void)
{
    static const char *candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        NULL,
    };
    for (size_t i = 0; candidates[i]; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0) {
            return candidates[i];
        }
    }
    return NULL;
}

static void history_push(struct ff_app *app, const char *path)
{
    /* drop forward history */
    if (app->history.count > 0 && app->history.pos + 1 < app->history.count) {
        for (size_t i = app->history.pos + 1; i < app->history.count; i++) {
            free(app->history.items[i]);
        }
        app->history.count = app->history.pos + 1;
    }
    if (app->history.count > 0) {
        const char *cur = app->history.items[app->history.pos];
        if (cur && strcmp(cur, path) == 0) {
            return;
        }
    }
    if (app->history.count == app->history.cap) {
        size_t ncap = app->history.cap == 0 ? 16 : app->history.cap * 2;
        char **tmp = (char **)realloc(app->history.items, ncap * sizeof(*tmp));
        if (!tmp) {
            return;
        }
        app->history.items = tmp;
        app->history.cap = ncap;
    }
    char *dup = strdup(path);
    if (!dup) {
        return;
    }
    app->history.items[app->history.count++] = dup;
    app->history.pos = app->history.count - 1;
}

void ff_app_mark_dirty(struct ff_app *app)
{
    app->dirty = true;
}

void ff_app_set_status(struct ff_app *app, const char *msg)
{
    snprintf(app->status_msg, sizeof(app->status_msg), "%s", msg ? msg : "");
    ff_app_mark_dirty(app);
}

void ff_app_set_error(struct ff_app *app, const char *title, const char *detail)
{
    app->dialog = FF_DLG_ERROR;
    app->prev_focus = app->focus;
    app->focus = FF_FOCUS_DIALOG;
    snprintf(app->dialog_msg, sizeof(app->dialog_msg), "%.200s\n\n%.700s",
             title ? title : "Error", detail ? detail : "");
    ff_app_mark_dirty(app);
}

static char *resolve_start(const char *start)
{
    char real[PATH_MAX];
    struct stat st;
    if (!start || !start[0]) {
        const char *home = getenv("HOME");
        if (!home || !home[0]) {
            home = "/";
        }
        return strdup(home);
    }
    if (stat(start, &st) != 0) {
        return strdup(start);
    }
    if (S_ISDIR(st.st_mode)) {
        if (realpath(start, real)) {
            return strdup(real);
        }
        return strdup(start);
    }
    /* file: open externally later; start in its dir */
    char *dup = strdup(start);
    if (!dup) {
        return strdup("/");
    }
    char *slash = strrchr(dup, '/');
    if (slash) {
        if (slash == dup) {
            dup[1] = '\0';
        } else {
            *slash = '\0';
        }
        char *out = strdup(dup);
        free(dup);
        return out ? out : strdup("/");
    }
    free(dup);
    const char *home = getenv("HOME");
    return strdup(home && home[0] ? home : "/");
}

int ff_app_init(struct ff_app *app, const char *config_path_override, const char *start_path)
{
    memset(app, 0, sizeof(*app));
    char cfgpath[1024];
    const char *cfg_to_load = NULL;
    if (config_path_override) {
        cfg_to_load = config_path_override;
    } else if (ff_config_default_path(cfgpath, sizeof(cfgpath)) == 0) {
        cfg_to_load = cfgpath;
    }
    ff_config_load(&app->cfg, cfg_to_load);
    app->show_hidden = app->cfg.show_hidden;

    app->current_path = resolve_start(start_path);
    if (!app->current_path) {
        return -1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        free(app->current_path);
        return -1;
    }
    app->window = SDL_CreateWindow("FastFiles", 1000, 650,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        free(app->current_path);
        return -1;
    }
    app->renderer = SDL_CreateRenderer(app->window, NULL);
    if (!app->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(app->window);
        SDL_Quit();
        free(app->current_path);
        return -1;
    }
    SDL_SetRenderVSync(app->renderer, 1);

    const char *fpath = app->cfg.font_path[0] ? app->cfg.font_path : ff_find_font();
    if (!fpath) {
        fprintf(stderr, "No font found. Install DejaVuSans.\n");
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        SDL_Quit();
        free(app->current_path);
        return -1;
    }
    app->font = ff_font_open(app->renderer, fpath, app->cfg.font_size);
    int small_px = app->cfg.font_size > 3 ? app->cfg.font_size - 2 : app->cfg.font_size;
    app->font_small = ff_font_open(app->renderer, fpath, small_px);
    if (!app->font || !app->font_small) {
        fprintf(stderr, "Failed to load font %s\n", fpath);
        if (app->font) {
            ff_font_close(app->font);
        }
        if (app->font_small) {
            ff_font_close(app->font_small);
        }
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        SDL_Quit();
        free(app->current_path);
        return -1;
    }

    SDL_GetWindowSize(app->window, &app->win_w, &app->win_h);
    ff_app_sidebar_init(app);
    snprintf(app->path_edit, sizeof(app->path_edit), "%s", app->current_path);
    history_push(app, app->current_path);
    app->focus = FF_FOCUS_FILES;
    app->dirty = true;
    if (ff_app_refresh(app) != 0) {
        /* fall back to HOME */
        const char *home = getenv("HOME");
        if (home && strcmp(app->current_path, home) != 0) {
            free(app->current_path);
            app->current_path = strdup(home);
            history_push(app, app->current_path);
            ff_app_refresh(app);
        }
    }
    return 0;
}

void ff_app_destroy(struct ff_app *app)
{
    ff_free_entries(app->entries, app->entry_count);
    free(app->current_path);
    free(app->selected);
    for (size_t i = 0; i < app->history.count; i++) {
        free(app->history.items[i]);
    }
    free(app->history.items);
    for (size_t i = 0; i < app->clipboard.count; i++) {
        free(app->clipboard.paths[i]);
    }
    free(app->clipboard.paths);
    if (app->font) {
        ff_font_close(app->font);
    }
    if (app->font_small) {
        ff_font_close(app->font_small);
    }
    if (app->renderer) {
        SDL_DestroyRenderer(app->renderer);
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
    }
    SDL_Quit();
}

void ff_app_sidebar_init(struct ff_app *app)
{
    app->sidebar_count = 0;
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        home = "/";
    }
    struct {
        const char *label;
        const char *env;
        const char *fallback;
    } defs[] = {
        {"Home", NULL, NULL},
        {"Desktop", "XDG_DESKTOP_DIR", "Desktop"},
        {"Documents", "XDG_DOCUMENTS_DIR", "Documents"},
        {"Downloads", "XDG_DOWNLOAD_DIR", "Downloads"},
        {"Pictures", "XDG_PICTURES_DIR", "Pictures"},
        {"Music", "XDG_MUSIC_DIR", "Music"},
        {"Videos", "XDG_VIDEOS_DIR", "Videos"},
    };
    size_t n = sizeof(defs) / sizeof(defs[0]);
    for (size_t i = 0; i < n && app->sidebar_count < 8; i++) {
        char path[1024];
        if (defs[i].env == NULL) {
            snprintf(path, sizeof(path), "%s", home);
        } else {
            const char *e = getenv(defs[i].env);
            if (e && e[0] == '/') {
                snprintf(path, sizeof(path), "%s", e);
            } else {
                snprintf(path, sizeof(path), "%s/%s", home, defs[i].fallback);
            }
        }
        struct ff_sidebar_item *it = &app->sidebar[app->sidebar_count++];
        snprintf(it->label, sizeof(it->label), "%s", defs[i].label);
        snprintf(it->path, sizeof(it->path), "%s", path);
    }
}

int ff_app_refresh(struct ff_app *app)
{
    ff_free_entries(app->entries, app->entry_count);
    app->entries = NULL;
    app->entry_count = 0;
    const char *filter = app->search[0] ? app->search : NULL;
    if (ff_list_directory(app->current_path, &app->entries, &app->entry_count,
                          app->show_hidden, filter) != 0) {
        int e = errno;
        char detail[512];
        snprintf(detail, sizeof(detail), "%.400s: %s", app->current_path, ff_strerror(e));
        ff_app_clear_selection(app);
        app->cursor = 0;
        ff_app_set_error(app, "Unable to open directory", detail);
        ff_app_set_status(app, detail);
        ff_app_mark_dirty(app);
        return -1;
    }
    ff_app_clear_selection(app);
    app->cursor = 0;
    app->scroll_offset = 0.0f;
    snprintf(app->path_edit, sizeof(app->path_edit), "%s", app->current_path);
    if (!app->status_msg[0]) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%lu items", (unsigned long)app->entry_count);
        ff_app_set_status(app, msg);
    } else {
        ff_app_mark_dirty(app);
    }
    ff_app_mark_dirty(app);
    return 0;
}

int ff_app_navigate(struct ff_app *app, const char *path)
{
    char real[PATH_MAX];
    const char *target = path;
    if (realpath(path, real)) {
        target = real;
    }
    struct stat st;
    if (stat(target, &st) != 0) {
        char detail[1024];
        snprintf(detail, sizeof(detail), "%s: %s", path, ff_strerror(errno));
        ff_app_set_error(app, "Unable to open path", detail);
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return ff_app_open_file_external(target);
    }
    char *dup = strdup(target);
    if (!dup) {
        return -1;
    }
    free(app->current_path);
    app->current_path = dup;
    app->search[0] = '\0';
    app->status_msg[0] = '\0';
    history_push(app, app->current_path);
    app->menu_open = false;
    return ff_app_refresh(app);
}

int ff_app_back(struct ff_app *app)
{
    if (app->history.pos == 0 || app->history.count == 0) {
        return -1;
    }
    app->history.pos--;
    free(app->current_path);
    app->current_path = strdup(app->history.items[app->history.pos]);
    if (!app->current_path) {
        return -1;
    }
    app->search[0] = '\0';
    app->menu_open = false;
    return ff_app_refresh(app);
}

int ff_app_forward(struct ff_app *app)
{
    if (app->history.count == 0 || app->history.pos + 1 >= app->history.count) {
        return -1;
    }
    app->history.pos++;
    free(app->current_path);
    app->current_path = strdup(app->history.items[app->history.pos]);
    if (!app->current_path) {
        return -1;
    }
    app->search[0] = '\0';
    app->menu_open = false;
    return ff_app_refresh(app);
}

int ff_app_up(struct ff_app *app)
{
    if (strcmp(app->current_path, "/") == 0) {
        return -1;
    }
    char *dup = strdup(app->current_path);
    if (!dup) {
        return -1;
    }
    /* strip trailing slashes */
    size_t len = strlen(dup);
    while (len > 1 && dup[len - 1] == '/') {
        dup[--len] = '\0';
    }
    char *slash = strrchr(dup, '/');
    if (!slash) {
        free(dup);
        return -1;
    }
    if (slash == dup) {
        dup[1] = '\0';
    } else {
        *slash = '\0';
    }
    int r = ff_app_navigate(app, dup);
    free(dup);
    return r;
}

int ff_app_home(struct ff_app *app)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        home = "/";
    }
    return ff_app_navigate(app, home);
}

void ff_app_clear_selection(struct ff_app *app)
{
    app->sel_count = 0;
}

void ff_app_select_only(struct ff_app *app, size_t idx)
{
    if (app->sel_cap == 0) {
        app->sel_cap = 8;
        app->selected = (size_t *)malloc(app->sel_cap * sizeof(*app->selected));
        if (!app->selected) {
            app->sel_cap = 0;
            return;
        }
    }
    app->sel_count = 1;
    app->selected[0] = idx;
    app->cursor = idx;
}

void ff_app_select_all(struct ff_app *app)
{
    if (app->entry_count == 0) {
        return;
    }
    if (app->sel_cap < app->entry_count) {
        size_t *tmp =
            (size_t *)realloc(app->selected, app->entry_count * sizeof(*tmp));
        if (!tmp) {
            return;
        }
        app->selected = tmp;
        app->sel_cap = app->entry_count;
    }
    for (size_t i = 0; i < app->entry_count; i++) {
        app->selected[i] = i;
    }
    app->sel_count = app->entry_count;
}

bool ff_app_is_selected(const struct ff_app *app, size_t idx)
{
    for (size_t i = 0; i < app->sel_count; i++) {
        if (app->selected[i] == idx) {
            return true;
        }
    }
    return false;
}

const struct ff_entry *ff_app_cursor_entry(const struct ff_app *app)
{
    if (app->entry_count == 0 || app->cursor >= app->entry_count) {
        return NULL;
    }
    return &app->entries[app->cursor];
}

static void clipboard_clear(struct ff_app *app)
{
    for (size_t i = 0; i < app->clipboard.count; i++) {
        free(app->clipboard.paths[i]);
    }
    free(app->clipboard.paths);
    app->clipboard.paths = NULL;
    app->clipboard.count = 0;
}

static int clipboard_set(struct ff_app *app, bool cut)
{
    if (app->sel_count == 0 && app->entry_count > 0) {
        ff_app_select_only(app, app->cursor);
    }
    if (app->sel_count == 0) {
        return -1;
    }
    clipboard_clear(app);
    app->clipboard.paths =
        (char **)malloc(app->sel_count * sizeof(*app->clipboard.paths));
    if (!app->clipboard.paths) {
        return -1;
    }
    for (size_t i = 0; i < app->sel_count; i++) {
        size_t idx = app->selected[i];
        if (idx >= app->entry_count) {
            continue;
        }
        app->clipboard.paths[app->clipboard.count] = strdup(app->entries[idx].path);
        if (!app->clipboard.paths[app->clipboard.count]) {
            continue;
        }
        app->clipboard.count++;
    }
    app->clipboard.cut = cut;
    ff_app_set_status(app, cut ? "Cut to clipboard" : "Copied to clipboard");
    return 0;
}

void ff_app_clipboard_copy(struct ff_app *app)
{
    (void)clipboard_set(app, false);
}

void ff_app_clipboard_cut(struct ff_app *app)
{
    (void)clipboard_set(app, true);
}

static const char *basename_of(const char *p)
{
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}

int ff_app_clipboard_paste(struct ff_app *app)
{
    if (app->clipboard.count == 0) {
        ff_app_set_status(app, "Clipboard is empty");
        return -1;
    }
    char msg[512];
    snprintf(msg, sizeof(msg), "%s %lu item(s)...", app->clipboard.cut ? "Moving" : "Copying",
             (unsigned long)app->clipboard.count);
    ff_app_set_status(app, msg);
    ff_app_mark_dirty(app);
    int failures = 0;
    for (size_t i = 0; i < app->clipboard.count; i++) {
        const char *src = app->clipboard.paths[i];
        char *dst = ff_join_path(app->current_path, basename_of(src));
        if (!dst) {
            failures++;
            continue;
        }
        if (strcmp(src, dst) == 0) {
            free(dst);
            continue;
        }
        if (ff_exists(dst)) {
            /* avoid silent overwrite: append " (copy)" */
            char alt[4096];
            snprintf(alt, sizeof(alt), "%s (copy)", dst);
            free(dst);
            dst = strdup(alt);
            if (!dst) {
                failures++;
                continue;
            }
            if (ff_exists(dst)) {
                free(dst);
                failures++;
                continue;
            }
        }
        int r = app->clipboard.cut ? ff_move_path(src, dst) : ff_copy_path(src, dst);
        free(dst);
        if (r != 0) {
            failures++;
        }
    }
    if (app->clipboard.cut && failures == 0) {
        clipboard_clear(app);
    }
    ff_app_refresh(app);
    if (failures > 0) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%d item(s) failed: %s", failures,
                 ff_strerror(errno));
        ff_app_set_error(app, "Paste failed", detail);
        return -1;
    }
    ff_app_set_status(app, "Paste complete");
    return 0;
}

int ff_app_create_folder(struct ff_app *app, const char *name)
{
    if (!name || !name[0] || strchr(name, '/') || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        ff_app_set_error(app, "Unable to create folder", "Invalid folder name");
        return -1;
    }
    char *full = ff_join_path(app->current_path, name);
    if (!full) {
        return -1;
    }
    int r = ff_create_directory(full);
    if (r != 0) {
        char detail[1024];
        snprintf(detail, sizeof(detail), "%s: %s", full, ff_strerror(errno));
        ff_app_set_error(app, "Unable to create folder", detail);
        free(full);
        return -1;
    }
    free(full);
    ff_app_refresh(app);
    ff_app_set_status(app, "Folder created");
    return 0;
}

int ff_app_rename_index(struct ff_app *app, size_t idx, const char *new_name)
{
    if (idx >= app->entry_count || !new_name || !new_name[0] ||
        strchr(new_name, '/')) {
        ff_app_set_error(app, "Unable to rename", "Invalid name");
        return -1;
    }
    char *dst = ff_join_path(app->current_path, new_name);
    if (!dst) {
        return -1;
    }
    if (ff_exists(dst)) {
        ff_app_set_error(app, "Unable to rename", "Destination already exists");
        free(dst);
        return -1;
    }
    int r = ff_rename_path(app->entries[idx].path, dst);
    if (r != 0) {
        char detail[1024];
        snprintf(detail, sizeof(detail), "%s", ff_strerror(errno));
        ff_app_set_error(app, "Unable to rename file", detail);
        free(dst);
        return -1;
    }
    free(dst);
    ff_app_refresh(app);
    ff_app_set_status(app, "Renamed");
    return 0;
}

int ff_app_delete_selected(struct ff_app *app)
{
    if (app->sel_count == 0) {
        return -1;
    }
    int failures = 0;
    /* delete in descending index order to keep indices valid */
    for (size_t k = app->sel_count; k > 0; k--) {
        size_t idx = app->selected[k - 1];
        if (idx >= app->entry_count) {
            continue;
        }
        if (ff_delete_path(app->entries[idx].path, true) != 0) {
            failures++;
        }
    }
    ff_app_refresh(app);
    if (failures > 0) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%d item(s) could not be deleted: %s", failures,
                 ff_strerror(errno));
        ff_app_set_error(app, "Delete failed", detail);
        return -1;
    }
    ff_app_set_status(app, "Deleted");
    return 0;
}

int ff_app_open_file_external(const char *path)
{
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        /* child: detach and exec xdg-open (no system()/shell) */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            (void)dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        /* close stdout/stderr? keep for debugging but redirect */
        pid_t g = fork();
        if (g < 0) {
            _exit(127);
        }
        if (g > 0) {
            _exit(0);
        }
        (void)setsid();
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return 0;
}

int ff_app_open_index(struct ff_app *app, size_t idx)
{
    if (idx >= app->entry_count) {
        return -1;
    }
    const struct ff_entry *e = &app->entries[idx];
    if (e->is_dir) {
        return ff_app_navigate(app, e->path);
    }
    if (e->kind == FF_KIND_SPECIAL) {
        ff_app_set_error(app, "Unable to open file", "Unsupported file type");
        return -1;
    }
    ff_app_set_status(app, "Opening...");
    return ff_app_open_file_external(e->path);
}
