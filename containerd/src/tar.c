#define _GNU_SOURCE
#include "tar.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ids.h"
#include "inroot.h"

#define BLOCK 512
#define NOBODY 65534u

struct reader {
	int fd;
	struct tar_progress *progress;
};

struct entry {
	char name[PATH_MAX];
	char link[PATH_MAX];
	char type;
	unsigned mode;
	unsigned long long uid, gid, size;
	long long mtime;
};

/* What 'L', 'K' and 'x' headers say about the entry that follows them. */
struct pending {
	char name[PATH_MAX], link[PATH_MAX];
	int has_name, has_link, has_size, has_uid, has_gid, has_mtime;
	unsigned long long size, uid, gid;
	long long mtime;
};

struct job {
	const struct tar_target *t;
	struct reader *r;
	const char *error;
};

/* ---- reading -------------------------------------------------------------------------- */

/* 0 when n bytes were read, 1 on end of stream before any byte, -1 otherwise. */
static int read_full(struct reader *r, void *buf, size_t n)
{
	size_t got = 0;
	while (got < n) {
		ssize_t k = read(r->fd, (char *)buf + got, n - got);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			return got || k < 0 ? -1 : 1;
		got += (size_t)k;
		r->progress->bytes += (unsigned long long)k;
	}
	return 0;
}

static unsigned long long padded(unsigned long long size) { return (size + BLOCK - 1) / BLOCK * BLOCK; }

static int skip_bytes(struct reader *r, unsigned long long n)
{
	char buf[BLOCK * 16];
	while (n) {
		size_t step = n < sizeof buf ? (size_t)n : sizeof buf;
		if (read_full(r, buf, step))
			return -1;
		n -= step;
	}
	return 0;
}

static void drain(struct reader *r)
{
	char buf[65536];
	ssize_t k;
	while ((k = read(r->fd, buf, sizeof buf)) > 0 || (k < 0 && errno == EINTR))
		r->progress->bytes += k > 0 ? (unsigned long long)k : 0;
}

/* A whole small payload ('L', 'K', 'x') into out, NUL-terminated. */
static int read_payload(struct reader *r, unsigned long long size, char *out, size_t cap)
{
	if (size >= cap || read_full(r, out, (size_t)size))
		return -1;
	out[size] = 0;
	return skip_bytes(r, padded(size) - size);
}

/* ---- headers -------------------------------------------------------------------------- */

static unsigned long long read_number(const unsigned char *field, size_t len)
{
	unsigned long long n = 0;
	if (field[0] & 0x80) { /* GNU base-256 */
		n = field[0] & 0x3f;
		for (size_t i = 1; i < len; i++)
			n = n << 8 | field[i];
		return n;
	}
	for (size_t i = 0; i < len && field[i]; i++)
		n = field[i] >= '0' && field[i] <= '7' ? n * 8 + (field[i] - '0') : n;
	return n;
}

static int is_zero_block(const unsigned char *h)
{
	for (int i = 0; i < BLOCK; i++)
		if (h[i])
			return 0;
	return 1;
}

static int has_valid_checksum(const unsigned char *h)
{
	unsigned long long expected = read_number(h + 148, 8);
	unsigned long long sum = 0;
	long long signed_sum = 0;
	for (int i = 0; i < BLOCK; i++) {
		unsigned char c = i >= 148 && i < 156 ? ' ' : h[i];
		sum += c;
		signed_sum += (signed char)c;
	}
	return expected == sum || (long long)expected == signed_sum;
}

static void read_header_name(const unsigned char *h, char *out, size_t cap)
{
	int is_ustar = !memcmp(h + 257, "ustar", 5);
	if (is_ustar && h[345])
		snprintf(out, cap, "%.155s/%.100s", (const char *)h + 345, (const char *)h + 0);
	else
		snprintf(out, cap, "%.100s", (const char *)h);
}

static void read_header(const unsigned char *h, struct entry *e)
{
	read_header_name(h, e->name, sizeof e->name);
	snprintf(e->link, sizeof e->link, "%.100s", (const char *)h + 157);
	e->type = (char)h[156];
	e->mode = (unsigned)read_number(h + 100, 8) & 07777;
	e->uid = read_number(h + 108, 8);
	e->gid = read_number(h + 116, 8);
	e->size = read_number(h + 124, 12);
	e->mtime = (long long)read_number(h + 136, 12);
}

static void apply_pending(struct entry *e, struct pending *p)
{
	if (p->has_name)
		snprintf(e->name, sizeof e->name, "%s", p->name);
	if (p->has_link)
		snprintf(e->link, sizeof e->link, "%s", p->link);
	e->size = p->has_size ? p->size : e->size;
	e->uid = p->has_uid ? p->uid : e->uid;
	e->gid = p->has_gid ? p->gid : e->gid;
	e->mtime = p->has_mtime ? p->mtime : e->mtime;
	*p = (struct pending){ 0 };
}

/* ---- pax records: "<len> <key>=<value>\n" --------------------------------------------- */

static void apply_pax_record(struct pending *p, const char *key, const char *value)
{
	char *end = NULL;
	unsigned long long n = strtoull(value, &end, 10);
	if (!strcmp(key, "path")) {
		snprintf(p->name, sizeof p->name, "%s", value);
		p->has_name = 1;
	} else if (!strcmp(key, "linkpath")) {
		snprintf(p->link, sizeof p->link, "%s", value);
		p->has_link = 1;
	} else if (!strcmp(key, "size")) {
		p->size = n;
		p->has_size = 1;
	} else if (!strcmp(key, "uid")) {
		p->uid = n;
		p->has_uid = 1;
	} else if (!strcmp(key, "gid")) {
		p->gid = n;
		p->has_gid = 1;
	} else if (!strcmp(key, "mtime")) {
		p->mtime = (long long)n;
		p->has_mtime = 1;
	}
}

static int parse_pax(char *text, size_t len, struct pending *p)
{
	for (size_t at = 0; at < len;) {
		char *end = NULL;
		unsigned long long rec = strtoull(text + at, &end, 10);
		if (!rec || at + rec > len || *end != ' ' || text[at + rec - 1] != '\n')
			return -1;
		text[at + rec - 1] = 0;
		char *eq = strchr(end + 1, '=');
		if (!eq)
			return -1;
		*eq = 0;
		apply_pax_record(p, end + 1, eq + 1);
		at += rec;
	}
	return 0;
}

/* ---- writing entries ------------------------------------------------------------------ */

static const char *errno_code(void) { return errno == ENOSPC || errno == EDQUOT ? "no_space" : "io"; }

static unsigned host_id(const struct tar_target *t, unsigned long long id)
{
	return t->id_base + (unsigned)(id < CONTAINER_ID_COUNT ? id : NOBODY);
}

static int chown_created(void *ctx, int dirfd, const char *name)
{
	const struct tar_target *t = ctx;
	return t->chown_at(t->ctx, dirfd, name, t->id_base, t->id_base);
}

static int set_owner(struct job *j, int dirfd, const char *name, const struct entry *e)
{
	return j->t->chown_at(j->t->ctx, dirfd, name, host_id(j->t, e->uid), host_id(j->t, e->gid));
}

static void set_mtime(int dirfd, const char *name, const struct entry *e)
{
	struct timespec times[2] = { { e->mtime, 0 }, { e->mtime, 0 } };
	utimensat(dirfd, name, times, AT_SYMLINK_NOFOLLOW);
}

/* Removes what is at name unless it is a folder, so the entry can take its place. */
static void clear_place(int dirfd, const char *name)
{
	struct stat st;
	if (!fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) && !S_ISDIR(st.st_mode))
		unlinkat(dirfd, name, 0);
}

static int copy_data(struct job *j, int out, unsigned long long size)
{
	char buf[65536];
	unsigned long long left = size;
	while (left) {
		size_t step = left < sizeof buf ? (size_t)left : sizeof buf;
		if (read_full(j->r, buf, step)) {
			j->error = "bad_archive";
			return -1;
		}
		for (size_t done = 0; done < step;) {
			ssize_t k = write(out, buf + done, step - done);
			if (k < 0 && errno == EINTR)
				continue;
			if (k <= 0) {
				j->error = errno_code();
				return -1;
			}
			done += (size_t)k;
		}
		left -= step;
	}
	return skip_bytes(j->r, padded(size) - size) ? (j->error = "bad_archive", -1) : 0;
}

static int write_file(struct job *j, int dirfd, const char *name, const struct entry *e)
{
	clear_place(dirfd, name);
	int fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0) {
		j->error = errno_code();
		return -1;
	}
	int failed = copy_data(j, fd, e->size);
	if (!failed && (set_owner(j, dirfd, name, e) || fchmod(fd, e->mode)))
		j->error = errno_code(), failed = -1;
	struct timespec times[2] = { { e->mtime, 0 }, { e->mtime, 0 } };
	if (!failed)
		futimens(fd, times);
	close(fd);
	return failed;
}

static int write_dir(struct job *j, int dirfd, const char *name, const struct entry *e)
{
	struct stat st;
	int exists = !fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW);
	if (exists && !S_ISDIR(st.st_mode))
		unlinkat(dirfd, name, 0);
	if ((!exists || !S_ISDIR(st.st_mode)) && mkdirat(dirfd, name, 0700))
		return j->error = errno_code(), -1;
	return set_owner(j, dirfd, name, e) || fchmodat(dirfd, name, e->mode, 0) ? (j->error = errno_code(), -1) : 0;
}

static int write_symlink(struct job *j, int dirfd, const char *name, const struct entry *e)
{
	clear_place(dirfd, name);
	if (symlinkat(e->link, dirfd, name) || set_owner(j, dirfd, name, e))
		return j->error = errno_code(), -1;
	set_mtime(dirfd, name, e);
	return 0;
}

static int write_hardlink(struct job *j, int dirfd, const char *name, const struct entry *e)
{
	char base[NAME_MAX + 1];
	int target_dir = open_parent_in_root(j->t->root_fd, e->link, base, sizeof base, chown_created, (void *)j->t);
	if (target_dir < 0)
		return j->error = "bad_archive", -1;
	clear_place(dirfd, name);
	int failed = linkat(target_dir, base, dirfd, name, 0);
	close(target_dir);
	return failed ? (j->error = errno == ENOENT ? "bad_archive" : errno_code(), -1) : 0;
}

static int write_fifo(struct job *j, int dirfd, const char *name, const struct entry *e)
{
	clear_place(dirfd, name);
	if (mknodat(dirfd, name, S_IFIFO | 0600, 0) || set_owner(j, dirfd, name, e) || fchmodat(dirfd, name, e->mode, 0))
		return j->error = errno_code(), -1;
	return 0;
}

static int skip_device(struct job *j, int dirfd, const char *name, const struct entry *e)
{
	(void)j, (void)dirfd, (void)name, (void)e;
	return 0;
}

static const struct {
	char type;
	int (*write)(struct job *, int dirfd, const char *name, const struct entry *);
	int has_data; /* the writer consumes the entry's data itself */
} WRITERS[] = {
	{ '0', write_file, 1 },	   { '\0', write_file, 1 },    { '7', write_file, 1 },	  { '5', write_dir, 0 },
	{ '2', write_symlink, 0 }, { '1', write_hardlink, 0 }, { '6', write_fifo, 0 },	  { '3', skip_device, 0 },
	{ '4', skip_device, 0 },
};

static int write_root(struct job *j, const struct entry *e)
{
	if (e->type != '5')
		return j->error = "bad_archive", -1;
	return set_owner(j, j->t->root_fd, "", e) || fchmod(j->t->root_fd, e->mode) ? (j->error = errno_code(), -1) : 0;
}

static int write_entry(struct job *j, const struct entry *e, int index)
{
	char rel[PATH_MAX], base[NAME_MAX + 1];
	if (normalize_rel(e->name, rel, sizeof rel))
		return j->error = "bad_archive", -1;
	if (!*rel)
		return write_root(j, e);
	int dirfd = open_parent_in_root(j->t->root_fd, rel, base, sizeof base, chown_created, (void *)j->t);
	if (dirfd < 0)
		return j->error = errno_code(), -1;
	int failed = WRITERS[index].write(j, dirfd, base, e);
	close(dirfd);
	return failed;
}

static int find_writer(char type)
{
	for (size_t i = 0; i < sizeof WRITERS / sizeof *WRITERS; i++)
		if (WRITERS[i].type == type)
			return (int)i;
	return -1;
}

/* One real entry: written when its type is known, its data skipped otherwise. */
static int handle_entry(struct job *j, const struct entry *e)
{
	int index = find_writer(e->type);
	if (index < 0)
		return skip_bytes(j->r, padded(e->size)) ? (j->error = "bad_archive", -1) : 0;
	if (write_entry(j, e, index))
		return -1;
	if (!WRITERS[index].has_data && skip_bytes(j->r, padded(e->size)))
		return j->error = "bad_archive", -1;
	j->r->progress->files++;
	if (j->t->on_progress)
		j->t->on_progress(j->t->ctx, j->r->progress);
	return 0;
}

/* Headers that describe the next entry: GNU long name/link and pax records. */
static int handle_meta(struct job *j, const struct entry *e, struct pending *p)
{
	static char text[PATH_MAX * 4];
	int failed = 0;
	switch (e->type) {
	case 'L':
		failed = read_payload(j->r, e->size, p->name, sizeof p->name);
		p->has_name = !failed;
		break;
	case 'K':
		failed = read_payload(j->r, e->size, p->link, sizeof p->link);
		p->has_link = !failed;
		break;
	case 'x':
		failed = read_payload(j->r, e->size, text, sizeof text) || parse_pax(text, (size_t)e->size, p);
		break;
	default: /* 'g': global pax records, nothing we use */
		failed = skip_bytes(j->r, padded(e->size));
	}
	return failed ? (j->error = "bad_archive", -1) : 0;
}

static int is_meta(char type) { return type == 'L' || type == 'K' || type == 'x' || type == 'g'; }

const char *extract_tar(int fd, const struct tar_target *t, struct tar_progress *progress)
{
	struct tar_progress local = { 0 };
	struct reader r = { fd, progress ? progress : &local };
	struct job j = { t, &r, NULL };
	struct pending p = { 0 };
	unsigned char h[BLOCK];
	*r.progress = (struct tar_progress){ 0 };
	for (int blocks = 0;; blocks++) {
		int got = read_full(&r, h, BLOCK);
		if (got)
			return got > 0 && blocks ? NULL : "bad_archive";
		if (is_zero_block(h)) {
			drain(&r);
			return blocks ? NULL : "bad_archive";
		}
		struct entry e;
		if (!has_valid_checksum(h))
			return "bad_archive";
		read_header(h, &e);
		if (is_meta(e.type) ? handle_meta(&j, &e, &p) : (apply_pending(&e, &p), handle_entry(&j, &e)))
			return j.error;
	}
}
