#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/fs.h"

static int failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);             \
            failures++;                                               \
        } else {                                                      \
            printf("ok: %s\n", msg);                                  \
        }                                                             \
    } while (0)

static void make_tmp(char *tmpl)
{
    if (!mkdtemp(tmpl)) {
        perror("mkdtemp");
        exit(1);
    }
}

static void write_file(const char *path, const char *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen");
        exit(1);
    }
    if (n > 0 && fwrite(data, 1, n, f) != n) {
        perror("fwrite");
        exit(1);
    }
    fclose(f);
}

int main(void)
{
    char tmp[] = "/tmp/fastfiles-test-XXXXXX";
    make_tmp(tmp);
    printf("tmpdir: %s\n", tmp);

    /* setup: dirs + files */
    char *sub = ff_join_path(tmp, "Sub");
    char *f1 = ff_join_path(tmp, "b.txt");
    char *f2 = ff_join_path(tmp, "A.txt");
    char *hid = ff_join_path(tmp, ".hidden");
    assert(sub && f1 && f2 && hid);
    CHECK(ff_create_directory(sub) == 0, "create directory");
    write_file(f1, "hello", 5);
    write_file(f2, "world", 5);
    write_file(hid, "h", 1);
    /* unicode + long + spaces + dash names */
    char *uni = ff_join_path(tmp, "caf\xc3\xa9 \xe2\x82\xac.txt");
    char *sp = ff_join_path(tmp, "with space.txt");
    char *dash = ff_join_path(tmp, "-dash.txt");
    write_file(uni, "u", 1);
    write_file(sp, "s", 1);
    write_file(dash, "d", 1);

    /* listing without hidden */
    struct ff_entry *ents = NULL;
    size_t n = 0;
    CHECK(ff_list_directory(tmp, &ents, &n, false, NULL) == 0, "list directory");
    /* expect: Sub(dir first), then files sorted case-insensitively:
       -dash, A.txt, b.txt, cafe..., with space */
    CHECK(n == 6, "hidden excluded");
    if (n > 0) {
        CHECK(ents[0].is_dir, "dirs first");
    }
    ff_free_entries(ents, n);
    ents = NULL;

    CHECK(ff_list_directory(tmp, &ents, &n, true, NULL) == 0, "list with hidden");
    CHECK(n == 7, "hidden included");
    ff_free_entries(ents, n);
    ents = NULL;

    /* search filter */
    CHECK(ff_list_directory(tmp, &ents, &n, true, "caf") == 0, "filtered list");
    CHECK(n == 1, "filter matches one");
    ff_free_entries(ents, n);
    ents = NULL;

    /* sorting determinism */
    CHECK(ff_list_directory(tmp, &ents, &n, false, NULL) == 0, "relist");
    for (size_t i = 1; i < n; i++) {
        if (ents[i - 1].is_dir == ents[i].is_dir) {
            CHECK(strcasecmp(ents[i - 1].name, ents[i].name) <= 0, "sorted alphabetical");
        }
    }
    ff_free_entries(ents, n);
    ents = NULL;

    /* rename */
    char *rn = ff_join_path(tmp, "renamed.txt");
    CHECK(ff_rename_path(f1, rn) == 0, "rename file");
    CHECK(ff_exists(rn), "renamed exists");
    CHECK(!ff_exists(f1), "old gone");

    /* copy file incremental */
    char big[70000];
    memset(big, 'x', sizeof(big));
    char *srcbig = ff_join_path(tmp, "big.bin");
    write_file(srcbig, big, sizeof(big));
    char *dstbig = ff_join_path(tmp, "big-copy.bin");
    CHECK(ff_copy_path(srcbig, dstbig) == 0, "copy file");
    struct stat a, b;
    CHECK(stat(srcbig, &a) == 0 && stat(dstbig, &b) == 0 && a.st_size == b.st_size,
          "copy size equal");

    /* copy dir recursive */
    char *inner = ff_join_path(sub, "inner.txt");
    write_file(inner, "in", 2);
    char *subcopy = ff_join_path(tmp, "SubCopy");
    CHECK(ff_copy_path(sub, subcopy) == 0, "copy dir");
    char *innercopy = ff_join_path(subcopy, "inner.txt");
    CHECK(ff_exists(innercopy), "copied child exists");

    /* move */
    char *moved = ff_join_path(tmp, "moved.bin");
    CHECK(ff_move_path(dstbig, moved) == 0, "move file");
    CHECK(ff_exists(moved) && !ff_exists(dstbig), "move semantics");

    /* symlink: never follow */
    char *linkp = ff_join_path(tmp, "link-to-sub");
    (void)unlink(linkp);
    CHECK(symlink(sub, linkp) == 0, "create symlink");
    CHECK(ff_list_directory(tmp, &ents, &n, true, "link") == 0, "list symlink");
    CHECK(n == 1 && ents[0].is_symlink, "symlink flagged");
    ff_free_entries(ents, n);
    ents = NULL;
    /* delete symlink: must not delete target */
    CHECK(ff_delete_path(linkp, true) == 0, "delete symlink only");
    CHECK(ff_exists(sub), "target survives symlink delete");

    /* broken symlink */
    char *broken = ff_join_path(tmp, "broken-link");
    (void)unlink(broken);
    CHECK(symlink("/nonexistent-ff-target", broken) == 0, "broken symlink");
    CHECK(ff_list_directory(tmp, &ents, &n, true, NULL) == 0, "list with broken link ok");
    ff_free_entries(ents, n);
    ents = NULL;
    CHECK(ff_delete_path(broken, true) == 0, "delete broken link");

    /* delete empty dir non-recursive */
    char *empt = ff_join_path(tmp, "Empty");
    CHECK(ff_create_directory(empt) == 0, "create empty");
    CHECK(ff_delete_path(empt, false) == 0, "delete empty dir");

    /* recursive delete */
    CHECK(ff_delete_path(subcopy, true) == 0, "recursive delete");
    CHECK(!ff_exists(subcopy), "gone after recursive delete");

    /* invalid paths */
    CHECK(ff_list_directory("/nonexistent-ff-12345", &ents, &n, false, NULL) != 0,
          "invalid path errors");
    CHECK(ff_rename_path("/nonexistent-ff-12345/a", "/tmp/b") != 0, "rename missing errors");

    /* permission error (read-only dir create should fail for non-root; skip if root) */
    if (geteuid() != 0) {
        char *ro = ff_join_path(tmp, "RO");
        CHECK(ff_create_directory(ro) == 0, "create ro dir");
        CHECK(chmod(ro, (mode_t)0500) == 0, "chmod ro");
        char *rof = ff_join_path(ro, "x");
        CHECK(ff_create_directory(rof) != 0, "create in ro dir fails");
        (void)chmod(ro, (mode_t)0700);
        (void)ff_delete_path(ro, true);
        free(ro);
        free(rof);
    }

    /* long filename */
    char longname[220];
    memset(longname, 'l', 200);
    longname[200] = '\0';
    char *longp = ff_join_path(tmp, longname);
    write_file(longp, "x", 1);
    CHECK(ff_exists(longp), "long filename");
    CHECK(ff_delete_path(longp, false) == 0, "delete long filename");

    /* cleanup */
    free(sub);
    free(f1);
    free(f2);
    free(hid);
    free(uni);
    free(sp);
    free(dash);
    free(rn);
    free(srcbig);
    free(dstbig);
    free(inner);
    free(subcopy);
    free(innercopy);
    free(moved);
    free(linkp);
    free(broken);
    free(empt);
    free(longp);
    /* remove tmp tree */
    CHECK(ff_delete_path(tmp, true) == 0, "cleanup tmp");

    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURES\n", failures);
    return 1;
}
