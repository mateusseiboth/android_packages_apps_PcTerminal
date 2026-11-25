/* The protocol's JSON: one object per line in, one object per line out. */
#include "check.h"
#include "json.h"

static void parses_a_request(void)
{
	struct json *j = json_parse("{\"id\":7,\"cmd\":\"exec\",\"pty\":true,\"argv\":[\"ls\",\"-l\"],\"env\":{\"A\":\"b\"}}");
	long long id = 0;
	CHECK(j != NULL);
	CHECK(!json_int(j, "id", &id) && id == 7);
	CHECK_STR(json_string(j, "cmd"), "exec");
	CHECK(json_bool(j, "pty", 0) == 1);
	CHECK(json_bool(j, "missing", 1) == 1);
	const struct json *argv = json_find(j, "argv");
	CHECK(argv && argv->type == JSON_ARRAY && json_count(argv) == 2);
	CHECK_STR(json_at(argv, 1)->str, "-l");
	CHECK_STR(json_string(json_find(j, "env"), "A"), "b");
	json_free(j);
}

static void decodes_string_escapes(void)
{
	struct json *j = json_parse("{\"s\":\"a\\\"b\\\\c\\n\\t\\u00e9\\u20ac\\ud83d\\ude00/\"}");
	CHECK(j != NULL);
	CHECK_STR(json_string(j, "s"), "a\"b\\c\n\t\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80/");
	json_free(j);
}

static void reads_numbers_nulls_and_nesting(void)
{
	struct json *j = json_parse(" { \"n\" : -12 , \"x\": null, \"f\": 1.5e3, \"o\": {\"a\": [1, [2], {}]} } ");
	long long n = 0;
	CHECK(j != NULL);
	CHECK(!json_int(j, "n", &n) && n == -12);
	CHECK(json_find(j, "x")->type == JSON_NULL);
	CHECK(json_int(j, "f", &n)); /* not an integer */
	CHECK(json_string(j, "n") == NULL);
	CHECK(json_count(json_find(json_find(j, "o"), "a")) == 3);
	json_free(j);
}

static void rejects_malformed_text(void)
{
	static const char *const BAD[] = {
		"", "{", "{\"a\":}", "{\"a\" 1}", "[1,]", "{\"a\":1,}", "{\"a\":tru}", "\"\\x\"", "{\"a\":1} x",
		"{\"a\":\"\x01\"}", "{\"a\":\"\\ud83d\"}", "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]",
	};
	for (size_t i = 0; i < sizeof BAD / sizeof *BAD; i++) {
		struct json *j = json_parse(BAD[i]);
		if (j)
			fprintf(stderr, "accepted: %s\n", BAD[i]);
		CHECK(j == NULL);
		json_free(j);
	}
}

static void writes_objects_with_commas_and_escapes(void)
{
	struct jw w;
	jw_init(&w);
	jw_begin_object(&w);
	jw_key_int(&w, "id", 3);
	jw_key_bool(&w, "ok", 1);
	jw_key_string(&w, "s", "a\"b\\\n\x01\xc3\xa9");
	jw_key_null(&w, "x");
	jw_key(&w, "list");
	jw_begin_array(&w);
	jw_string(&w, "a");
	jw_int(&w, -1);
	jw_begin_object(&w);
	jw_end_object(&w);
	jw_end_array(&w);
	jw_end_object(&w);
	CHECK_STR(jw_line(&w), "{\"id\":3,\"ok\":true,\"s\":\"a\\\"b\\\\\\n\\u0001\xc3\xa9\",\"x\":null,\"list\":[\"a\",-1,{}]}\n");
	jw_free(&w);
}

static void a_written_object_parses_back(void)
{
	struct jw w;
	jw_init(&w);
	jw_begin_object(&w);
	jw_key_string(&w, "t", "tab\there \"quoted\" \x7f");
	jw_end_object(&w);
	struct json *j = json_parse(jw_line(&w));
	CHECK(j != NULL);
	CHECK_STR(json_string(j, "t"), "tab\there \"quoted\" \x7f");
	json_free(j);
	jw_free(&w);
}

int main(void)
{
	RUN(parses_a_request);
	RUN(decodes_string_escapes);
	RUN(reads_numbers_nulls_and_nesting);
	RUN(rejects_malformed_text);
	RUN(writes_objects_with_commas_and_escapes);
	RUN(a_written_object_parses_back);
	return CHECK_DONE();
}
