#define _GNU_SOURCE
#include "store.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ids.h"
#include "json.h"
#include "util.h"

#define CONFIG_VERSION 1
#define CONFIG_MAX 4096

/* ---- config.json ---------------------------------------------------------------------- */

int save_container_config(const char *path, const struct container *c)
{
	struct jw w;
	jw_init(&w);
	jw_begin_object(&w);
	jw_key_int(&w, "version", CONFIG_VERSION);
	jw_key_string(&w, "distro", c->distro);
	jw_key_string(&w, "release", c->release);
	jw_key_int(&w, "created", c->created);
	jw_key_string(&w, "user", c->user);
	jw_key(&w, "uid_map");
	jw_begin_object(&w);
	jw_key_int(&w, "inside", 0);
	jw_key_int(&w, "outside", c->id_base);
	jw_key_int(&w, "count", CONTAINER_ID_COUNT);
	jw_end_object(&w);
	jw_end_object(&w);
	int failed = save_text_file(path, jw_line(&w), 0644);
	jw_free(&w);
	return failed;
}

static int copy_string(const struct json *j, const char *key, char *out, size_t cap)
{
	const char *s = json_string(j, key);
	if (!s || strlen(s) >= cap)
		return -1;
	snprintf(out, cap, "%s", s);
	return 0;
}

static int read_config_json(const struct json *j, struct container *c)
{
	long long outside = 0;
	if (copy_string(j, "distro", c->distro, sizeof c->distro) || copy_string(j, "release", c->release, sizeof c->release) ||
	    copy_string(j, "user", c->user, sizeof c->user) || json_int(j, "created", &c->created) ||
	    json_int(json_find(j, "uid_map"), "outside", &outside))
		return -1;
	c->id_base = (unsigned)outside;
	return is_valid_user_name(c->user) ? 0 : -1;
}

int read_container_config(const char *path, struct container *c)
{
	char text[CONFIG_MAX];
	if (read_text_file(path, text, sizeof text))
		return -1;
	struct json *j = json_parse(text);
	int failed = !j || read_config_json(j, c);
	json_free(j);
	return failed ? -1 : 0;
}

/* ---- folders -------------------------------------------------------------------------- */

void build_container_path(const struct store *st, unsigned user, const char *name, const char *leaf, char *out, size_t cap)
{
	snprintf(out, cap, "%s/%u/%s%s%s", st->data_root, user, name, *leaf ? "/" : "", leaf);
}

/* The container's uids cross the user's and the container's folders to reach the rootfs. chmod,
 * not just mkdir's mode: init starts services with umask 077. */
int open_container_dirs(const struct store *st, unsigned user, const char *name)
{
	char path[PATH_MAX];
	if (!is_valid_name(name))
		return -1;
	snprintf(path, sizeof path, "%s/%u", st->data_root, user);
	if (chmod(path, 0711))
		return -1;
	build_container_path(st, user, name, "", path, sizeof path);
	return chmod(path, 0711);
}

int add_container_dirs(const struct store *st, unsigned user, const char *name)
{
	char path[PATH_MAX];
	if (!is_valid_name(name))
		return -1;
	snprintf(path, sizeof path, "%s/%u", st->data_root, user);
	if (make_dirs(path, 0711))
		return -1;
	build_container_path(st, user, name, "", path, sizeof path);
	if (mkdir(path, 0711))
		return -1;
	build_container_path(st, user, name, "rootfs", path, sizeof path);
	if (mkdir(path, 0755) || chmod(path, 0755))
		return -1;
	return open_container_dirs(st, user, name);
}

int save_container(const struct store *st, unsigned user, const struct container *c)
{
	char path[PATH_MAX];
	build_container_path(st, user, c->name, "config.json", path, sizeof path);
	return save_container_config(path, c);
}

int find_container(const struct store *st, unsigned user, const char *name, struct container *out)
{
	char path[PATH_MAX];
	if (!is_valid_name(name))
		return -1;
	build_container_path(st, user, name, "config.json", path, sizeof path);
	*out = (struct container){ 0 };
	if (read_container_config(path, out))
		return -1;
	snprintf(out->name, sizeof out->name, "%s", name);
	return 0;
}

static int compare_names(const void *a, const void *b)
{
	return strcmp(((const struct container *)a)->name, ((const struct container *)b)->name);
}

int list_containers(const struct store *st, unsigned user, struct container *out, int max)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%u", st->data_root, user);
	DIR *d = opendir(path);
	int n = 0;
	for (struct dirent *e; d && n < max && (e = readdir(d));)
		n += !find_container(st, user, e->d_name, &out[n]);
	if (d)
		closedir(d);
	qsort(out, (size_t)n, sizeof *out, compare_names);
	return n;
}

int rename_container(const struct store *st, unsigned user, const char *name, const char *new_name)
{
	char from[PATH_MAX], to[PATH_MAX];
	if (!is_valid_name(name) || !is_valid_name(new_name))
		return -1;
	build_container_path(st, user, name, "", from, sizeof from);
	build_container_path(st, user, new_name, "", to, sizeof to);
	return renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE);
}

int remove_container(const struct store *st, unsigned user, const char *name)
{
	char path[PATH_MAX], doomed[64];
	if (!is_valid_name(name))
		return -1;
	snprintf(path, sizeof path, "%s/%u", st->data_root, user);
	int dirfd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0)
		return -1;
	/* Out of the list at once, then removed at leisure. */
	snprintf(doomed, sizeof doomed, ".removing-%s", name);
	int failed = renameat(dirfd, name, dirfd, doomed) || remove_tree_at(dirfd, doomed);
	close(dirfd);
	return failed ? -1 : 0;
}
