#define _GNU_SOURCE
#include "desktop.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "inroot.h"

#define ENTRY_MAX 65536
#define ICON_MAX_BYTES (4 * 1024 * 1024)
#define APPS_MAX 1024

/* ---- Exec ------------------------------------------------------------------------------- */

struct splitter {
	struct app_argv *a;
	size_t len;      /* bytes used in buf */
	int has_word;    /* the current argument has started (even if empty and quoted) */
	int has_code;    /* the current argument is a lone field code so far */
};

static int put_char(struct splitter *s, char c)
{
	if (s->len + 2 > sizeof s->a->buf)
		return -1;
	if (!s->has_word) {
		if (s->a->argc == APP_ARGS_MAX)
			return -1;
		s->a->argv[s->a->argc++] = s->a->buf + s->len;
		s->has_word = 1;
	}
	s->a->buf[s->len++] = c;
	s->a->buf[s->len] = 0;
	return 0;
}

static void end_word(struct splitter *s)
{
	if (s->has_word)
		s->len++;
	s->has_word = 0;
}

/* %% is a percent sign; any other %x is a field code, which this launcher drops. */
static int put_percent(struct splitter *s, const char **p)
{
	char code = (*p)[1];
	*p += code ? 2 : 1;
	return code == '%' ? put_char(s, '%') : 0;
}

/* Inside double quotes: \" \` \$ \\ are escapes, everything else is literal. */
static int put_quoted(struct splitter *s, const char **p)
{
	(*p)++;
	if (!s->has_word && put_char(s, 0) == 0)
		s->len--; /* an empty "" is still an argument */
	while (**p && **p != '"') {
		if (**p == '\\' && (*p)[1] && strchr("\"`$\\", (*p)[1]))
			(*p)++;
		if (put_char(s, *(*p)++))
			return -1;
	}
	if (**p != '"')
		return -1;
	(*p)++;
	return 0;
}

int split_exec(const char *exec, struct app_argv *a)
{
	struct splitter s = { a, 0, 0, 0 };
	*a = (struct app_argv){ 0 };
	for (const char *p = exec; *p;) {
		int failed = 0;
		if (*p == ' ' || *p == '\t') {
			end_word(&s);
			p++;
		} else if (*p == '"') {
			failed = put_quoted(&s, &p);
		} else if (*p == '%') {
			failed = put_percent(&s, &p);
		} else {
			failed = put_char(&s, *p++);
		}
		if (failed)
			return -1;
	}
	a->argv[a->argc] = NULL;
	return a->argc ? 0 : -1;
}

/* ---- [Desktop Entry] -------------------------------------------------------------------- */

/* The Desktop Entry string escapes: \s \n \t \r \\ (Exec quoting comes after). */
static void unescape_value(const char *in, char *out, size_t cap)
{
	static const char ESCAPES[][2] = { { 's', ' ' }, { 'n', '\n' }, { 't', '\t' }, { 'r', '\r' }, { '\\', '\\' } };
	size_t n = 0;
	for (const char *p = in; *p && n + 1 < cap; p++) {
		char c = *p;
		for (size_t i = 0; c == '\\' && i < sizeof ESCAPES / sizeof *ESCAPES; i++)
			if (p[1] == ESCAPES[i][0]) {
				c = ESCAPES[i][1];
				p++;
				break;
			}
		out[n++] = c;
	}
	out[n] = 0;
}

struct entry_keys {
	char type[32], name[256], name_lang[256], name_locale[256], exec[1024], icon[256], categories[512];
	char no_display[8], hidden[8], terminal[8];
};

static void build_locale_keys(const char *locale, char *full, char *lang, size_t cap)
{
	*full = *lang = 0;
	if (!locale || !*locale)
		return;
	snprintf(full, cap, "Name[%s]", locale);
	snprintf(lang, cap, "Name[%.*s]", (int)strcspn(locale, "_.@"), locale);
}

static void store_key(struct entry_keys *k, const char *key, const char *value, const char *full, const char *lang)
{
	const struct {
		const char *key;
		char *out;
		size_t cap;
	} KEYS[] = {
		{ "Type", k->type, sizeof k->type },
		{ "Name", k->name, sizeof k->name },
		{ full, k->name_locale, sizeof k->name_locale },
		{ lang, k->name_lang, sizeof k->name_lang },
		{ "Exec", k->exec, sizeof k->exec },
		{ "Icon", k->icon, sizeof k->icon },
		{ "Categories", k->categories, sizeof k->categories },
		{ "NoDisplay", k->no_display, sizeof k->no_display },
		{ "Hidden", k->hidden, sizeof k->hidden },
		{ "Terminal", k->terminal, sizeof k->terminal },
	};
	for (size_t i = 0; i < sizeof KEYS / sizeof *KEYS; i++)
		if (*KEYS[i].key && !strcmp(KEYS[i].key, key)) {
			unescape_value(value, KEYS[i].out, KEYS[i].cap);
			return;
		}
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
		*--end = 0;
	return s;
}

static void read_entry_group(const char *text, const char *locale, struct entry_keys *k)
{
	char full[96], lang[96], line[2048];
	int in_entry = 0;
	build_locale_keys(locale, full, lang, sizeof full);
	for (const char *p = text; *p;) {
		size_t len = strcspn(p, "\n");
		snprintf(line, sizeof line, "%.*s", (int)len, p);
		p += len + (p[len] == '\n');
		char *l = trim(line), *eq = strchr(l, '=');
		if (*l == '[') {
			in_entry = !strcmp(l, "[Desktop Entry]");
			continue;
		}
		if (!in_entry || *l == '#' || !eq)
			continue;
		*eq = 0;
		store_key(k, trim(l), trim(eq + 1), full, lang);
	}
}

static const char *pick_name(const struct entry_keys *k)
{
	return *k->name_locale ? k->name_locale : *k->name_lang ? k->name_lang : k->name;
}

int parse_desktop_entry(const char *text, const char *locale, struct desktop_app *app)
{
	struct entry_keys k = { 0 };
	read_entry_group(text, locale, &k);
	if (strcmp(k.type, "Application") || !*k.exec || !*k.name || !strcmp(k.no_display, "true") || !strcmp(k.hidden, "true"))
		return -1;
	*app = (struct desktop_app){ .terminal = !strcmp(k.terminal, "true") };
	snprintf(app->name, sizeof app->name, "%s", pick_name(&k));
	snprintf(app->exec, sizeof app->exec, "%s", k.exec);
	snprintf(app->icon, sizeof app->icon, "%s", k.icon);
	snprintf(app->categories, sizeof app->categories, "%s", k.categories);
	return 0;
}

/* ---- icons ------------------------------------------------------------------------------ */

static const struct {
	const char *ext;
	const char *format;
} FORMATS[] = { { ".png", "png" }, { ".svg", "svg" }, { ".xpm", "xpm" } };

static const char *find_format(const char *path)
{
	size_t len = strlen(path);
	for (size_t i = 0; i < sizeof FORMATS / sizeof *FORMATS; i++) {
		size_t n = strlen(FORMATS[i].ext);
		if (len > n && !strcmp(path + len - n, FORMATS[i].ext))
			return FORMATS[i].format;
	}
	return NULL;
}

/* Where an icon name is looked for, best first: big PNGs, SVG, small PNGs, then pixmaps; each in the
 * distro's share and in what Flatpak exports from its apps. */
static const char *const ICON_ROOTS[] = { "usr/share", "var/lib/flatpak/exports/share", "var/lib/flatpak-user/exports/share" };
static const char *const ICON_PLACES[] = {
	"icons/hicolor/512x512/apps/%s.png", "icons/hicolor/256x256/apps/%s.png", "icons/hicolor/192x192/apps/%s.png",
	"icons/hicolor/128x128/apps/%s.png", "icons/hicolor/96x96/apps/%s.png",   "icons/hicolor/scalable/apps/%s.svg",
	"icons/hicolor/72x72/apps/%s.png",   "icons/hicolor/64x64/apps/%s.png",   "icons/hicolor/48x48/apps/%s.png",
	"icons/hicolor/32x32/apps/%s.png",   "icons/hicolor/24x24/apps/%s.png",   "icons/hicolor/16x16/apps/%s.png",
	"pixmaps/%s.png",		     "pixmaps/%s.svg",			  "pixmaps/%s.xpm",
};

/* Links are followed, inside the container's root (open_in_root): Flatpak exports its apps' icons and
 * entries as links into each app's own files. */
static int is_regular_in_root(int root_fd, const char *rel, long long *size)
{
	struct stat st;
	int fd = open_in_root(root_fd, rel, O_RDONLY | O_NONBLOCK, 0);
	int ok = fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode);
	*size = ok ? st.st_size : 0;
	if (fd >= 0)
		close(fd);
	return ok;
}

static void set_icon(struct desktop_app *app, const char *rel)
{
	snprintf(app->icon_path, sizeof app->icon_path, "/%.510s", rel);
	app->icon_format = find_format(rel);
}

static void find_app_icon(int root_fd, struct desktop_app *app)
{
	char name[256], rel[512];
	long long size;
	app->icon_path[0] = 0;
	app->icon_format = NULL;
	if (app->icon[0] == '/') {
		if (find_format(app->icon) && !normalize_rel(app->icon, rel, sizeof rel) && is_regular_in_root(root_fd, rel, &size))
			set_icon(app, rel);
		return;
	}
	snprintf(name, sizeof name, "%s", app->icon);
	const char *format = find_format(name);
	if (format)
		name[strlen(name) - 4] = 0;
	if (!*name || strchr(name, '/'))
		return;
	for (size_t i = 0; i < sizeof ICON_PLACES / sizeof *ICON_PLACES; i++)
		for (size_t r = 0; r < sizeof ICON_ROOTS / sizeof *ICON_ROOTS; r++) {
			char place[128];
			snprintf(place, sizeof place, "%s/%s", ICON_ROOTS[r], ICON_PLACES[i]);
			snprintf(rel, sizeof rel, place, name);
			if (is_regular_in_root(root_fd, rel, &size)) {
				set_icon(app, rel);
				return;
			}
		}
}

int open_icon(int root_fd, const char *path, const char **format, int *size)
{
	char rel[512];
	struct stat st;
	*format = find_format(path);
	if (!*format || normalize_rel(path, rel, sizeof rel))
		return -1;
	int fd = open_in_root(root_fd, rel, O_RDONLY | O_NONBLOCK, 0);
	if (fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size <= ICON_MAX_BYTES) {
		*size = (int)st.st_size;
		return fd;
	}
	if (fd >= 0)
		close(fd);
	return -1;
}

/* ---- listing ---------------------------------------------------------------------------- */

/* Local apps first: an id found there hides the same id in usr/share, and the distro's own packages
 * hide a Flatpak app of the same id (Flatpak exports its apps' entries in var/lib/flatpak/exports, and
 * the user's in var/lib/flatpak-user/exports: FLATPAK_USER_DIR, runtime.c). */
static const char *const APP_DIRS[] = { "usr/local/share/applications", "usr/share/applications",
					"var/lib/flatpak/exports/share/applications",
					"var/lib/flatpak-user/exports/share/applications" };

static int read_in_root(int root_fd, const char *rel, char *out, size_t cap)
{
	int fd = open_in_root(root_fd, rel, O_RDONLY | O_NONBLOCK, 0);
	ssize_t n = fd >= 0 ? read(fd, out, cap - 1) : -1;
	if (fd >= 0)
		close(fd);
	out[n > 0 ? n : 0] = 0;
	return n > 0 ? 0 : -1;
}

static int has_id(const struct desktop_app *apps, int n, const char *id)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(apps[i].id, id))
			return 1;
	return 0;
}

static int is_desktop_file(const char *name, size_t cap)
{
	size_t len = strlen(name);
	return len > 8 && len - 8 < cap && !strcmp(name + len - 8, ".desktop");
}

static int add_dir_apps(int root_fd, const char *dir, const char *locale, struct desktop_app *apps, int n)
{
	static char text[ENTRY_MAX];
	char rel[512];
	int fd = open_in_root(root_fd, dir, O_RDONLY | O_DIRECTORY, 0);
	DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
	if (!d) {
		if (fd >= 0)
			close(fd);
		return n;
	}
	for (struct dirent *e; n < APPS_MAX && (e = readdir(d));) {
		char id[128];
		if (!is_desktop_file(e->d_name, sizeof id))
			continue;
		snprintf(id, sizeof id, "%.*s", (int)(strlen(e->d_name) - 8), e->d_name);
		snprintf(rel, sizeof rel, "%s/%s", dir, e->d_name);
		if (has_id(apps, n, id) || read_in_root(root_fd, rel, text, sizeof text) || parse_desktop_entry(text, locale, &apps[n]))
			continue;
		snprintf(apps[n].id, sizeof apps[n].id, "%s", id);
		n++;
	}
	closedir(d);
	return n;
}

static int compare_ids(const void *a, const void *b)
{
	return strcmp(((const struct desktop_app *)a)->id, ((const struct desktop_app *)b)->id);
}

int list_apps(int root_fd, const char *locale, struct desktop_app **apps)
{
	struct desktop_app *list = calloc(APPS_MAX, sizeof *list);
	int n = 0;
	for (size_t i = 0; list && i < sizeof APP_DIRS / sizeof *APP_DIRS; i++)
		n = add_dir_apps(root_fd, APP_DIRS[i], locale, list, n);
	qsort(list, (size_t)n, sizeof *list, compare_ids);
	int shown = 0;
	for (int i = 0; i < n; i++) {
		if (split_exec(list[i].exec, &list[i].argv))
			continue;
		find_app_icon(root_fd, &list[i]);
		if (shown != i) {
			list[shown] = list[i];
			split_exec(list[shown].exec, &list[shown].argv); /* argv points into the moved copy */
		}
		shown++;
	}
	*apps = list;
	return list ? shown : -1;
}
