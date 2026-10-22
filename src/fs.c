#include "fs.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static bool has_suffix_ci(const char *name, const char *suf)
{
    size_t nl = strlen(name);
    size_t sl = strlen(suf);
    if (sl > nl) {
        return false;
    }
    return strcasecmp(name + nl - sl, suf) == 0;
}

static enum ff_file_kind classify(const char *name, bool is_dir, bool is_link, mode_t mode)
{
    if (is_link) {
        return FF_KIND_LINK;
    }
    if (is_dir) {
        return FF_KIND_DIR;
    }
    if (!S_ISREG(mode)) {
        return FF_KIND_SPECIAL;
    }
    if (has_suffix_ci(name, ".txt") || has_suffix_ci(name, ".md") ||
        has_suffix_ci(name, ".c") || has_suffix_ci(name, ".h") ||
        has_suffix_ci(name, ".py") || has_suffix_ci(name, ".rs") ||
        has_suffix_ci(name, ".log") || has_suffix_ci(name, ".csv") ||
        has_suffix_ci(name, ".json") || has_suffix_ci(name, ".xml") ||
        has_suffix_ci(name, ".yml") || has_suffix_ci(name, ".yaml") ||
        has_suffix_ci(name, ".toml") || has_suffix_ci(name, ".ini")) {
        return FF_KIND_TEXT;
    }
    if (has_suffix_ci(name, ".png") || has_suffix_ci(name, ".jpg") ||
        has_suffix_ci(name, ".jpeg") || has_suffix_ci(name, ".gif") ||
        has_suffix_ci(name, ".bmp") || has_suffix_ci(name, ".svg") ||
        has_suffix_ci(name, ".webp")) {
        return FF_KIND_IMAGE;
    }
    if (has_suffix_ci(name, ".mp3") || has_suffix_ci(name, ".wav") ||
        has_suffix_ci(name, ".ogg") || has_suffix_ci(name, ".flac")) {
        return FF_KIND_AUDIO;
    }
    if (has_suffix_ci(name, ".mp4") || has_suffix_ci(name, ".mkv") ||
        has_suffix_ci(name, ".avi") || has_suffix_ci(name, ".webm") ||
        has_suffix_ci(name, ".mov")) {
        return FF_KIND_VIDEO;
    }
    if (has_suffix_ci(name, ".zip") || has_suffix_ci(name, ".tar") ||
        has_suffix_ci(name, ".gz") || has_suffix_ci(name, ".xz") ||
        has_suffix_ci(name, ".7z") || has_suffix_ci(name, ".deb")) {
        return FF_KIND_ARCHIVE;
    }
    return FF_KIND_FILE;
}

/* case-insensitive substring */
static bool match_filter(const char *name, const char *filter)
{
    if (!filter || !filter[0]) {
        return true;
    }
    size_t nl = strlen(name);
    size_t fl = strlen(filter);
    if (fl > nl) {
        return false;
    }
    for (size_t i = 0; i + fl <= nl; i++) {
        size_t j = 0;
        for (; j < fl; j++) {
            int a = tolower((unsigned char)name[i + j]);
            int b = tolower((unsigned char)filter[j]);
            if (a != b) {
                break;
            }
        }
        if (j == fl) {
            return true;
        }
    }
    return false;
}

static int entry_cmp(const void *a, const void *b)
{
    const struct ff_entry *ea = (const struct ff_entry *)a;
    const struct ff_entry *eb = (const struct ff_entry *)b;
    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }
    int r = strcasecmp(ea->name, eb->name);
    if (r != 0) {
        return r;
    }
    return strcmp(ea->name, eb->name);
}

char *ff_join_path(const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    size_t nl = strlen(name);
    bool need_slash = dl == 0 || dir[dl - 1] != '/';
    size_t total = dl + (need_slash ? 1UL : 0UL) + nl + 1UL;
    char *out = (char *)malloc(total);
    if (!out) {
        return NULL;
    }
    memcpy(out, dir, dl);
    size_t pos = dl;
    if (need_slash) {
        out[pos++] = '/';
    }
    memcpy(out + pos, name, nl + 1UL);
    return out;
}

int ff_list_directory(const char *path, struct ff_entry **out_entries,
                      size_t *out_count, bool show_hidden, const char *filter)
{
    *out_entries = NULL;
    *out_count = 0;
    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }
    struct ff_entry *entries = NULL;
    size_t count = 0;
    size_t cap = 0;
    struct dirent *de = NULL;
    int ret = 0;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) {
            continue;
        }
        bool hidden = nm[0] == '.';
        if (hidden && !show_hidden) {
            continue;
        }
        if (!match_filter(nm, filter)) {
            continue;
        }
        if (count == cap) {
            size_t ncap = cap == 0 ? 64 : cap * 2;
            struct ff_entry *tmp =
                (struct ff_entry *)realloc(entries, ncap * sizeof(*entries));
            if (!tmp) {
                ret = -1;
                break;
            }
            entries = tmp;
            cap = ncap;
        }
        char *full = ff_join_path(path, nm);
        if (!full) {
            ret = -1;
            break;
        }
        struct stat st;
        bool is_link = false;
        bool is_dir = false;
        uint64_t sz = 0;
        time_t mt = 0;
        mode_t mode = 0;
        if (lstat(full, &st) == 0) {
            mode = st.st_mode;
            mt = st.st_mtime;
            if (S_ISLNK(st.st_mode)) {
                is_link = true;
                struct stat tst;
                if (stat(full, &tst) == 0 && S_ISDIR(tst.st_mode)) {
                    is_dir = true; /* symlink to dir: treat as dir for nav */
                    sz = 0;
                } else if (stat(full, &tst) == 0) {
                    sz = (uint64_t)tst.st_size;
                } else {
                    sz = 0; /* broken symlink */
                }
            } else if (S_ISDIR(st.st_mode)) {
                is_dir = true;
            } else if (S_ISREG(st.st_mode)) {
                sz = (uint64_t)st.st_size;
            }
        }
        entries[count].name = strdup(nm);
        entries[count].path = full;
        if (!entries[count].name) {
            free(full);
            ret = -1;
            break;
        }
        entries[count].is_dir = is_dir;
        entries[count].is_symlink = is_link;
        entries[count].is_hidden = hidden;
        entries[count].size = sz;
        entries[count].modified = mt;
        entries[count].kind = classify(nm, is_dir, is_link, mode);
        count++;
    }
    closedir(d);
    if (ret != 0) {
        ff_free_entries(entries, count);
        return -1;
    }
    if (count > 0) {
        /* entries is NULL for empty directories; qsort(NULL, 0) is UB. */
        qsort(entries, count, sizeof(*entries), entry_cmp);
    }
    *out_entries = entries;
    *out_count = count;
    return 0;
}

void ff_free_entries(struct ff_entry *entries, size_t count)
{
    if (!entries) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(entries[i].name);
        free(entries[i].path);
    }
    free(entries);
}

int ff_create_directory(const char *path)
{
    if (mkdir(path, (mode_t)0755) != 0) {
        return -1;
    }
    return 0;
}

int ff_rename_path(const char *from, const char *to)
{
    if (!from || !to || !from[0] || !to[0]) {
        errno = EINVAL;
        return -1;
    }
    if (rename(from, to) != 0) {
        return -1;
    }
    return 0;
}

int ff_exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

int ff_is_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

/* ---- delete ---- */
static int delete_file_only(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        return -1;
    }
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        if (rmdir(path) != 0) {
            return -1;
        }
        return 0;
    }
    if (unlink(path) != 0) {
        return -1;
    }
    return 0;
}

static int delete_recursive_inner(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
        return unlink(path);
    }
    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }
    struct dirent *de = NULL;
    int rc = 0;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char *full = ff_join_path(path, de->d_name);
        if (!full) {
            rc = -1;
            break;
        }
        /* Never follow symlinks: lstat inside recursion handles it */
        if (delete_recursive_inner(full) != 0) {
            free(full);
            rc = -1;
            break;
        }
        free(full);
    }
    closedir(d);
    if (rc != 0) {
        return rc;
    }
    return rmdir(path);
}

int ff_delete_path(const char *path, bool recursive)
{
    if (!recursive) {
        return delete_file_only(path);
    }
    return delete_recursive_inner(path);
}

/* ---- copy ---- */
#define FF_COPY_BUF 65536

static int copy_file_data(const char *src, const char *dst, mode_t mode)
{
    int infd = open(src, O_RDONLY | O_NOFOLLOW);
    if (infd < 0) {
        /* Symlink or special: if symlink, recreate link; else fail gracefully */
        struct stat st;
        if (lstat(src, &st) == 0 && S_ISLNK(st.st_mode)) {
            char target[PATH_MAX + 1];
            ssize_t n = readlink(src, target, (size_t)PATH_MAX);
            if (n < 0) {
                return -1;
            }
            target[(size_t)n] = '\0';
            unlink(dst);
            if (symlink(target, dst) != 0) {
                return -1;
            }
            return 0;
        }
        return -1;
    }
    mode_t outmode = (mode & (mode_t)0100) ? (mode_t)0755 : (mode_t)0644;
    outmode &= (mode_t)0777;
    int outfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, outmode);
    if (outfd < 0) {
        close(infd);
        return -1;
    }
    char *buf = (char *)malloc(FF_COPY_BUF);
    if (!buf) {
        close(infd);
        close(outfd);
        return -1;
    }
    ssize_t r = 0;
    int rc = 0;
    while ((r = read(infd, buf, FF_COPY_BUF)) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(outfd, buf + off, (size_t)(r - off));
            if (w <= 0) {
                rc = -1;
                break;
            }
            off += w;
        }
        if (rc != 0) {
            break;
        }
    }
    if (r < 0) {
        rc = -1;
    }
    free(buf);
    close(infd);
    if (close(outfd) != 0) {
        rc = -1;
    }
    return rc;
}

static int copy_recursive(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) != 0) {
        return -1;
    }
    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX + 1];
        ssize_t n = readlink(src, target, (size_t)PATH_MAX);
        if (n < 0) {
            return -1;
        }
        target[(size_t)n] = '\0';
        unlink(dst);
        return symlink(target, dst);
    }
    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst, st.st_mode & (mode_t)0777) != 0 && errno != EEXIST) {
            return -1;
        }
        DIR *d = opendir(src);
        if (!d) {
            return -1;
        }
        struct dirent *de = NULL;
        int rc = 0;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
                continue;
            }
            char *s = ff_join_path(src, de->d_name);
            char *td = ff_join_path(dst, de->d_name);
            if (!s || !td) {
                free(s);
                free(td);
                rc = -1;
                break;
            }
            rc = copy_recursive(s, td);
            free(s);
            free(td);
            if (rc != 0) {
                break;
            }
        }
        closedir(d);
        return rc;
    }
    if (S_ISREG(st.st_mode)) {
        return copy_file_data(src, dst, st.st_mode);
    }
    errno = ENOTSUP;
    return -1; /* sockets, fifos, devices: refuse */
}

int ff_copy_path(const char *src, const char *dst)
{
    return copy_recursive(src, dst);
}

int ff_move_path(const char *src, const char *dst)
{
    if (rename(src, dst) == 0) {
        return 0;
    }
    if (errno != EXDEV) {
        return -1;
    }
    /* cross-filesystem: copy + delete source only on success */
    if (ff_copy_path(src, dst) != 0) {
        return -1;
    }
    if (ff_delete_path(src, true) != 0) {
        /* best effort: remove incomplete dest? keep dest, report error */
        return -1;
    }
    return 0;
}

uint64_t ff_free_bytes(const char *path)
{
    struct statvfs sv;
    if (statvfs(path, &sv) != 0) {
        return 0;
    }
    return (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
}

const char *ff_strerror(int errnum)
{
    return strerror(errnum);
}
