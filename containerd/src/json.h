/*
 * The protocol's JSON (PROTOCOL.md): a small tree parser for requests and a writer for replies. Numbers
 * are kept as integers when they are integral; strings are UTF-8.
 */
#pragma once
#include <stddef.h>

enum json_type { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT };

struct json {
	enum json_type type;
	char *key; /* the member name inside an object */
	char *str; /* JSON_STRING */
	long long integer;
	int is_integer; /* JSON_NUMBER without fraction or exponent */
	int boolean;
	struct json *child; /* first element or member */
	struct json *next;
};

/* NULL when the text is not one valid JSON value (trailing spaces are fine). */
struct json *json_parse(const char *text);
void json_free(struct json *j);

const struct json *json_find(const struct json *obj, const char *key);
size_t json_count(const struct json *container);
const struct json *json_at(const struct json *array, size_t index);
/* The member's string, or NULL when missing or of another type. */
const char *json_string(const struct json *obj, const char *key);
/* 0 and *out when the member is an integer, else -1. */
int json_int(const struct json *obj, const char *key, long long *out);
/* The member's boolean, or fallback when missing or of another type. */
int json_bool(const struct json *obj, const char *key, int fallback);

#define JW_MAX_DEPTH 16

struct jw {
	char *buf;
	size_t len, cap;
	int depth;
	int has_items[JW_MAX_DEPTH];
	int after_key;
};

void jw_init(struct jw *w);
void jw_free(struct jw *w);
void jw_begin_object(struct jw *w);
void jw_end_object(struct jw *w);
void jw_begin_array(struct jw *w);
void jw_end_array(struct jw *w);
void jw_key(struct jw *w, const char *key);
void jw_string(struct jw *w, const char *s);
void jw_int(struct jw *w, long long n);
void jw_bool(struct jw *w, int b);
void jw_null(struct jw *w);
void jw_key_string(struct jw *w, const char *key, const char *s);
void jw_key_int(struct jw *w, const char *key, long long n);
void jw_key_bool(struct jw *w, const char *key, int b);
void jw_key_null(struct jw *w, const char *key);
/* A string member, or null when s is NULL. */
void jw_key_string_or_null(struct jw *w, const char *key, const char *s);
/* The text written so far with a newline: the reply line. */
const char *jw_line(struct jw *w);
