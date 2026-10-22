#ifndef FF_FONT_H
#define FF_FONT_H

#include <SDL3/SDL.h>

typedef struct ff_font ff_font_t;

ff_font_t *ff_font_open(SDL_Renderer *renderer, const char *path, int px_size);
void ff_font_close(ff_font_t *f);
int ff_font_height(const ff_font_t *f);
/* Measure text (UTF-8, invalid sequences shown as '?'). */
void ff_font_measure(const ff_font_t *f, const char *text, int *w, int *h);
/* Draw text at (x,y) top-left, clipped to clip (may be NULL). Returns width drawn. */
int ff_font_draw(ff_font_t *f, const char *text, float x, float y, SDL_Color color,
                 const SDL_Rect *clip);
/* Draw truncated with ellipsis to fit max_w. */
int ff_font_draw_fit(ff_font_t *f, const char *text, float x, float y, SDL_Color color,
                     int max_w, const SDL_Rect *clip);

#endif
