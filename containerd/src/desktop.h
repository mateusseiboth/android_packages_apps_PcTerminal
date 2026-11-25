/*
 * The apps of a container: Desktop Entry files in usr/{local/,}share/applications of its rootfs, their
 * command lines and their icon files. Every file is read through inroot.h.
 */
#pragma once
#include <limits.h>

#define APP_ARGS_MAX 64

struct app_argv {
	int argc;
	char *argv[APP_ARGS_MAX + 1]; /* point into buf */
	char buf[2048];
};

struct desktop_app {
	char id[128];
	char name[256];
	char exec[1024];
	char icon[256];
	char categories[512];
	int terminal;
	struct app_argv argv;
	char icon_path[512];     /* inside the container; "" when none */
	const char *icon_format; /* "png", "svg", "xpm" or NULL */
};

/* Exec split by the Desktop Entry quoting rules, without field codes; -1 when empty or malformed. */
int split_exec(const char *exec, struct app_argv *a);
/* The [Desktop Entry] group of a shown application (no NoDisplay/Hidden, has Exec); -1 otherwise.
 * locale (may be NULL) picks Name[ll_CC], then Name[ll]. argv and the icon are not filled. */
int parse_desktop_entry(const char *text, const char *locale, struct desktop_app *app);
/* Every shown app sorted by id, with argv and icon filled, in a malloc'd array; the count. */
int list_apps(int root_fd, const char *locale, struct desktop_app **apps);
/* Opens an icon file by its path inside the container (.png, .svg, .xpm, at most 4 MiB). */
int open_icon(int root_fd, const char *path, const char **format, int *size);
