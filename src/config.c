#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ff_config_defaults(struct ff_config *cfg)
{
    cfg->show_hidden = false;
    cfg->sidebar = true;
    cfg->view[0] = 'g';
    cfg->view[1] = 'r';
    cfg->view[2] = 'i';
    cfg->view[3] = 'd';
    cfg->view[4] = '\0';
    cfg->icon_size = 48;
    cfg->font_size = 15;
    cfg->confirm_delete = true;
    cfg->font_path[0] = '\0';
}

int ff_config_default_path(char *buf, unsigned long buflen)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        return -1;
    }
    int n = snprintf(buf, (size_t)buflen, "%s/.config/fastfiles/config", home);
    if (n < 0 || (unsigned long)n >= buflen) {
        return -1;
    }
    return 0;
}

static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                       s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[len - 1] = '\0';
        len--;
    }
}

static bool parse_bool(const char *v, bool *out)
{
    if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0 || strcmp(v, "yes") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0 || strcmp(v, "no") == 0) {
        *out = false;
        return true;
    }
    return false;
}

int ff_config_load(struct ff_config *cfg, const char *path)
{
    ff_config_defaults(cfg);
    if (!path) {
        return 0;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0; /* missing config is fine */
    }
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "fastfiles: warning: ignoring bad config line: %s\n", line);
            continue;
        }
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;
        if (strcmp(key, "show_hidden") == 0) {
            bool b = false;
            if (parse_bool(val, &b)) {
                cfg->show_hidden = b;
            } else {
                fprintf(stderr, "fastfiles: warning: bad value for show_hidden\n");
            }
        } else if (strcmp(key, "sidebar") == 0) {
            bool b = true;
            if (parse_bool(val, &b)) {
                cfg->sidebar = b;
            } else {
                fprintf(stderr, "fastfiles: warning: bad value for sidebar\n");
            }
        } else if (strcmp(key, "view") == 0) {
            snprintf(cfg->view, sizeof(cfg->view), "%s", val);
        } else if (strcmp(key, "icon_size") == 0) {
            long v = strtol(val, NULL, 10);
            if (v >= 24 && v <= 128) {
                cfg->icon_size = (int)v;
            } else {
                fprintf(stderr, "fastfiles: warning: bad icon_size\n");
            }
        } else if (strcmp(key, "font_size") == 0) {
            long v = strtol(val, NULL, 10);
            if (v >= 8 && v <= 32) {
                cfg->font_size = (int)v;
            } else {
                fprintf(stderr, "fastfiles: warning: bad font_size\n");
            }
        } else if (strcmp(key, "confirm_delete") == 0) {
            bool b = true;
            if (parse_bool(val, &b)) {
                cfg->confirm_delete = b;
            } else {
                fprintf(stderr, "fastfiles: warning: bad confirm_delete\n");
            }
        } else if (strcmp(key, "font_path") == 0) {
            snprintf(cfg->font_path, sizeof(cfg->font_path), "%s", val);
        } else {
            fprintf(stderr, "fastfiles: warning: unknown config option '%s'\n", key);
        }
    }
    fclose(f);
    return 0;
}
