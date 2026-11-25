#define _GNU_SOURCE
#include "cgroup.h"

#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "util.h"

#define PROCS_MAX 65536

void build_cgroup_dir(const char *cgroup_root, unsigned user, const char *name, char *out, size_t cap)
{
	snprintf(out, cap, "%s/pc-containers/u%u-%s", cgroup_root, user, name);
}

/* The number after "key " at the start of a line. */
static int find_keyed_number(const char *text, const char *key, unsigned long long *out)
{
	size_t len = strlen(key);
	for (const char *line = text; line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
		if (strncmp(line, key, len) || (line[len] != ' ' && line[len] != '\t'))
			continue;
		char *end = NULL;
		*out = strtoull(line + len + 1, &end, 10);
		return end > line + len + 1 ? 0 : -1;
	}
	return -1;
}

int parse_cpu_usage(const char *text, unsigned long long *usec)
{
	return find_keyed_number(text, "usage_usec", usec);
}

int find_ns_pid(const char *proc, int pid)
{
	char path[PATH_MAX], text[4096];
	snprintf(path, sizeof path, "%s/%d/status", proc, pid);
	if (read_text_file(path, text, sizeof text))
		return -1;
	const char *line = strncmp(text, "NSpid:", 6) ? strstr(text, "\nNSpid:") : text;
	if (!line)
		return -1;
	line += *line == '\n';
	const char *end = strchr(line, '\n');
	const char *last = line + 6;
	for (const char *p = line + 6; *p && p != end; p++)
		last = *p == '\t' || *p == ' ' ? p + 1 : last;
	return atoi(last);
}

static int read_procs(const char *cgroup_dir, char *text, size_t cap)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/cgroup.procs", cgroup_dir);
	return read_text_file(path, text, cap);
}

int find_init_pid(const char *cgroup_dir, const char *proc)
{
	static char text[PROCS_MAX];
	if (read_procs(cgroup_dir, text, sizeof text))
		return -1;
	for (char *save = NULL, *tok = strtok_r(text, "\n", &save); tok; tok = strtok_r(NULL, "\n", &save))
		if (find_ns_pid(proc, atoi(tok)) == 1)
			return atoi(tok);
	return -1;
}

static int read_number_file(const char *dir, const char *leaf, unsigned long long *out)
{
	char path[PATH_MAX], text[64], *end = NULL;
	snprintf(path, sizeof path, "%s/%s", dir, leaf);
	if (read_text_file(path, text, sizeof text))
		return -1;
	*out = strtoull(text, &end, 10);
	return end > text ? 0 : -1;
}

static int count_procs(const char *cgroup_dir, unsigned long long *out)
{
	static char text[PROCS_MAX];
	if (read_procs(cgroup_dir, text, sizeof text))
		return -1;
	*out = 0;
	for (const char *p = text; *p; p++)
		*out += *p == '\n';
	return 0;
}

void read_cgroup_stats(const char *cgroup_dir, struct cgroup_stats *s)
{
	char path[PATH_MAX], text[4096];
	*s = (struct cgroup_stats){ 0 };
	s->has_memory = !read_number_file(cgroup_dir, "memory.current", &s->memory_bytes);
	snprintf(path, sizeof path, "%s/cpu.stat", cgroup_dir);
	s->has_cpu = !read_text_file(path, text, sizeof text) && !parse_cpu_usage(text, &s->cpu_usec);
	s->has_pids = !read_number_file(cgroup_dir, "pids.current", &s->pids) || !count_procs(cgroup_dir, &s->pids);
}

/* ---- disk usage ----------------------------------------------------------------------- */

#define SEEN_MAX 65536

static struct {
	unsigned long long bytes;
	struct {
		dev_t dev;
		ino_t ino;
	} seen[SEEN_MAX];
	size_t count;
} usage;

static int is_seen(const struct stat *st)
{
	for (size_t i = 0; i < usage.count; i++)
		if (usage.seen[i].dev == st->st_dev && usage.seen[i].ino == st->st_ino)
			return 1;
	if (usage.count < SEEN_MAX) {
		usage.seen[usage.count].dev = st->st_dev;
		usage.seen[usage.count++].ino = st->st_ino;
	}
	return 0;
}

static int add_usage(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
	(void)path, (void)type, (void)ftw;
	if (st->st_nlink > 1 && !S_ISDIR(st->st_mode) && is_seen(st))
		return 0;
	usage.bytes += (unsigned long long)st->st_blocks * 512;
	return 0;
}

unsigned long long find_disk_usage(const char *dir)
{
	usage.bytes = 0;
	usage.count = 0;
	nftw(dir, add_usage, 32, FTW_PHYS | FTW_MOUNT);
	return usage.bytes;
}
