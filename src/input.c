#include "input.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void ff_input_clamp_scroll(struct ff_app *app)
{
    SDL_Rect v;
    ui_file_view_rect(app, &v);
    int cols = 1, cw = 112, ch = 100;
    int rows = ui_content_rows(app, &cols, &cw, &ch);
    int total = rows * ch + 24;
    int maxs = total - v.h;
    if (maxs < 0) {
        maxs = 0;
    }
    if (app->scroll_offset < 0.0f) {
        app->scroll_offset = 0.0f;
    }
    if (app->scroll_offset > (float)maxs) {
        app->scroll_offset = (float)maxs;
    }
}

static void ensure_cursor_visible(struct ff_app *app)
{
    SDL_Rect v;
    ui_file_view_rect(app, &v);
    int cols = 1, cw = 112, ch = 100;
    ui_content_rows(app, &cols, &cw, &ch);
    if (app->entry_count == 0) {
        return;
    }
    if (app->cursor >= app->entry_count) {
        app->cursor = app->entry_count - 1;
    }
    size_t row = app->cursor / (size_t)cols;
    int y0 = v.y + 12;
    int top = y0 + (int)row * ch - (int)app->scroll_offset;
    if (top < v.y + 8) {
        app->scroll_offset = (float)((int)row * ch + 12 - 8);
    } else if (top + ch > v.y + v.h - 8) {
        app->scroll_offset = (float)((int)row * ch + ch + 12 - (v.h - 8));
    }
    ff_input_clamp_scroll(app);
}

/* remove last UTF-8 char */
static void utf8_backspace(char *s)
{
    size_t len = strlen(s);
    if (len == 0) {
        return;
    }
    size_t i = len - 1;
    while (i > 0 && (((unsigned char)s[i] & 0xC0u) == 0x80u)) {
        i--;
    }
    s[i] = '\0';
}

static void append_text(char *dst, size_t cap, const char *src)
{
    size_t dl = strlen(dst);
    size_t sl = strlen(src);
    if (dl + sl + 1 > cap) {
        return;
    }
    memcpy(dst + dl, src, sl + 1);
}

static void open_dialog_new_folder(struct ff_app *app)
{
    app->dialog = FF_DLG_NEW_FOLDER;
    app->prev_focus = app->focus;
    app->focus = FF_FOCUS_DIALOG;
    app->dialog_input[0] = '\0';
    app->menu_open = false;
    SDL_StartTextInput(app->window);
    ff_app_mark_dirty(app);
}

static void open_dialog_rename(struct ff_app *app, size_t idx)
{
    if (idx >= app->entry_count) {
        return;
    }
    app->dialog = FF_DLG_RENAME;
    app->prev_focus = app->focus;
    app->focus = FF_FOCUS_DIALOG;
    snprintf(app->dialog_input, sizeof(app->dialog_input), "%s", app->entries[idx].name);
    app->menu_open = false;
    SDL_StartTextInput(app->window);
    ff_app_mark_dirty(app);
}

static void open_dialog_confirm_delete(struct ff_app *app)
{
    if (app->sel_count == 0 && app->entry_count > 0) {
        ff_app_select_only(app, app->cursor);
    }
    if (app->sel_count == 0) {
        return;
    }
    if (!app->cfg.confirm_delete) {
        (void)ff_app_delete_selected(app);
        return;
    }
    app->dialog = FF_DLG_CONFIRM_DELETE;
    app->prev_focus = app->focus;
    app->focus = FF_FOCUS_DIALOG;
    if (app->sel_count == 1 && app->selected[0] < app->entry_count) {
        snprintf(app->dialog_msg, sizeof(app->dialog_msg), "Delete \"%s\"?\n\nThis cannot be undone.",
                 app->entries[app->selected[0]].name);
    } else {
        snprintf(app->dialog_msg, sizeof(app->dialog_msg), "Delete %lu items?\n\nThis cannot be undone.",
                 (unsigned long)app->sel_count);
    }
    app->menu_open = false;
    ff_app_mark_dirty(app);
}

static void open_dialog_properties(struct ff_app *app, size_t idx)
{
    if (idx >= app->entry_count) {
        return;
    }
    const struct ff_entry *e = &app->entries[idx];
    const char *kind = "File";
    switch (e->kind) {
    case FF_KIND_DIR:
        kind = "Folder";
        break;
    case FF_KIND_TEXT:
        kind = "Text file";
        break;
    case FF_KIND_IMAGE:
        kind = "Image";
        break;
    case FF_KIND_AUDIO:
        kind = "Audio";
        break;
    case FF_KIND_VIDEO:
        kind = "Video";
        break;
    case FF_KIND_ARCHIVE:
        kind = "Archive";
        break;
    case FF_KIND_LINK:
        kind = "Symbolic link";
        break;
    case FF_KIND_SPECIAL:
        kind = "Special file";
        break;
    default:
        break;
    }
    char tbuf[64];
    struct tm tmv;
    localtime_r(&e->modified, &tmv);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &tmv);
    char sbuf[64];
    if (e->is_dir) {
        snprintf(sbuf, sizeof(sbuf), "--");
    } else if (e->size >= 1000000000UL) {
        snprintf(sbuf, sizeof(sbuf), "%.2f GB", (double)e->size / 1e9);
    } else if (e->size >= 1000000UL) {
        snprintf(sbuf, sizeof(sbuf), "%.2f MB", (double)e->size / 1e6);
    } else if (e->size >= 1000UL) {
        snprintf(sbuf, sizeof(sbuf), "%.1f KB", (double)e->size / 1e3);
    } else {
        snprintf(sbuf, sizeof(sbuf), "%lu B", (unsigned long)e->size);
    }
    snprintf(app->dialog_msg, sizeof(app->dialog_msg), "%s\nType: %s\nSize: %s\nModified: %s\nPath: %s",
             e->name, kind, sbuf, tbuf, e->path);
    app->dialog = FF_DLG_PROPERTIES;
    app->prev_focus = app->focus;
    app->focus = FF_FOCUS_DIALOG;
    app->menu_open = false;
    ff_app_mark_dirty(app);
}

static void close_dialog(struct ff_app *app, bool restore_text_input)
{
    app->dialog = FF_DLG_NONE;
    app->focus = app->prev_focus;
    if (app->focus != FF_FOCUS_PATH && app->focus != FF_FOCUS_SEARCH &&
        app->focus != FF_FOCUS_DIALOG) {
        if (!restore_text_input) {
            SDL_StopTextInput(app->window);
        }
    }
    ff_app_mark_dirty(app);
}

static void dialog_confirm(struct ff_app *app)
{
    switch (app->dialog) {
    case FF_DLG_NEW_FOLDER:
        if (app->dialog_input[0]) {
            ff_app_create_folder(app, app->dialog_input);
        }
        close_dialog(app, false);
        SDL_StopTextInput(app->window);
        if (app->focus == FF_FOCUS_FILES) {
            SDL_StopTextInput(app->window);
        }
        break;
    case FF_DLG_RENAME: {
        size_t idx = app->sel_count > 0 ? app->selected[app->sel_count - 1] : app->cursor;
        if (app->dialog_input[0]) {
            ff_app_rename_index(app, idx, app->dialog_input);
        }
        close_dialog(app, false);
        SDL_StopTextInput(app->window);
        break;
    }
    case FF_DLG_CONFIRM_DELETE:
        close_dialog(app, false);
        (void)ff_app_delete_selected(app);
        break;
    case FF_DLG_ERROR:
    case FF_DLG_PROPERTIES:
        close_dialog(app, false);
        break;
    default:
        break;
    }
    ff_app_mark_dirty(app);
}

static void dispatch_menu(struct ff_app *app, int action)
{
    int target = app->menu_target;
    app->menu_open = false;
    ff_app_mark_dirty(app);
    switch (action) {
    case FF_MENU_OPEN:
        if (target >= 0) {
            ff_app_select_only(app, (size_t)target);
            (void)ff_app_open_index(app, (size_t)target);
        }
        break;
    case FF_MENU_COPY:
        if (target >= 0) {
            ff_app_select_only(app, (size_t)target);
            ff_app_clipboard_copy(app);
        }
        break;
    case FF_MENU_CUT:
        if (target >= 0) {
            ff_app_select_only(app, (size_t)target);
            ff_app_clipboard_cut(app);
        }
        break;
    case FF_MENU_PASTE:
        (void)ff_app_clipboard_paste(app);
        break;
    case FF_MENU_RENAME:
        if (target >= 0) {
            ff_app_select_only(app, (size_t)target);
            open_dialog_rename(app, (size_t)target);
        }
        break;
    case FF_MENU_DELETE:
        if (target >= 0) {
            ff_app_select_only(app, (size_t)target);
        }
        open_dialog_confirm_delete(app);
        break;
    case FF_MENU_PROPERTIES:
        if (target >= 0) {
            open_dialog_properties(app, (size_t)target);
        }
        break;
    case FF_MENU_NEW_FOLDER:
        open_dialog_new_folder(app);
        break;
    case FF_MENU_REFRESH:
        (void)ff_app_refresh(app);
        break;
    default:
        break;
    }
}

static void focus_path(struct ff_app *app)
{
    app->prev_focus = app->focus;
    app->focus = FF_FOCUS_PATH;
    snprintf(app->path_edit, sizeof(app->path_edit), "%s", app->current_path);
    app->path_editing = true;
    SDL_StartTextInput(app->window);
    ff_app_mark_dirty(app);
}

static void focus_search(struct ff_app *app)
{
    app->prev_focus = app->focus;
    app->focus = FF_FOCUS_SEARCH;
    SDL_StartTextInput(app->window);
    ff_app_mark_dirty(app);
}

static void focus_files(struct ff_app *app)
{
    app->focus = FF_FOCUS_FILES;
    app->path_editing = false;
    SDL_StopTextInput(app->window);
    ff_app_mark_dirty(app);
}

static void handle_key(struct ff_app *app, const SDL_Event *e)
{
    SDL_Keycode kc = e->key.key;
    SDL_Keymod mod = e->key.mod;
    bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    bool shift = (mod & SDL_KMOD_SHIFT) != 0;

    /* Dialog focus: limited keys */
    if (app->focus == FF_FOCUS_DIALOG) {
        if (kc == SDLK_ESCAPE) {
            close_dialog(app, false);
            SDL_StopTextInput(app->window);
            return;
        }
        if (kc == SDLK_RETURN || kc == SDLK_KP_ENTER) {
            dialog_confirm(app);
            return;
        }
        if (app->dialog == FF_DLG_NEW_FOLDER || app->dialog == FF_DLG_RENAME) {
            if (kc == SDLK_BACKSPACE) {
                utf8_backspace(app->dialog_input);
                ff_app_mark_dirty(app);
                return;
            }
        }
        return;
    }

    /* Global shortcuts with Ctrl */
    if (ctrl && !shift) {
        switch (kc) {
        case SDLK_L:
            focus_path(app);
            return;
        case SDLK_F:
            focus_search(app);
            return;
        case SDLK_C:
            ff_app_clipboard_copy(app);
            return;
        case SDLK_X:
            ff_app_clipboard_cut(app);
            return;
        case SDLK_V:
            (void)ff_app_clipboard_paste(app);
            return;
        case SDLK_A:
            ff_app_select_all(app);
            ff_app_mark_dirty(app);
            return;
        case SDLK_H:
            app->show_hidden = !app->show_hidden;
            (void)ff_app_refresh(app);
            ff_app_set_status(app, app->show_hidden ? "Hidden files shown" : "Hidden files hidden");
            return;
        case SDLK_Q:
            app->quit = true;
            return;
        default:
            break;
        }
    }
    if (ctrl && shift && kc == SDLK_N) {
        open_dialog_new_folder(app);
        return;
    }

    /* Path field */
    if (app->focus == FF_FOCUS_PATH) {
        if (kc == SDLK_ESCAPE) {
            focus_files(app);
            snprintf(app->path_edit, sizeof(app->path_edit), "%s", app->current_path);
            return;
        }
        if (kc == SDLK_RETURN || kc == SDLK_KP_ENTER) {
            char tmp[4096];
            snprintf(tmp, sizeof(tmp), "%s", app->path_edit);
            focus_files(app);
            (void)ff_app_navigate(app, tmp);
            return;
        }
        if (kc == SDLK_BACKSPACE) {
            utf8_backspace(app->path_edit);
            ff_app_mark_dirty(app);
            return;
        }
        return;
    }

    /* Search field */
    if (app->focus == FF_FOCUS_SEARCH) {
        if (kc == SDLK_ESCAPE) {
            app->search[0] = '\0';
            focus_files(app);
            (void)ff_app_refresh(app);
            return;
        }
        if (kc == SDLK_RETURN || kc == SDLK_KP_ENTER) {
            focus_files(app);
            return;
        }
        if (kc == SDLK_BACKSPACE) {
            utf8_backspace(app->search);
            (void)ff_app_refresh(app);
            return;
        }
        return;
    }

    /* Files focus */
    switch (kc) {
    case SDLK_Q:
        break;
    case SDLK_ESCAPE:
        if (app->menu_open) {
            app->menu_open = false;
            ff_app_mark_dirty(app);
        } else if (app->search[0]) {
            app->search[0] = '\0';
            (void)ff_app_refresh(app);
        } else {
            ff_app_clear_selection(app);
            ff_app_mark_dirty(app);
        }
        return;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (app->entry_count > 0) {
            size_t idx = app->sel_count > 0 ? app->selected[app->sel_count - 1] : app->cursor;
            (void)ff_app_open_index(app, idx);
        }
        return;
    case SDLK_BACKSPACE:
        (void)ff_app_up(app);
        return;
    case SDLK_F5:
        (void)ff_app_refresh(app);
        return;
    case SDLK_F2: {
        size_t idx = app->sel_count > 0 ? app->selected[app->sel_count - 1] : app->cursor;
        if (app->entry_count > 0) {
            ff_app_select_only(app, idx);
            open_dialog_rename(app, idx);
        }
        return;
    }
    case SDLK_DELETE:
        open_dialog_confirm_delete(app);
        return;
    case SDLK_LEFT:
        if (ctrl) {
            (void)ff_app_back(app);
            return;
        }
        if (app->entry_count == 0) {
            return;
        }
        {
            int cols = 1, cw = 0, ch = 0;
            ui_content_rows(app, &cols, &cw, &ch);
            if (app->cursor >= (size_t)cols) {
                app->cursor -= (size_t)cols;
            } else {
                app->cursor = 0;
            }
            ff_app_select_only(app, app->cursor);
            ensure_cursor_visible(app);
            ff_app_mark_dirty(app);
        }
        return;
    case SDLK_RIGHT:
        if (ctrl) {
            (void)ff_app_forward(app);
            return;
        }
        if (app->entry_count == 0) {
            return;
        }
        {
            int cols = 1, cw = 0, ch = 0;
            ui_content_rows(app, &cols, &cw, &ch);
            if (app->cursor + (size_t)cols < app->entry_count) {
                app->cursor += (size_t)cols;
            } else {
                app->cursor = app->entry_count - 1;
            }
            ff_app_select_only(app, app->cursor);
            ensure_cursor_visible(app);
            ff_app_mark_dirty(app);
        }
        return;
    case SDLK_UP:
        if (app->entry_count == 0) {
            return;
        }
        if (app->cursor > 0) {
            app->cursor--;
        }
        ff_app_select_only(app, app->cursor);
        ensure_cursor_visible(app);
        ff_app_mark_dirty(app);
        return;
    case SDLK_DOWN:
        if (app->entry_count == 0) {
            return;
        }
        if (app->cursor + 1 < app->entry_count) {
            app->cursor++;
        }
        ff_app_select_only(app, app->cursor);
        ensure_cursor_visible(app);
        ff_app_mark_dirty(app);
        return;
    default:
        break;
    }
}

void ff_input_handle_event(struct ff_app *app, const SDL_Event *e)
{
    switch (e->type) {
    case SDL_EVENT_QUIT:
        app->quit = true;
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        app->win_w = e->window.data1;
        app->win_h = e->window.data2;
        ff_input_clamp_scroll(app);
        ff_app_mark_dirty(app);
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
        ff_app_mark_dirty(app);
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        SDL_Rect v;
        ui_file_view_rect(app, &v);
        float mx = 0.0f, my = 0.0f;
        (void)SDL_GetMouseState(&mx, &my);
        if (mx >= (float)v.x && mx < (float)(v.x + v.w) && my >= (float)v.y &&
            my < (float)(v.y + v.h)) {
            app->scroll_offset -= e->wheel.y * 60.0f;
            ff_input_clamp_scroll(app);
            ff_app_mark_dirty(app);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int mx = (int)e->button.x;
        int my = (int)e->button.y;
        Uint8 btn = e->button.button;
        int clicks = (int)e->button.clicks;

        /* dialog clicks first */
        if (app->dialog != FF_DLG_NONE) {
            int b = ui_dialog_button_at(app, mx, my);
            if (b == 0) {
                close_dialog(app, false);
                SDL_StopTextInput(app->window);
            } else if (b == 1) {
                dialog_confirm(app);
            }
            break;
        }
        /* open menu: click inside dispatches, outside closes */
        if (app->menu_open) {
            int act = ui_menu_action_at(app, mx, my);
            if (act != FF_MENU_NONE) {
                dispatch_menu(app, act);
            } else {
                app->menu_open = false;
                ff_app_mark_dirty(app);
            }
            break;
        }
        if (btn == SDL_BUTTON_RIGHT) {
            /* context menu only applies to the file view */
            SDL_Rect v;
            ui_file_view_rect(app, &v);
            if (mx < v.x || mx >= v.x + v.w || my < v.y || my >= v.y + v.h) {
                break;
            }
            int fi = ui_file_index_at(app, mx, my);
            app->menu_x = mx;
            app->menu_y = my;
            app->menu_target = fi;
            if (fi >= 0) {
                ff_app_select_only(app, (size_t)fi);
            }
            app->menu_open = true;
            ff_app_mark_dirty(app);
            break;
        }
        if (btn != SDL_BUTTON_LEFT) {
            break;
        }
        SDL_Rect back, fwd, up, path, search;
        ui_header_rects(app, &back, &fwd, &up, &path, &search);
        if (mx >= back.x && mx < back.x + back.w && my >= back.y && my < back.y + back.h) {
            (void)ff_app_back(app);
            break;
        }
        if (mx >= fwd.x && mx < fwd.x + fwd.w && my >= fwd.y && my < fwd.y + fwd.h) {
            (void)ff_app_forward(app);
            break;
        }
        if (mx >= up.x && mx < up.x + up.w && my >= up.y && my < up.y + up.h) {
            (void)ff_app_up(app);
            break;
        }
        if (mx >= path.x && mx < path.x + path.w && my >= path.y && my < path.y + path.h) {
            focus_path(app);
            break;
        }
        if (mx >= search.x && mx < search.x + search.w && my >= search.y &&
            my < search.y + search.h) {
            focus_search(app);
            break;
        }
        int si = ui_sidebar_index_at(app, mx, my);
        if (si >= 0) {
            focus_files(app);
            (void)ff_app_navigate(app, app->sidebar[(size_t)si].path);
            break;
        }
        int fi = ui_file_index_at(app, mx, my);
        if (fi >= 0) {
            if (app->focus != FF_FOCUS_FILES) {
                focus_files(app);
            }
            ff_app_select_only(app, (size_t)fi);
            ff_app_mark_dirty(app);
            if (clicks >= 2) {
                (void)ff_app_open_index(app, (size_t)fi);
            }
        } else {
            SDL_Rect v;
            ui_file_view_rect(app, &v);
            if (mx >= v.x && my >= v.y && mx < v.x + v.w && my < v.y + v.h) {
                if (app->focus != FF_FOCUS_FILES) {
                    focus_files(app);
                } else {
                    ff_app_clear_selection(app);
                    ff_app_mark_dirty(app);
                }
            }
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN:
        handle_key(app, e);
        break;
    case SDL_EVENT_TEXT_INPUT: {
        const char *t = e->text.text;
        if (!t || !t[0]) {
            break;
        }
        if (app->focus == FF_FOCUS_PATH) {
            append_text(app->path_edit, sizeof(app->path_edit), t);
            ff_app_mark_dirty(app);
        } else if (app->focus == FF_FOCUS_SEARCH) {
            if (strlen(app->search) + strlen(t) < sizeof(app->search)) {
                append_text(app->search, sizeof(app->search), t);
                (void)ff_app_refresh(app);
            }
        } else if (app->focus == FF_FOCUS_DIALOG &&
                   (app->dialog == FF_DLG_NEW_FOLDER || app->dialog == FF_DLG_RENAME)) {
            append_text(app->dialog_input, sizeof(app->dialog_input), t);
            ff_app_mark_dirty(app);
        }
        break;
    }
    default:
        break;
    }
}
