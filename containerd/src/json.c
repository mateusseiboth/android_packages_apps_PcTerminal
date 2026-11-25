#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NESTING 32

/* ---- parsing -------------------------------------------------------------------------- */

struct parser {
	const char *p;
	int depth;
};

static struct json *parse_value(struct parser *ps);

static void skip_spaces(struct parser *ps)
{
	while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')
		ps->p++;
}

static struct json *new_node(enum json_type type)
{
	struct json *j = calloc(1, sizeof *j);
	if (j)
		j->type = type;
	return j;
}

static int is_literal(struct parser *ps, const char *word)
{
	size_t n = strlen(word);
	if (strncmp(ps->p, word, n))
		return 0;
	ps->p += n;
	return 1;
}

static struct json *parse_literal(struct parser *ps)
{
	static const struct {
		const char *word;
		enum json_type type;
		int boolean;
	} LITERALS[] = { { "true", JSON_BOOL, 1 }, { "false", JSON_BOOL, 0 }, { "null", JSON_NULL, 0 } };
	for (size_t i = 0; i < sizeof LITERALS / sizeof *LITERALS; i++) {
		if (!is_literal(ps, LITERALS[i].word))
			continue;
		struct json *j = new_node(LITERALS[i].type);
		if (j)
			j->boolean = LITERALS[i].boolean;
		return j;
	}
	return NULL;
}

static struct json *parse_number(struct parser *ps)
{
	const char *start = ps->p;
	char *end = NULL;
	if (*ps->p == '-')
		ps->p++;
	if (!isdigit((unsigned char)*ps->p))
		return NULL;
	strtod(start, &end);
	struct json *j = end > start ? new_node(JSON_NUMBER) : NULL;
	if (!j)
		return NULL;
	j->is_integer = !memchr(start, '.', (size_t)(end - start)) && !memchr(start, 'e', (size_t)(end - start)) &&
			!memchr(start, 'E', (size_t)(end - start));
	j->integer = j->is_integer ? strtoll(start, NULL, 10) : 0;
	ps->p = end;
	return j;
}

static int read_hex4(const char *p, unsigned *out)
{
	*out = 0;
	for (int i = 0; i < 4; i++) {
		int c = (unsigned char)p[i];
		int v = isdigit(c) ? c - '0' : isxdigit(c) ? (tolower(c) - 'a' + 10) : -1;
		if (v < 0)
			return -1;
		*out = *out << 4 | (unsigned)v;
	}
	return 0;
}

static size_t put_utf8(char *out, unsigned cp)
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xc0 | cp >> 6);
		out[1] = (char)(0x80 | (cp & 0x3f));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xe0 | cp >> 12);
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[2] = (char)(0x80 | (cp & 0x3f));
		return 3;
	}
	out[0] = (char)(0xf0 | cp >> 18);
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
	out[3] = (char)(0x80 | (cp & 0x3f));
	return 4;
}

/* \uXXXX, with a surrogate pair when it is a high surrogate; advances *p past it. */
static int read_unicode_escape(const char **p, unsigned *cp)
{
	unsigned lo = 0;
	if (read_hex4(*p, cp))
		return -1;
	*p += 4;
	if (*cp >= 0xdc00 && *cp <= 0xdfff)
		return -1;
	if (*cp < 0xd800 || *cp > 0xdbff)
		return 0;
	if ((*p)[0] != '\\' || (*p)[1] != 'u' || read_hex4(*p + 2, &lo) || lo < 0xdc00 || lo > 0xdfff)
		return -1;
	*p += 6;
	*cp = 0x10000 + ((*cp - 0xd800) << 10) + (lo - 0xdc00);
	return 0;
}

static const char SIMPLE_ESCAPES[][2] = { { '"', '"' }, { '\\', '\\' }, { '/', '/' }, { 'b', '\b' },
					  { 'f', '\f' },  { 'n', '\n' },   { 'r', '\r' }, { 't', '\t' } };

static int find_simple_escape(char c, char *out)
{
	for (size_t i = 0; i < sizeof SIMPLE_ESCAPES / sizeof *SIMPLE_ESCAPES; i++)
		if (SIMPLE_ESCAPES[i][0] == c) {
			*out = SIMPLE_ESCAPES[i][1];
			return 0;
		}
	return -1;
}

/* One escape after the backslash at *p; writes its UTF-8 bytes to out. */
static int read_escape(const char **p, char *out, size_t *n)
{
	char c = *(*p)++;
	unsigned cp = 0;
	if (c != 'u') {
		*n = 1;
		return find_simple_escape(c, out);
	}
	if (read_unicode_escape(p, &cp))
		return -1;
	*n = put_utf8(out, cp);
	return 0;
}

/* A quoted string at ps->p into a malloc'd buffer (escapes never make it longer than the source). */
static char *parse_raw_string(struct parser *ps)
{
	if (*ps->p != '"')
		return NULL;
	const char *p = ps->p + 1;
	char *out = malloc(strlen(p) + 1);
	size_t len = 0;
	int failed = !out;
	while (!failed && *p && *p != '"') {
		size_t n = 0;
		failed = (unsigned char)*p < 0x20;
		if (!failed && *p != '\\') {
			out[len++] = *p++;
			continue;
		}
		p++;
		failed = failed || read_escape(&p, out + len, &n);
		len += n;
	}
	if (failed || *p != '"') {
		free(out);
		return NULL;
	}
	out[len] = 0;
	ps->p = p + 1;
	return out;
}

static struct json *parse_string(struct parser *ps)
{
	char *s = parse_raw_string(ps);
	struct json *j = s ? new_node(JSON_STRING) : NULL;
	if (!j) {
		free(s);
		return NULL;
	}
	j->str = s;
	return j;
}

/* Elements or members up to the closing bracket; member names come before ':' when is_object. */
static struct json *parse_container(struct parser *ps, enum json_type type, char close)
{
	struct json *j = ++ps->depth <= MAX_NESTING ? new_node(type) : NULL, **tail;
	if (!j)
		return NULL;
	tail = &j->child;
	ps->p++;
	skip_spaces(ps);
	if (*ps->p == close) {
		ps->p++;
		ps->depth--;
		return j;
	}
	for (;;) {
		char *key = NULL;
		skip_spaces(ps);
		if (type == JSON_OBJECT) {
			key = parse_raw_string(ps);
			skip_spaces(ps);
			if (!key || *ps->p++ != ':') {
				free(key);
				break;
			}
		}
		struct json *item = parse_value(ps);
		if (!item) {
			free(key);
			break;
		}
		item->key = key;
		*tail = item;
		tail = &item->next;
		skip_spaces(ps);
		if (*ps->p == ',') {
			ps->p++;
			continue;
		}
		if (*ps->p++ != close)
			break;
		ps->depth--;
		return j;
	}
	json_free(j);
	return NULL;
}

static struct json *parse_value(struct parser *ps)
{
	skip_spaces(ps);
	switch (*ps->p) {
	case '{':
		return parse_container(ps, JSON_OBJECT, '}');
	case '[':
		return parse_container(ps, JSON_ARRAY, ']');
	case '"':
		return parse_string(ps);
	case 't':
	case 'f':
	case 'n':
		return parse_literal(ps);
	default:
		return parse_number(ps);
	}
}

struct json *json_parse(const char *text)
{
	struct parser ps = { text, 0 };
	struct json *j = parse_value(&ps);
	skip_spaces(&ps);
	if (j && !*ps.p)
		return j;
	json_free(j);
	return NULL;
}

void json_free(struct json *j)
{
	while (j) {
		struct json *next = j->next;
		json_free(j->child);
		free(j->key);
		free(j->str);
		free(j);
		j = next;
	}
}

/* ---- reading -------------------------------------------------------------------------- */

const struct json *json_find(const struct json *obj, const char *key)
{
	if (!obj || obj->type != JSON_OBJECT)
		return NULL;
	for (const struct json *m = obj->child; m; m = m->next)
		if (!strcmp(m->key, key))
			return m;
	return NULL;
}

size_t json_count(const struct json *container)
{
	size_t n = 0;
	for (const struct json *m = container ? container->child : NULL; m; m = m->next)
		n++;
	return n;
}

const struct json *json_at(const struct json *array, size_t index)
{
	const struct json *m = array ? array->child : NULL;
	for (; m && index; index--)
		m = m->next;
	return m;
}

const char *json_string(const struct json *obj, const char *key)
{
	const struct json *m = json_find(obj, key);
	return m && m->type == JSON_STRING ? m->str : NULL;
}

int json_int(const struct json *obj, const char *key, long long *out)
{
	const struct json *m = json_find(obj, key);
	if (!m || m->type != JSON_NUMBER || !m->is_integer)
		return -1;
	*out = m->integer;
	return 0;
}

int json_bool(const struct json *obj, const char *key, int fallback)
{
	const struct json *m = json_find(obj, key);
	return m && m->type == JSON_BOOL ? m->boolean : fallback;
}

/* ---- writing -------------------------------------------------------------------------- */

static void put(struct jw *w, const char *s, size_t n)
{
	if (w->len + n + 2 > w->cap) {
		size_t cap = (w->len + n + 2) * 2;
		char *buf = realloc(w->buf, cap);
		if (!buf)
			return;
		w->buf = buf;
		w->cap = cap;
	}
	memcpy(w->buf + w->len, s, n);
	w->len += n;
	w->buf[w->len] = 0;
}

static void put_str(struct jw *w, const char *s) { put(w, s, strlen(s)); }

/* The comma before a value, unless it follows its key or opens its container. */
static void begin_item(struct jw *w)
{
	if (w->after_key) {
		w->after_key = 0;
		return;
	}
	if (w->depth > 0 && w->has_items[w->depth - 1]++)
		put_str(w, ",");
}

static void put_quoted(struct jw *w, const char *s)
{
	put_str(w, "\"");
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		char esc[8];
		char simple = 0;
		for (size_t i = 0; i < sizeof SIMPLE_ESCAPES / sizeof *SIMPLE_ESCAPES && !simple; i++)
			simple = SIMPLE_ESCAPES[i][1] == (char)*p && SIMPLE_ESCAPES[i][0] != '/' ? SIMPLE_ESCAPES[i][0] : 0;
		if (simple)
			snprintf(esc, sizeof esc, "\\%c", simple);
		else if (*p < 0x20)
			snprintf(esc, sizeof esc, "\\u%04x", *p);
		else
			snprintf(esc, sizeof esc, "%c", *p);
		put_str(w, esc);
	}
	put_str(w, "\"");
}

void jw_init(struct jw *w)
{
	*w = (struct jw){ 0 };
	put(w, "", 0);
}

void jw_free(struct jw *w)
{
	free(w->buf);
	*w = (struct jw){ 0 };
}

static void open_container(struct jw *w, const char *bracket)
{
	begin_item(w);
	put_str(w, bracket);
	if (w->depth < JW_MAX_DEPTH)
		w->has_items[w->depth] = 0;
	w->depth++;
}

static void close_container(struct jw *w, const char *bracket)
{
	w->depth--;
	put_str(w, bracket);
}

void jw_begin_object(struct jw *w) { open_container(w, "{"); }
void jw_end_object(struct jw *w) { close_container(w, "}"); }
void jw_begin_array(struct jw *w) { open_container(w, "["); }
void jw_end_array(struct jw *w) { close_container(w, "]"); }

void jw_key(struct jw *w, const char *key)
{
	begin_item(w);
	put_quoted(w, key);
	put_str(w, ":");
	w->after_key = 1;
}

void jw_string(struct jw *w, const char *s)
{
	begin_item(w);
	put_quoted(w, s);
}

void jw_int(struct jw *w, long long n)
{
	char num[32];
	snprintf(num, sizeof num, "%lld", n);
	begin_item(w);
	put_str(w, num);
}

void jw_bool(struct jw *w, int b)
{
	begin_item(w);
	put_str(w, b ? "true" : "false");
}

void jw_null(struct jw *w)
{
	begin_item(w);
	put_str(w, "null");
}

void jw_key_string(struct jw *w, const char *key, const char *s)
{
	jw_key(w, key);
	jw_string(w, s);
}

void jw_key_int(struct jw *w, const char *key, long long n)
{
	jw_key(w, key);
	jw_int(w, n);
}

void jw_key_bool(struct jw *w, const char *key, int b)
{
	jw_key(w, key);
	jw_bool(w, b);
}

void jw_key_null(struct jw *w, const char *key)
{
	jw_key(w, key);
	jw_null(w);
}

void jw_key_string_or_null(struct jw *w, const char *key, const char *s)
{
	jw_key(w, key);
	if (s)
		jw_string(w, s);
	else
		jw_null(w);
}

const char *jw_line(struct jw *w)
{
	if (!w->len || w->buf[w->len - 1] != '\n')
		put_str(w, "\n");
	return w->buf ? w->buf : "\n";
}
