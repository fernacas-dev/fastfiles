#ifndef FF_INPUT_H
#define FF_INPUT_H

#include <SDL3/SDL.h>
#include "app.h"

void ff_input_handle_event(struct ff_app *app, const SDL_Event *e);
void ff_input_clamp_scroll(struct ff_app *app);

#endif
