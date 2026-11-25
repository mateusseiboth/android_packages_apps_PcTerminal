/*
 * Text of the files the container's init writes inside the container at every start: the person's
 * account (always uid/gid 1000, CONTAINER_USER_ID), /etc/hosts and /etc/resolv.conf. Pure text in, text out.
 */
#pragma once
#include <stddef.h>

/* /etc/passwd with one entry for name at uid 1000, replacing any other user that had uid 1000. malloc'd. */
char *build_passwd(const char *old, const char *name, const char *shell);
/* /etc/group with name's group at gid 1000 and name in sudo and wheel when they exist. malloc'd. */
char *build_group(const char *old, const char *name);
/* /etc/shadow with a locked password for name (sudo is passwordless); drops replaced (may be NULL). */
char *build_shadow(const char *old, const char *name, const char *replaced);
int find_user_with_uid(const char *passwd, unsigned uid, char *name, size_t cap);
int find_login_shell(const char *passwd, const char *name, char *shell, size_t cap);
void build_hosts(const char *hostname, char *out, size_t cap);
int is_valid_address(const char *s);
/* -1 when an address is not a plain IPv4 or IPv6 address. */
int build_resolv_conf(const char *const *servers, int count, char *out, size_t cap);
