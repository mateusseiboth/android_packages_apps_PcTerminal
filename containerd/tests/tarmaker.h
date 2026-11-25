/* Builds tar streams in memory for the extractor's tests: ustar headers, GNU long names, pax records. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tarball {
	char *data;
	size_t len, cap;
};

static inline void tar_put(struct tarball *t, const void *p, size_t n)
{
	if (t->len + n > t->cap) {
		t->cap = (t->len + n) * 2 + 4096;
		t->data = realloc(t->data, t->cap);
	}
	memcpy(t->data + t->len, p, n);
	t->len += n;
}

static inline void tar_pad(struct tarball *t)
{
	static const char zeros[512];
	tar_put(t, zeros, (512 - t->len % 512) % 512);
}

static inline void tar_header(struct tarball *t, const char *name, char type, unsigned mode, unsigned uid, unsigned gid,
			      size_t size, const char *link)
{
	unsigned char h[512] = { 0 };
	snprintf((char *)h, 100, "%s", name);
	snprintf((char *)h + 100, 8, "%07o", mode);
	snprintf((char *)h + 108, 8, "%07o", uid);
	snprintf((char *)h + 116, 8, "%07o", gid);
	snprintf((char *)h + 124, 12, "%011zo", size);
	snprintf((char *)h + 136, 12, "%011o", 1700000000u);
	memset(h + 148, ' ', 8);
	h[156] = (unsigned char)type;
	snprintf((char *)h + 157, 100, "%s", link ? link : "");
	memcpy(h + 257, "ustar\0" "00", 8);
	unsigned sum = 0;
	for (int i = 0; i < 512; i++)
		sum += h[i];
	snprintf((char *)h + 148, 8, "%06o", sum);
	tar_put(t, h, 512);
}

static inline void tar_entry(struct tarball *t, const char *name, char type, unsigned mode, const char *data,
			     const char *link)
{
	size_t size = data ? strlen(data) : 0;
	tar_header(t, name, type, mode, 0, 0, size, link);
	if (size) {
		tar_put(t, data, size);
		tar_pad(t);
	}
}

static inline void tar_file(struct tarball *t, const char *name, const char *data) { tar_entry(t, name, '0', 0644, data, NULL); }
static inline void tar_dir(struct tarball *t, const char *name) { tar_entry(t, name, '5', 0755, NULL, NULL); }
static inline void tar_symlink(struct tarball *t, const char *name, const char *to) { tar_entry(t, name, '2', 0777, NULL, to); }

/* A pax extended header ('x') with one key=value record for the next entry. */
static inline void tar_pax(struct tarball *t, const char *key, const char *value)
{
	char rec[1024];
	size_t body = strlen(key) + strlen(value) + 3, len = body + 1;
	while (snprintf(NULL, 0, "%zu", len) + body != len)
		len++;
	snprintf(rec, sizeof rec, "%zu %s=%s\n", len, key, value);
	tar_entry(t, "PaxHeader", 'x', 0644, rec, NULL);
}

static inline void tar_end(struct tarball *t)
{
	static const char zeros[1024];
	tar_put(t, zeros, sizeof zeros);
}
