#include "font.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define FF_CACHE_CAP 1024

struct glyph {
    uint32_t codepoint;
    bool used;
    SDL_Texture *tex;
    int w;
    int h;
    int bearing_x;
    int bearing_y;
    int advance;
};

struct ff_font {
    SDL_Renderer *renderer;
    FT_Library lib;
    FT_Face face;
    int px;
    int ascender;
    int line_h;
    struct glyph cache[FF_CACHE_CAP];
    size_t cache_next;
};

/* UTF-8 decode: returns codepoint, advances *idx. Invalid -> 0xFFFD. */
static uint32_t utf8_next(const char *s, size_t len, size_t *idx)
{
    size_t i = *idx;
    if (i >= len) {
        return 0;
    }
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80u) {
        (*idx)++;
        return c;
    }
    size_t need = 0;
    uint32_t cp = 0;
    if ((c & 0xE0u) == 0xC0u) {
        need = 1;
        cp = (uint32_t)(c & 0x1Fu);
    } else if ((c & 0xF0u) == 0xE0u) {
        need = 2;
        cp = (uint32_t)(c & 0x0Fu);
    } else if ((c & 0xF8u) == 0xF0u) {
        need = 3;
        cp = (uint32_t)(c & 0x07u);
    } else {
        (*idx)++;
        return 0xFFFDu;
    }
    if (i + need >= len) {
        (*idx)++;
        return 0xFFFDu;
    }
    for (size_t k = 1; k <= need; k++) {
        unsigned char cc = (unsigned char)s[i + k];
        if ((cc & 0xC0u) != 0x80u) {
            (*idx)++;
            return 0xFFFDu;
        }
        cp = (cp << 6u) | (uint32_t)(cc & 0x3Fu);
    }
    *idx += need + 1;
    return cp;
}

static struct glyph *glyph_lookup(ff_font_t *f, uint32_t cp)
{
    for (size_t i = 0; i < FF_CACHE_CAP; i++) {
        if (f->cache[i].used && f->cache[i].codepoint == cp) {
            return &f->cache[i];
        }
    }
    return NULL;
}

static struct glyph *glyph_load(ff_font_t *f, uint32_t cp)
{
    struct glyph *g = glyph_lookup(f, cp);
    if (g) {
        return g;
    }
    /* evict round-robin */
    g = &f->cache[f->cache_next];
    f->cache_next = (f->cache_next + 1) % FF_CACHE_CAP;
    if (g->used && g->tex) {
        SDL_DestroyTexture(g->tex);
    }
    memset(g, 0, sizeof(*g));
    g->used = true;
    g->codepoint = cp;

    if (FT_Load_Char(f->face, (FT_ULong)cp, FT_LOAD_RENDER) != 0) {
        /* fallback to '?' */
        if (cp != (uint32_t)'?' &&
            FT_Load_Char(f->face, (FT_ULong)'?', FT_LOAD_RENDER) != 0) {
            return g; /* empty glyph */
        }
    }
    FT_GlyphSlot slot = f->face->glyph;
    int w = (int)slot->bitmap.width;
    int h = (int)slot->bitmap.rows;
    g->w = w;
    g->h = h;
    g->bearing_x = slot->bitmap_left;
    g->bearing_y = slot->bitmap_top;
    g->advance = (int)(slot->advance.x >> 6);
    if (w == 0 || h == 0) {
        g->tex = NULL;
        return g;
    }
    SDL_Surface *surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        return g;
    }
    /* bitmap is grayscale; expand to white RGBA */
    for (int y = 0; y < h; y++) {
        uint8_t *dst = (uint8_t *)((uint8_t *)surf->pixels + (size_t)y * (size_t)surf->pitch);
        const uint8_t *srcrow = slot->bitmap.buffer + (size_t)y * (size_t)slot->bitmap.pitch;
        for (int x = 0; x < w; x++) {
            uint8_t a = srcrow[x];
            /* RGBA32 little endian: R,G,B,A bytes */
            dst[(size_t)x * 4UL + 0UL] = 255;
            dst[(size_t)x * 4UL + 1UL] = 255;
            dst[(size_t)x * 4UL + 2UL] = 255;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            dst[(size_t)x * 4UL + 3UL] = a;
#else
            /* SDL_PIXELFORMAT_RGBA32 on LE is actually ABGR byte order in memory;
               safest: map via SDL_MapSurfaceRGBA — instead set pixel via format */
            dst[(size_t)x * 4UL + 3UL] = a;
#endif
        }
    }
    /* Fix alpha properly using surface mapping: rewrite with MapPixel */
    /* Our manual bytes above assume RGBA8888 layout; SDL_RGBA32 guarantees R=mask 0xff000000? Actually
       use SDL_MapSurfaceRGBA per pixel would be slow but fine. Instead create texture with blend. */
    g->tex = SDL_CreateTextureFromSurface(f->renderer, surf);
    if (g->tex) {
        SDL_SetTextureBlendMode(g->tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g->tex, SDL_SCALEMODE_LINEAR);
    }
    SDL_DestroySurface(surf);
    (void)h;
    return g;
}

ff_font_t *ff_font_open(SDL_Renderer *renderer, const char *path, int px_size)
{
    ff_font_t *f = (ff_font_t *)calloc(1, sizeof(*f));
    if (!f) {
        return NULL;
    }
    f->renderer = renderer;
    f->px = px_size;
    if (FT_Init_FreeType(&f->lib) != 0) {
        free(f);
        return NULL;
    }
    if (FT_New_Face(f->lib, path, 0, &f->face) != 0) {
        FT_Done_FreeType(f->lib);
        free(f);
        return NULL;
    }
    FT_Set_Pixel_Sizes(f->face, (FT_UInt)px_size, (FT_UInt)px_size);
    f->ascender = (int)(f->face->size->metrics.ascender >> 6);
    f->line_h = (int)(f->face->size->metrics.height >> 6);
    if (f->line_h <= 0) {
        f->line_h = px_size + 4;
    }
    return f;
}

void ff_font_close(ff_font_t *f)
{
    if (!f) {
        return;
    }
    for (size_t i = 0; i < FF_CACHE_CAP; i++) {
        if (f->cache[i].used && f->cache[i].tex) {
            SDL_DestroyTexture(f->cache[i].tex);
        }
    }
    FT_Done_Face(f->face);
    FT_Done_FreeType(f->lib);
    free(f);
}

int ff_font_height(const ff_font_t *f)
{
    return f->line_h;
}

void ff_font_measure(const ff_font_t *f, const char *text, int *w, int *h)
{
    ff_font_t *m = (ff_font_t *)f;
    size_t len = strlen(text);
    size_t idx = 0;
    int total = 0;
    while (idx < len) {
        uint32_t cp = utf8_next(text, len, &idx);
        struct glyph *g = glyph_load(m, cp);
        total += g ? g->advance : m->px / 2;
    }
    if (w) {
        *w = total;
    }
    if (h) {
        *h = m->line_h;
    }
}

int ff_font_draw(ff_font_t *f, const char *text, float x, float y, SDL_Color color,
                 const SDL_Rect *clip)
{
    size_t len = strlen(text);
    size_t idx = 0;
    float pen = x;
    int baseline = (int)(y + (float)f->ascender);
    while (idx < len) {
        uint32_t cp = utf8_next(text, len, &idx);
        struct glyph *g = glyph_load(f, cp);
        if (!g) {
            continue;
        }
        if (g->tex && g->w > 0 && g->h > 0) {
            SDL_FRect dst = {(float)((int)pen + g->bearing_x), (float)(baseline - g->bearing_y),
                             (float)g->w, (float)g->h};
            /* crude clip check */
            if (!clip || (dst.x < (float)(clip->x + clip->w) && dst.x + dst.w > (float)clip->x &&
                          dst.y < (float)(clip->y + clip->h) && dst.y + dst.h > (float)clip->y)) {
                SDL_SetTextureColorMod(g->tex, color.r, color.g, color.b);
                SDL_SetTextureAlphaMod(g->tex, color.a);
                SDL_RenderTexture(f->renderer, g->tex, NULL, &dst);
            }
        }
        pen += (float)g->advance;
    }
    return (int)(pen - x);
}

int ff_font_draw_fit(ff_font_t *f, const char *text, float x, float y, SDL_Color color,
                     int max_w, const SDL_Rect *clip)
{
    int w = 0;
    int h = 0;
    ff_font_measure(f, text, &w, &h);
    if (w <= max_w) {
        return ff_font_draw(f, text, x, y, color, clip);
    }
    int ell = 0;
    int ehh = 0;
    ff_font_measure(f, "...", &ell, &ehh);
    /* binary truncate by bytes-safe prefix: truncate codepoints */
    size_t len = strlen(text);
    size_t best = 0;
    size_t idx = 0;
    int cur = 0;
    while (idx < len) {
        size_t save = idx;
        size_t tmp = idx;
        uint32_t cp = utf8_next(text, len, &tmp);
        struct glyph *g = glyph_lookup(f, cp);
        int adv = 0;
        if (g) {
            adv = g->advance;
        } else {
            adv = f->px / 2;
            /* measure precisely: load */
            g = glyph_load(f, cp);
            adv = g ? g->advance : adv;
        }
        if (cur + adv + ell > max_w) {
            break;
        }
        cur += adv;
        best = tmp;
        idx = tmp;
        (void)save;
    }
    char *tmp2 = (char *)malloc(best + 4);
    if (!tmp2) {
        return 0;
    }
    memcpy(tmp2, text, best);
    memcpy(tmp2 + best, "...", 4);
    int r = ff_font_draw(f, tmp2, x, y, color, clip);
    free(tmp2);
    return r;
}
