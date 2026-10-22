#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define HDR_H 48
#define STATUS_H 28
#define SIDEBAR_W 200

static SDL_Color COL_BG = {28, 28, 34, 255};
static SDL_Color COL_SURFACE = {40, 40, 48, 255};
static SDL_Color COL_SURFACE2 = {52, 52, 62, 255};
static SDL_Color COL_TEXT = {230, 230, 235, 255};
static SDL_Color COL_MUTED = {150, 150, 160, 255};
static SDL_Color COL_SEL = {58, 92, 170, 255};
static SDL_Color COL_BORDER = {62, 62, 74, 255};
static SDL_Color COL_ACCENT = {110, 150, 220, 255};

void ui_header_rects(const struct ff_app *app, SDL_Rect *back, SDL_Rect *fwd,
                     SDL_Rect *up, SDL_Rect *path, SDL_Rect *search)
{
    int w = app->win_w;
    if (back) {
        back->x = 8;
        back->y = 8;
        back->w = 36;
        back->h = 32;
    }
    if (fwd) {
        fwd->x = 48;
        fwd->y = 8;
        fwd->w = 36;
        fwd->h = 32;
    }
    if (up) {
        up->x = 88;
        up->y = 8;
        up->w = 36;
        up->h = 32;
    }
    int search_w = 180;
    if (w < 500) {
        search_w = 110;
    }
    if (search) {
        search->x = w - search_w - 8;
        search->y = 8;
        search->w = search_w;
        search->h = 32;
    }
    if (path) {
        path->x = 132;
        path->y = 8;
        path->w = w - 132 - search_w - 16;
        if (path->w < 40) {
            path->w = 40;
        }
        path->h = 32;
    }
}

void ui_file_view_rect(const struct ff_app *app, SDL_Rect *r)
{
    int sx = (app->cfg.sidebar && app->sidebar_count > 0) ? SIDEBAR_W : 0;
    r->x = sx;
    r->y = HDR_H;
    r->w = app->win_w - sx;
    r->h = app->win_h - HDR_H - STATUS_H;
    if (r->w < 0) {
        r->w = 0;
    }
    if (r->h < 0) {
        r->h = 0;
    }
}

int ui_content_rows(const struct ff_app *app, int *cols_out, int *cell_w, int *cell_h)
{
    SDL_Rect v;
    ui_file_view_rect(app, &v);
    int cw = 112;
    int ch = app->cfg.icon_size + 40;
    if (ch < 80) {
        ch = 80;
    }
    int cols = v.w > 24 ? (v.w - 24) / cw : 1;
    if (cols < 1) {
        cols = 1;
    }
    if (cols_out) {
        *cols_out = cols;
    }
    if (cell_w) {
        *cell_w = cw;
    }
    if (cell_h) {
        *cell_h = ch;
    }
    size_t n = app->entry_count;
    int rows = (int)((n + (size_t)cols - 1UL) / (size_t)cols);
    return rows;
}

int ui_sidebar_index_at(const struct ff_app *app, int mx, int my)
{
    if (!app->cfg.sidebar) {
        return -1;
    }
    if (mx < 0 || mx >= SIDEBAR_W) {
        return -1;
    }
    int y0 = HDR_H + 8;
    int ih = 32;
    int idx = (my - y0) / ih;
    if (idx < 0 || (size_t)idx >= app->sidebar_count) {
        return -1;
    }
    int top = y0 + idx * ih;
    if (my < top || my >= top + ih) {
        return -1;
    }
    return idx;
}

int ui_file_index_at(const struct ff_app *app, int mx, int my)
{
    SDL_Rect v;
    ui_file_view_rect(app, &v);
    if (mx < v.x || mx >= v.x + v.w || my < v.y || my >= v.y + v.h) {
        return -1;
    }
    int cols = 1, cw = 112, ch = 100;
    ui_content_rows(app, &cols, &cw, &ch);
    int x0 = v.x + 12;
    int y0 = v.y + 12 - (int)app->scroll_offset;
    int lx = mx - x0;
    int ly = my - y0;
    if (lx < 0 || ly < 0) {
        return -1;
    }
    int col = lx / cw;
    int row = ly / ch;
    if (col >= cols) {
        return -1;
    }
    int fx = lx - col * cw;
    int fy = ly - row * ch;
    if (fx < 0 || fx >= cw || fy < 0 || fy >= ch) {
        return -1;
    }
    long idx = (long)row * (long)cols + (long)col;
    if (idx < 0 || (size_t)idx >= app->entry_count) {
        return -1;
    }
    return (int)idx;
}

/* ---------- drawing helpers ---------- */

static void draw_rect(SDL_Renderer *r, const SDL_Rect *rc, SDL_Color c)
{
    SDL_FRect fr = {(float)rc->x, (float)rc->y, (float)rc->w, (float)rc->h};
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &fr);
}

static void draw_rect_outline(SDL_Renderer *r, const SDL_Rect *rc, SDL_Color c)
{
    SDL_FRect fr = {(float)rc->x, (float)rc->y, (float)rc->w, (float)rc->h};
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderRect(r, &fr);
}

static void draw_icon(SDL_Renderer *r, enum ff_file_kind kind, int x, int y, int s)
{
    SDL_Color folder = {232, 184, 90, 255};
    SDL_Color folder_d = {190, 145, 60, 255};
    SDL_Color file = {180, 190, 205, 255};
    SDL_Color txt = {130, 200, 140, 255};
    SDL_Color img = {140, 180, 230, 255};
    SDL_Color aud = {200, 150, 220, 255};
    SDL_Color vid = {230, 140, 140, 255};
    SDL_Color arc = {220, 200, 130, 255};
    SDL_Color lnk = {140, 220, 220, 255};
    SDL_Color spc = {120, 120, 130, 255};

    if (kind == FF_KIND_DIR) {
        SDL_FRect tab = {(float)x, (float)y, (float)s * 0.4f, (float)s * 0.16f};
        SDL_FRect body = {(float)x, (float)(y + (int)((float)s * 0.12f)), (float)s,
                          (float)s * 0.72f};
        SDL_SetRenderDrawColor(r, folder_d.r, folder_d.g, folder_d.b, 255);
        SDL_RenderFillRect(r, &tab);
        SDL_SetRenderDrawColor(r, folder.r, folder.g, folder.b, 255);
        SDL_RenderFillRect(r, &body);
        return;
    }
    SDL_Color c = file;
    switch (kind) {
    case FF_KIND_TEXT:
        c = txt;
        break;
    case FF_KIND_IMAGE:
        c = img;
        break;
    case FF_KIND_AUDIO:
        c = aud;
        break;
    case FF_KIND_VIDEO:
        c = vid;
        break;
    case FF_KIND_ARCHIVE:
        c = arc;
        break;
    case FF_KIND_LINK:
        c = lnk;
        break;
    case FF_KIND_SPECIAL:
        c = spc;
        break;
    default:
        break;
    }
    SDL_FRect body = {(float)x, (float)y, (float)s * 0.7f, (float)s};
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(r, &body);
    /* folded corner */
    int fx = x + (int)((float)s * 0.7f);
    int fy = y;
    SDL_SetRenderDrawColor(r, 28, 28, 34, 255);
    SDL_RenderLine(r, (float)(fx - 10), (float)fy, (float)fx, (float)(fy + 10));
    /* glyph-ish lines for text */
    if (kind == FF_KIND_TEXT) {
        SDL_SetRenderDrawColor(r, 40, 60, 45, 255);
        for (int i = 0; i < 3; i++) {
            float ly = (float)(y + s / 3 + i * 8);
            SDL_RenderLine(r, (float)(x + 6), ly, (float)(x + (int)((float)s * 0.7f) - 6), ly);
        }
    }
}

static void draw_text_btn(struct ff_app *app, const SDL_Rect *rc, const char *label,
                          bool active)
{
    SDL_Color bg = active ? COL_SEL : COL_SURFACE2;
    draw_rect(app->renderer, rc, bg);
    draw_rect_outline(app->renderer, rc, COL_BORDER);
    int tw = 0, th = 0;
    ff_font_measure(app->font, label, &tw, &th);
    float tx = (float)rc->x + ((float)rc->w - (float)tw) / 2.0f;
    float ty = (float)rc->y + ((float)rc->h - (float)ff_font_height(app->font)) / 2.0f;
    ff_font_draw(app->font, label, tx, ty, COL_TEXT, rc);
}

static void draw_header(struct ff_app *app)
{
    SDL_Rect hdr = {0, 0, app->win_w, HDR_H};
    draw_rect(app->renderer, &hdr, COL_SURFACE);
    SDL_Rect line = {0, HDR_H - 1, app->win_w, 1};
    draw_rect(app->renderer, &line, COL_BORDER);

    SDL_Rect back, fwd, up, path, search;
    ui_header_rects(app, &back, &fwd, &up, &path, &search);
    draw_text_btn(app, &back, "<", false);
    draw_text_btn(app, &fwd, ">", false);
    draw_text_btn(app, &up, "^", false);

    /* path field */
    bool path_focus = app->focus == FF_FOCUS_PATH;
    draw_rect(app->renderer, &path, COL_BG);
    draw_rect_outline(app->renderer, &path, path_focus ? COL_ACCENT : COL_BORDER);
    SDL_Rect clip = {path.x + 6, path.y + 4, path.w - 12, path.h - 8};
    SDL_SetRenderClipRect(app->renderer, &clip);
    const char *pt = path_focus || app->path_editing ? app->path_edit : app->current_path;
    char disp[4096];
    snprintf(disp, sizeof(disp), "%s", pt ? pt : "");
    if (path_focus && ((SDL_GetTicks() / 500) % 2 == 0)) {
        size_t l = strlen(disp);
        if (l + 2 < sizeof(disp)) {
            disp[l] = '|';
            disp[l + 1] = '\0';
        }
    }
    ff_font_draw_fit(app->font, disp, (float)(path.x + 8), (float)(path.y + 7), COL_TEXT,
                     path.w - 16, &clip);
    SDL_SetRenderClipRect(app->renderer, NULL);

    /* search field */
    bool s_focus = app->focus == FF_FOCUS_SEARCH;
    draw_rect(app->renderer, &search, COL_BG);
    draw_rect_outline(app->renderer, &search, s_focus ? COL_ACCENT : COL_BORDER);
    SDL_Rect sc = {search.x + 6, search.y + 4, search.w - 12, search.h - 8};
    SDL_SetRenderClipRect(app->renderer, &sc);
    char sbuf[300];
    if (app->search[0]) {
        snprintf(sbuf, sizeof(sbuf), "%s", app->search);
    } else {
        snprintf(sbuf, sizeof(sbuf), "Search");
    }
    SDL_Color sc_col = app->search[0] ? COL_TEXT : COL_MUTED;
    if (s_focus && ((SDL_GetTicks() / 500) % 2 == 0) && app->search[0]) {
        size_t l = strlen(sbuf);
        if (l + 2 < sizeof(sbuf)) {
            sbuf[l] = '|';
            sbuf[l + 1] = '\0';
        }
    }
    ff_font_draw_fit(app->font, sbuf, (float)(search.x + 8), (float)(search.y + 7), sc_col,
                     search.w - 16, &sc);
    SDL_SetRenderClipRect(app->renderer, NULL);
}

static void draw_sidebar(struct ff_app *app)
{
    if (!app->cfg.sidebar) {
        return;
    }
    SDL_Rect sb = {0, HDR_H, SIDEBAR_W, app->win_h - HDR_H - STATUS_H};
    draw_rect(app->renderer, &sb, COL_SURFACE);
    SDL_Rect line = {SIDEBAR_W - 1, HDR_H, 1, app->win_h - HDR_H - STATUS_H};
    draw_rect(app->renderer, &line, COL_BORDER);
    int y0 = HDR_H + 8;
    for (size_t i = 0; i < app->sidebar_count; i++) {
        SDL_Rect rc = {8, y0 + (int)i * 32, SIDEBAR_W - 16, 28};
        bool cur = app->current_path &&
                   strcmp(app->current_path, app->sidebar[i].path) == 0;
        if (cur) {
            draw_rect(app->renderer, &rc, COL_SEL);
        } else {
            draw_rect(app->renderer, &rc, COL_SURFACE);
        }
        ff_font_draw_fit(app->font, app->sidebar[i].label, (float)(rc.x + 10),
                         (float)(rc.y + 5), cur ? COL_TEXT : COL_MUTED, rc.w - 20, &rc);
    }
}

static void draw_status(struct ff_app *app)
{
    SDL_Rect st = {0, app->win_h - STATUS_H, app->win_w, STATUS_H};
    draw_rect(app->renderer, &st, COL_SURFACE);
    SDL_Rect line = {0, app->win_h - STATUS_H, app->win_w, 1};
    draw_rect(app->renderer, &line, COL_BORDER);

    char left[300];
    if (app->sel_count > 1) {
        snprintf(left, sizeof(left), "%lu items (%lu selected)", (unsigned long)app->entry_count,
                 (unsigned long)app->sel_count);
    } else {
        snprintf(left, sizeof(left), "%lu items", (unsigned long)app->entry_count);
    }
    ff_font_draw(app->font_small, left, 10.0f, (float)(app->win_h - STATUS_H + 6), COL_MUTED,
                 &st);

    /* center: status message */
    if (app->status_msg[0]) {
        int sw = 0, sh = 0;
        ff_font_measure(app->font_small, app->status_msg, &sw, &sh);
        float sx = (float)app->win_w / 2.0f - (float)sw / 2.0f;
        ff_font_draw(app->font_small, app->status_msg, sx, (float)(app->win_h - STATUS_H + 6),
                     COL_ACCENT, &st);
    }

    /* right: free space */
    uint64_t fb = 0;
    if (app->current_path) {
        fb = ff_free_bytes(app->current_path);
    }
    char right[128];
    double gb = (double)fb / 1e9;
    if (gb >= 1.0) {
        snprintf(right, sizeof(right), "%.1f GB free", gb);
    } else {
        snprintf(right, sizeof(right), "%.0f MB free", (double)fb / 1e6);
    }
    int rw = 0, rh = 0;
    ff_font_measure(app->font_small, right, &rw, &rh);
    ff_font_draw(app->font_small, right, (float)(app->win_w - rw - 10),
                 (float)(app->win_h - STATUS_H + 6), COL_MUTED, &st);
}

static void draw_files(struct ff_app *app)
{
    SDL_Rect v;
    ui_file_view_rect(app, &v);
    SDL_Rect bg = v;
    draw_rect(app->renderer, &bg, COL_BG);

    int cols = 1, cw = 112, ch = 100;
    int rows = ui_content_rows(app, &cols, &cw, &ch);
    int x0 = v.x + 12;
    int y0 = v.y + 12 - (int)app->scroll_offset;

    SDL_SetRenderClipRect(app->renderer, &v);
    int icon_s = app->cfg.icon_size;
    if (icon_s > ch - 36) {
        icon_s = ch - 36;
    }
    for (size_t i = 0; i < app->entry_count; i++) {
        int row = (int)(i / (size_t)cols);
        int col = (int)(i % (size_t)cols);
        int cx = x0 + col * cw;
        int cy = y0 + row * ch;
        if (cy + ch < v.y || cy > v.y + v.h) {
            continue; /* only visible */
        }
        bool sel = ff_app_is_selected(app, i) || (app->sel_count == 0 && app->cursor == i);
        bool cursor_only = !ff_app_is_selected(app, i) && app->cursor == i;
        SDL_Rect cell = {cx, cy, cw - 8, ch - 6};
        if (sel) {
            SDL_Color c = cursor_only ? COL_SURFACE2 : COL_SEL;
            draw_rect(app->renderer, &cell, c);
        }
        const struct ff_entry *e = &app->entries[i];
        int ix = cx + (cw - 8 - (int)((float)icon_s * 0.7f)) / 2;
        int iy = cy + 6;
        draw_icon(app->renderer, e->kind, ix, iy, icon_s);
        /* name: single centered line, truncated with ellipsis */
        int text_y = cy + 6 + icon_s + 2;
        int text_h = ch - (6 + icon_s + 4);
        if (text_h > 0) {
            SDL_SetRenderClipRect(app->renderer, &v);
            int tw = 0, th = 0;
            ff_font_measure(app->font_small, e->name, &tw, &th);
            int maxw = cw - 16;
            float tx = (float)cx + ((float)(cw - 8) - (float)(tw > maxw ? maxw : tw)) / 2.0f;
            ff_font_draw_fit(app->font_small, e->name, tx, (float)text_y, COL_TEXT, maxw,
                             &v);
        }
    }
    if (app->entry_count == 0) {
        ff_font_draw(app->font, "(empty)", (float)(v.x + 20), (float)(v.y + 20), COL_MUTED,
                     &v);
    }
    SDL_SetRenderClipRect(app->renderer, NULL);
    (void)rows;
}

/* ---------- context menu ---------- */
int ui_menu_count(const struct ff_app *app)
{
    if (!app->menu_open) {
        return 0;
    }
    return app->menu_target >= 0 ? 6 : 3;
}

static const char *menu_label(const struct ff_app *app, int i)
{
    if (app->menu_target >= 0) {
        static const char *labels[] = {"Open",  "Copy",   "Cut",
                                       "Rename", "Delete", "Properties"};
        if (i >= 0 && i < 6) {
            return labels[i];
        }
        return "";
    }
    static const char *labels2[] = {"New Folder", "Paste", "Refresh"};
    if (i >= 0 && i < 3) {
        return labels2[i];
    }
    return "";
}

void ui_menu_rect(const struct ff_app *app, SDL_Rect *r)
{
    int n = ui_menu_count(app);
    int w = 180;
    int h = n * 30 + 8;
    int x = app->menu_x;
    int y = app->menu_y;
    if (x + w > app->win_w - 8) {
        x = app->win_w - w - 8;
    }
    if (y + h > app->win_h - STATUS_H - 8) {
        y = app->win_h - STATUS_H - h - 8;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < HDR_H) {
        y = HDR_H;
    }
    r->x = x;
    r->y = y;
    r->w = w;
    r->h = h;
}

int ui_menu_action_at(const struct ff_app *app, int mx, int my)
{
    if (!app->menu_open) {
        return FF_MENU_NONE;
    }
    SDL_Rect mr;
    ui_menu_rect(app, &mr);
    if (mx < mr.x || mx >= mr.x + mr.w || my < mr.y || my >= mr.y + mr.h) {
        return FF_MENU_NONE;
    }
    int i = (my - mr.y - 4) / 30;
    int n = ui_menu_count(app);
    if (i < 0 || i >= n) {
        return FF_MENU_NONE;
    }
    if (app->menu_target >= 0) {
        static const int map[] = {FF_MENU_OPEN, FF_MENU_COPY, FF_MENU_CUT,
                                  FF_MENU_RENAME, FF_MENU_DELETE, FF_MENU_PROPERTIES};
        return map[i];
    }
    static const int map2[] = {FF_MENU_NEW_FOLDER, FF_MENU_PASTE, FF_MENU_REFRESH};
    return map2[i];
}

static void draw_menu(struct ff_app *app)
{
    if (!app->menu_open) {
        return;
    }
    SDL_Rect mr;
    ui_menu_rect(app, &mr);
    draw_rect(app->renderer, &mr, COL_SURFACE2);
    draw_rect_outline(app->renderer, &mr, COL_BORDER);
    int n = ui_menu_count(app);
    for (int i = 0; i < n; i++) {
        SDL_Rect rc = {mr.x + 4, mr.y + 4 + i * 30, mr.w - 8, 28};
        draw_rect(app->renderer, &rc, COL_SURFACE2);
        ff_font_draw(app->font, menu_label(app, i), (float)(rc.x + 10), (float)(rc.y + 5),
                     COL_TEXT, &rc);
    }
}

/* ---------- dialogs ---------- */
void ui_dialog_rect(const struct ff_app *app, SDL_Rect *r)
{
    (void)app;
    r->w = 440;
    r->h = app->dialog == FF_DLG_PROPERTIES ? 260 : 190;
    r->x = (app->win_w - r->w) / 2;
    r->y = (app->win_h - r->h) / 2;
    if (r->x < 0) {
        r->x = 0;
    }
    if (r->y < HDR_H) {
        r->y = HDR_H;
    }
}

int ui_dialog_button_at(const struct ff_app *app, int mx, int my)
{
    if (app->dialog == FF_DLG_NONE) {
        return -1;
    }
    SDL_Rect dr;
    ui_dialog_rect(app, &dr);
    SDL_Rect b0 = {dr.x + dr.w - 220, dr.y + dr.h - 44, 100, 32};
    SDL_Rect b1 = {dr.x + dr.w - 110, dr.y + dr.h - 44, 100, 32};
    bool in_b1 = mx >= b1.x && mx < b1.x + b1.w && my >= b1.y && my < b1.y + b1.h;
    if (app->dialog == FF_DLG_ERROR || app->dialog == FF_DLG_PROPERTIES) {
        return in_b1 ? 1 : -1; /* single OK button */
    }
    if (mx >= b0.x && mx < b0.x + b0.w && my >= b0.y && my < b0.y + b0.h) {
        return 0;
    }
    if (in_b1) {
        return 1;
    }
    return -1;
}

static void draw_dialog(struct ff_app *app)
{
    if (app->dialog == FF_DLG_NONE) {
        return;
    }
    /* dim */
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 120);
    SDL_FRect full = {0, 0, (float)app->win_w, (float)app->win_h};
    SDL_RenderFillRect(app->renderer, &full);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_NONE);

    SDL_Rect dr;
    ui_dialog_rect(app, &dr);
    draw_rect(app->renderer, &dr, COL_SURFACE);
    draw_rect_outline(app->renderer, &dr, COL_BORDER);

    const char *title = "";
    switch (app->dialog) {
    case FF_DLG_NEW_FOLDER:
        title = "New folder name:";
        break;
    case FF_DLG_RENAME:
        title = "Rename to:";
        break;
    case FF_DLG_CONFIRM_DELETE:
        title = "Delete?";
        break;
    case FF_DLG_ERROR:
        title = "Error";
        break;
    case FF_DLG_PROPERTIES:
        title = "Properties";
        break;
    default:
        break;
    }
    ff_font_draw(app->font, title, (float)(dr.x + 16), (float)(dr.y + 12), COL_TEXT, &dr);

    if (app->dialog == FF_DLG_NEW_FOLDER || app->dialog == FF_DLG_RENAME) {
        SDL_Rect inp = {dr.x + 16, dr.y + 44, dr.w - 32, 32};
        draw_rect(app->renderer, &inp, COL_BG);
        draw_rect_outline(app->renderer, &inp, COL_ACCENT);
        SDL_Rect clip = {inp.x + 6, inp.y + 4, inp.w - 12, inp.h - 8};
        SDL_SetRenderClipRect(app->renderer, &clip);
        char tmp[600];
        snprintf(tmp, sizeof(tmp), "%s", app->dialog_input);
        if (((SDL_GetTicks() / 500) % 2) == 0) {
            size_t l = strlen(tmp);
            if (l + 2 < sizeof(tmp)) {
                tmp[l] = '|';
                tmp[l + 1] = '\0';
            }
        }
        ff_font_draw_fit(app->font, tmp, (float)(inp.x + 8), (float)(inp.y + 6), COL_TEXT,
                         inp.w - 16, &clip);
        SDL_SetRenderClipRect(app->renderer, NULL);
    } else {
        /* message lines */
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", app->dialog_msg);
        char *line = buf;
        char *save = NULL;
        /* simple: split on \n manually */
        int ly = dr.y + 44;
        char *cur = line;
        (void)save;
        while (cur && ly < dr.y + dr.h - 56) {
            char *nl = strchr(cur, '\n');
            char tmpc = '\0';
            if (nl) {
                tmpc = *nl;
                *nl = '\0';
            }
            SDL_Rect clip = {dr.x + 16, ly, dr.w - 32, 24};
            ff_font_draw_fit(app->font_small, cur, (float)(dr.x + 16), (float)ly, COL_TEXT,
                             dr.w - 32, &clip);
            ly += 22;
            if (!nl) {
                break;
            }
            *nl = tmpc;
            cur = nl + 1;
        }
    }

    SDL_Rect b0 = {dr.x + dr.w - 220, dr.y + dr.h - 44, 100, 32};
    SDL_Rect b1 = {dr.x + dr.w - 110, dr.y + dr.h - 44, 100, 32};
    if (app->dialog == FF_DLG_ERROR || app->dialog == FF_DLG_PROPERTIES) {
        draw_text_btn(app, &b1, "OK", false);
    } else if (app->dialog == FF_DLG_CONFIRM_DELETE) {
        draw_text_btn(app, &b0, "Cancel", false);
        draw_text_btn(app, &b1, "Delete", false);
    } else {
        draw_text_btn(app, &b0, "Cancel", false);
        draw_text_btn(app, &b1, "OK", false);
    }
}

void ui_draw(struct ff_app *app)
{
    SDL_SetRenderDrawColor(app->renderer, 28, 28, 34, 255);
    SDL_RenderClear(app->renderer);
    draw_header(app);
    draw_sidebar(app);
    draw_files(app);
    draw_status(app);
    draw_menu(app);
    draw_dialog(app);
    SDL_RenderPresent(app->renderer);
    app->dirty = false;
}
