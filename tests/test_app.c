/* Headless test of the app layer (app.c).
 *
 * Runs with SDL_VIDEODRIVER=offscreen so no display is needed.
 * Exercises: init, refresh, navigate, back/forward/up, selection,
 * clipboard copy/cut/paste, create folder, rename, delete, search
 * filter and hidden toggle. Uses only tmp dirs, never the real HOME
 * contents for mutation (HOME dir is only listed, not modified).
 *
 * Linked with ASan+UBSan+LSan in sanitizer builds, so leaks and
 * undefined behavior in the app logic are caught here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/app.h"
#include "../src/fs.h"

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);                 \
            failures++;                                                   \
        } else {                                                          \
            printf("ok: %s\n", msg);                                      \
        }                                                                 \
    } while (0)

static int find_name(struct ff_app *app, const char *name)
{
    for (size_t i = 0; i < app->entry_count; i++) {
        if (strcmp(app->entries[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void write_file(const char *path, const char *data)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen");
        exit(1);
    }
    size_t n = strlen(data);
    if (n > 0 && fwrite(data, 1, n, f) != n) {
        perror("fwrite");
        exit(1);
    }
    fclose(f);
}

static bool ends_with(const char *s, const char *suf)
{
    size_t ls = strlen(s);
    size_t lf = strlen(suf);
    return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}

int main(void)
{
    char tmp[] = "/tmp/fastfiles-app-test-XXXXXX";
    if (!mkdtemp(tmp)) {
        perror("mkdtemp");
        return 1;
    }
    printf("tmpdir: %s\n", tmp);

    char *dir_a = ff_join_path(tmp, "A");
    char *dir_b = ff_join_path(tmp, "B");
    char *dir_c = ff_join_path(tmp, "C");
    char *top = ff_join_path(tmp, "top.txt");
    if (!dir_a || !dir_b || !dir_c || !top) {
        return 1;
    }
    CHECK(ff_create_directory(dir_a) == 0, "setup A");
    CHECK(ff_create_directory(dir_b) == 0, "setup B");
    CHECK(ff_create_directory(dir_c) == 0, "setup C");
    write_file(top, "top");
    char *a1 = ff_join_path(dir_a, "f1.txt");
    char *a2 = ff_join_path(dir_a, "f2.txt");
    write_file(a1, "1");
    write_file(a2, "2");

    (void)setenv("SDL_VIDEODRIVER", "offscreen", 1);

    struct ff_app app;
    memset(&app, 0, sizeof(app));
    if (ff_app_init(&app, "/nonexistent-ff-config-xyz", tmp) != 0) {
        printf("SKIP: ff_app_init failed (no video driver or font?)\n");
        return 0;
    }

    /* initial listing: A B C dirs first, then top.txt */
    CHECK(app.entry_count == 4, "initial count");
    CHECK(find_name(&app, "A") == 0, "dir A first");
    CHECK(find_name(&app, "top.txt") == 3, "file last");

    /* enter A by opening its index */
    CHECK(ff_app_open_index(&app, (size_t)find_name(&app, "A")) == 0, "open dir A");
    CHECK(ends_with(app.current_path, "/A"), "current is A");
    CHECK(app.entry_count == 2, "A has 2 files");

    /* history: back -> tmp, forward -> A, up -> tmp */
    CHECK(ff_app_back(&app) == 0, "back");
    CHECK(strcmp(app.current_path, tmp) == 0, "back at tmp");
    CHECK(app.entry_count == 4, "tmp relisted");
    CHECK(ff_app_forward(&app) == 0, "forward");
    CHECK(ends_with(app.current_path, "/A"), "forward to A");
    CHECK(ff_app_up(&app) == 0, "up");
    CHECK(strcmp(app.current_path, tmp) == 0, "up at tmp");

    /* copy top.txt into B via clipboard */
    ff_app_select_only(&app, (size_t)find_name(&app, "top.txt"));
    ff_app_clipboard_copy(&app);
    CHECK(ff_app_navigate(&app, dir_b) == 0, "goto B");
    CHECK(ff_app_clipboard_paste(&app) == 0, "paste copy into B");
    char *btop = ff_join_path(dir_b, "top.txt");
    CHECK(ff_exists(btop), "copied file exists in B");

    /* cut (move) B/top.txt into C */
    CHECK(find_name(&app, "top.txt") >= 0, "top in B");
    ff_app_select_only(&app, (size_t)find_name(&app, "top.txt"));
    ff_app_clipboard_cut(&app);
    CHECK(ff_app_navigate(&app, dir_c) == 0, "goto C");
    CHECK(ff_app_clipboard_paste(&app) == 0, "paste move into C");
    char *ctop = ff_join_path(dir_c, "top.txt");
    CHECK(ff_exists(ctop), "moved file exists in C");
    CHECK(!ff_exists(btop), "source gone after move");

    /* create folder + rename + delete */
    CHECK(ff_app_create_folder(&app, "NewDir") == 0, "create folder");
    char *newdir = ff_join_path(dir_c, "NewDir");
    CHECK(ff_exists(newdir), "new dir exists");
    /* entries in C: NewDir, top.txt */
    CHECK(ff_app_rename_index(&app, (size_t)find_name(&app, "top.txt"), "renamed.txt") == 0,
          "rename");
    char *renamed = ff_join_path(dir_c, "renamed.txt");
    CHECK(ff_exists(renamed) && !ff_exists(ctop), "rename effect");
    ff_app_select_only(&app, (size_t)find_name(&app, "renamed.txt"));
    app.cfg.confirm_delete = false; /* app fn deletes directly; dialog is UI-level */
    CHECK(ff_app_delete_selected(&app) == 0, "delete selected");
    CHECK(!ff_exists(renamed), "deleted");

    /* search filter */
    snprintf(app.search, sizeof(app.search), "%s", "New");
    CHECK(ff_app_refresh(&app) == 0, "refresh with filter");
    CHECK(app.entry_count == 1 && strcmp(app.entries[0].name, "NewDir") == 0,
          "filter matches NewDir");
    app.search[0] = '\0';

    /* hidden toggle */
    char *hid = ff_join_path(dir_c, ".hfile");
    write_file(hid, "h");
    app.show_hidden = false;
    CHECK(ff_app_refresh(&app) == 0, "refresh no hidden");
    CHECK(find_name(&app, ".hfile") < 0, "hidden file excluded");
    app.show_hidden = true;
    CHECK(ff_app_refresh(&app) == 0, "refresh with hidden");
    CHECK(find_name(&app, ".hfile") >= 0, "hidden file included");

    /* select all + clear */
    ff_app_select_all(&app);
    CHECK(app.sel_count == app.entry_count && app.entry_count > 0, "select all");
    ff_app_clear_selection(&app);
    CHECK(app.sel_count == 0, "clear selection");

    /* invalid navigation must fail gracefully (error dialog, no crash) */
    CHECK(ff_app_navigate(&app, "/nonexistent-ff-12345") != 0, "bad navigate fails");
    CHECK(app.dialog == FF_DLG_ERROR, "error dialog shown");

    ff_app_destroy(&app);

    free(dir_a);
    free(dir_b);
    free(dir_c);
    free(top);
    free(a1);
    free(a2);
    free(btop);
    free(ctop);
    free(newdir);
    free(renamed);
    free(hid);
    CHECK(ff_delete_path(tmp, true) == 0, "cleanup tmp");

    if (failures == 0) {
        printf("ALL APP TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURES\n", failures);
    return 1;
}
