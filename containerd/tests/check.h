/* A tiny test harness: CHECK records failures and keeps going, RUN prints a summary per test. */
#pragma once
#include <stdio.h>
#include <string.h>

static int check_failures;

#define CHECK(cond)                                                                            \
	do {                                                                                   \
		if (!(cond)) {                                                                 \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
			check_failures++;                                                      \
		}                                                                              \
	} while (0)

#define CHECK_STR(a, b)                                                                              \
	do {                                                                                         \
		const char *check_a = (a), *check_b = (b);                                           \
		if (strcmp(check_a, check_b)) {                                                      \
			fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, check_a, check_b); \
			check_failures++;                                                            \
		}                                                                                    \
	} while (0)

#define RUN(test)                                         \
	do {                                              \
		int check_before = check_failures;        \
		test();                                   \
		printf("%s %s\n", check_failures == check_before ? "ok  " : "FAIL", #test); \
	} while (0)

#define CHECK_DONE() (check_failures ? (fprintf(stderr, "%d check(s) failed\n", check_failures), 1) : 0)
