#ifndef FF_UI_H
#define FF_UI_H

#include <SDL3/SDL.h>
#include "app.h"

void ui_draw(struct ff_app *app);

/* Hit testing helpers for input.c */
void ui_header_rects(const struct ff_app *app, SDL_Rect *back, SDL_Rect *fwd,
                     SDL_Rect *up, SDL_Rect *path, SDL_Rect *search);
int ui_sidebar_index_at(const struct ff_app *app, int mx, int my);
int ui_file_index_at(const struct ff_app *app, int mx, int my);
void ui_file_view_rect(const struct ff_app *app, SDL_Rect *r);

/* Context menu: returns action id (>=0) at point, or -1. Actions: */
enum ff_menu_action {
    FF_MENU_NONE = -1,
    FF_MENU_OPEN = 0,
    FF_MENU_COPY,
    FF_MENU_CUT,
    FF_MENU_PASTE,
    FF_MENU_RENAME,
    FF_MENU_DELETE,
    FF_MENU_PROPERTIES,
    FF_MENU_NEW_FOLDER,
    FF_MENU_REFRESH
};
int ui_menu_action_at(const struct ff_app *app, int mx, int my);
void ui_menu_rect(const struct ff_app *app, SDL_Rect *r);
int ui_menu_count(const struct ff_app *app);

/* Dialog buttons: 0 = cancel/OK-left, 1 = confirm-right, -1 none */
int ui_dialog_button_at(const struct ff_app *app, int mx, int my);
void ui_dialog_rect(const struct ff_app *app, SDL_Rect *r);

int ui_content_rows(const struct ff_app *app, int *cols_out, int *cell_w, int *cell_h);

#endif
