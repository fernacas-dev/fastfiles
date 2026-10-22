#ifndef FF_FS_H
#define FF_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

enum ff_file_kind {
    FF_KIND_DIR = 0,
    FF_KIND_TEXT,
    FF_KIND_IMAGE,
    FF_KIND_AUDIO,
    FF_KIND_VIDEO,
    FF_KIND_ARCHIVE,
    FF_KIND_FILE,
    FF_KIND_LINK,
    FF_KIND_SPECIAL
};

struct ff_entry {
    char *name;
    char *path;
    bool is_dir;
    bool is_symlink;
    bool is_hidden;
    uint64_t size;
    time_t modified;
    enum ff_file_kind kind;
};

int ff_list_directory(const char *path, struct ff_entry **out_entries,
                      size_t *out_count, bool show_hidden, const char *filter);
void ff_free_entries(struct ff_entry *entries, size_t count);

int ff_create_directory(const char *path);
int ff_rename_path(const char *from, const char *to);
int ff_exists(const char *path);
int ff_is_dir(const char *path);
/* Delete: removes file/symlink, or empty dir. If recursive=true, deletes dir trees
   without following symlinks. Returns 0 on success, -1 on error with errno set. */
int ff_delete_path(const char *path, bool recursive);
int ff_copy_path(const char *src, const char *dst);
int ff_move_path(const char *src, const char *dst);
char *ff_join_path(const char *dir, const char *name);
uint64_t ff_free_bytes(const char *path);
const char *ff_strerror(int errnum);

#endif
