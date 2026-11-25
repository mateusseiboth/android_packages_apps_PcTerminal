#include "ids.h"

#include <string.h>

int is_trusted_uid(unsigned uid)
{
	return uid == 0 || uid % AID_USER_OFFSET == AID_SYSTEM;
}

unsigned find_android_user(unsigned uid)
{
	return uid / AID_USER_OFFSET;
}

unsigned find_host_id_base(unsigned user_id)
{
	return HOST_ID_START + user_id * AID_USER_OFFSET;
}

static int is_lower_or_digit(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

/* first is the class of the first character, rest the extra characters allowed after it. */
static int is_word(const char *s, int (*first)(char), const char *rest, size_t max)
{
	size_t len = s ? strlen(s) : 0;
	if (!len || len > max || !first(s[0]))
		return 0;
	for (size_t i = 1; i < len; i++)
		if (!is_lower_or_digit(s[i]) && !strchr(rest, s[i]))
			return 0;
	return 1;
}

static int is_user_start(char c) { return (c >= 'a' && c <= 'z') || c == '_'; }

int is_valid_name(const char *name)
{
	return is_word(name, is_lower_or_digit, "-", 31);
}

int is_valid_user_name(const char *name)
{
	return is_word(name, is_user_start, "_-", 32) && strcmp(name, "root");
}
