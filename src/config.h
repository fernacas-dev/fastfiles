#ifndef FF_CONFIG_H
#define FF_CONFIG_H

#include <stdbool.h>

struct ff_config {
    bool show_hidden;
    bool sidebar;
    char view[16];
    int icon_size;
    int font_size;
    bool confirm_delete;
    char font_path[1024];
};

void ff_config_defaults(struct ff_config *cfg);
int ff_config_load(struct ff_config *cfg, const char *path);
int ff_config_default_path(char *buf, unsigned long buflen);

#endif
