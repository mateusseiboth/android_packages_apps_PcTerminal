#define _GNU_SOURCE
#include "accounts.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ids.h"

/* Field index (0-based) of a colon-separated line, copied into out; -1 when missing. */
static int read_field(const char *line, size_t len, int index, char *out, size_t cap)
{
	const char *p = line, *end = line + len;
	for (int i = 0; i < index; i++) {
		p = memchr(p, ':', (size_t)(end - p));
		if (!p)
			return -1;
		p++;
	}
	const char *stop = memchr(p, ':', (size_t)(end - p));
	size_t n = (size_t)((stop ? stop : end) - p);
	if (n >= cap)
		return -1;
	memcpy(out, p, n);
	out[n] = 0;
	return 0;
}

struct line_filter {
	const char *name;      /* drop lines of this name (field 0) */
	const char *replaced;  /* and of this one, may be NULL */
	int id_field;          /* drop lines whose field holds id, -1 for none */
	const char *add_to[2]; /* append name to the member list (field 3) of these groups */
};

static int is_dropped(const struct line_filter *f, const char *line, size_t len)
{
	char field[256], id[16];
	snprintf(id, sizeof id, "%u", CONTAINER_USER_ID);
	if (read_field(line, len, 0, field, sizeof field))
		return 0;
	if (!strcmp(field, f->name) || (f->replaced && !strcmp(field, f->replaced)))
		return 1;
	return f->id_field >= 0 && !read_field(line, len, f->id_field, field, sizeof field) && !strcmp(field, id);
}

static int is_member_group(const struct line_filter *f, const char *line, size_t len)
{
	char field[256];
	if (read_field(line, len, 0, field, sizeof field))
		return 0;
	for (size_t i = 0; i < 2; i++)
		if (f->add_to[i] && !strcmp(field, f->add_to[i]))
			return 1;
	return 0;
}

static int has_member(const char *line, size_t len, const char *name)
{
	char members[1024];
	if (read_field(line, len, 3, members, sizeof members))
		return 0;
	for (char *save = NULL, *m = strtok_r(members, ",", &save); m; m = strtok_r(NULL, ",", &save))
		if (!strcmp(m, name))
			return 1;
	return 0;
}

static void append(char **out, size_t *len, const char *s, size_t n)
{
	char *grown = realloc(*out, *len + n + 1);
	if (!grown)
		return;
	memcpy(grown + *len, s, n);
	*len += n;
	grown[*len] = 0;
	*out = grown;
}

/* old filtered line by line, then extra appended. */
static char *rebuild(const char *old, const struct line_filter *f, const char *extra)
{
	char *out = NULL;
	size_t len = 0;
	append(&out, &len, "", 0);
	for (const char *p = old; *p;) {
		size_t n = strcspn(p, "\n");
		if (n && !is_dropped(f, p, n)) {
			append(&out, &len, p, n);
			int adds = is_member_group(f, p, n) && !has_member(p, n, f->name);
			if (adds && p[n - 1] != ':')
				append(&out, &len, ",", 1);
			if (adds)
				append(&out, &len, f->name, strlen(f->name));
			append(&out, &len, "\n", 1);
		}
		p += n + (p[n] == '\n');
	}
	append(&out, &len, extra, strlen(extra));
	return out;
}

char *build_passwd(const char *old, const char *name, const char *shell)
{
	char extra[512];
	struct line_filter f = { name, NULL, 2, { NULL, NULL } };
	snprintf(extra, sizeof extra, "%s:x:%u:%u:%s:/home/%s:%s\n", name, CONTAINER_USER_ID, CONTAINER_USER_ID, name, name, shell);
	return rebuild(old, &f, extra);
}

char *build_group(const char *old, const char *name)
{
	char extra[128];
	struct line_filter f = { name, NULL, 2, { "sudo", "wheel" } };
	snprintf(extra, sizeof extra, "%s:x:%u:\n", name, CONTAINER_USER_ID);
	return rebuild(old, &f, extra);
}

char *build_shadow(const char *old, const char *name, const char *replaced)
{
	char extra[128];
	struct line_filter f = { name, replaced, -1, { NULL, NULL } };
	snprintf(extra, sizeof extra, "%s:!:19000:0:99999:7:::\n", name);
	return rebuild(old, &f, extra);
}

/* The passwd line whose field at index equals value. */
static int find_line(const char *passwd, int index, const char *value, int out_index, char *out, size_t cap)
{
	char field[256];
	for (const char *p = passwd; *p;) {
		size_t n = strcspn(p, "\n");
		if (!read_field(p, n, index, field, sizeof field) && !strcmp(field, value))
			return read_field(p, n, out_index, out, cap);
		p += n + (p[n] == '\n');
	}
	return -1;
}

int find_user_with_uid(const char *passwd, unsigned uid, char *name, size_t cap)
{
	char id[16];
	snprintf(id, sizeof id, "%u", uid);
	return find_line(passwd, 2, id, 0, name, cap);
}

int find_login_shell(const char *passwd, const char *name, char *shell, size_t cap)
{
	return find_line(passwd, 0, name, 6, shell, cap) || !*shell ? -1 : 0;
}

void build_hosts(const char *hostname, char *out, size_t cap)
{
	snprintf(out, cap, "127.0.0.1\tlocalhost\n::1\tlocalhost ip6-localhost ip6-loopback\n127.0.1.1\t%s\n", hostname);
}

int is_valid_address(const char *s)
{
	unsigned char buf[16];
	return inet_pton(AF_INET, s, buf) == 1 || inet_pton(AF_INET6, s, buf) == 1;
}

int build_resolv_conf(const char *const *servers, int count, char *out, size_t cap)
{
	size_t len = 0;
	out[0] = 0;
	for (int i = 0; i < count; i++) {
		if (!is_valid_address(servers[i]))
			return -1;
		int n = snprintf(out + len, cap - len, "nameserver %s\n", servers[i]);
		if (n < 0 || (size_t)n >= cap - len)
			return -1;
		len += (size_t)n;
	}
	return 0;
}
