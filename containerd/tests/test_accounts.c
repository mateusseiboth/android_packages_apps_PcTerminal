/* Files the container's init writes inside it at every start: the person's account, hosts, resolv.conf. */
#include <stdlib.h>

#include "accounts.h"
#include "check.h"

static const char PASSWD[] = "root:x:0:0:root:/root:/bin/bash\n"
			     "daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin\n"
			     "ubuntu:x:1000:1000:Ubuntu:/home/ubuntu:/bin/bash\n"
			     "_apt:x:42:65534::/nonexistent:/usr/sbin/nologin\n";

static void the_account_takes_uid_1000(void)
{
	char *text = build_passwd(PASSWD, "mateus", "/bin/bash");
	CHECK_STR(text, "root:x:0:0:root:/root:/bin/bash\n"
			"daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin\n"
			"_apt:x:42:65534::/nonexistent:/usr/sbin/nologin\n"
			"mateus:x:1000:1000:mateus:/home/mateus:/bin/bash\n");
	char *again = build_passwd(text, "mateus", "/bin/bash");
	CHECK_STR(again, text); /* nothing changes on the next start */
	free(text);
	free(again);
	text = build_passwd("root:x:0:0::/root:/bin/sh", "user", "/bin/sh"); /* no final newline */
	CHECK_STR(text, "root:x:0:0::/root:/bin/sh\nuser:x:1000:1000:user:/home/user:/bin/sh\n");
	free(text);
}

static void the_group_and_sudo_membership(void)
{
	char *text = build_group("root:x:0:\nsudo:x:27:ubuntu\nwheel:x:10:\nubuntu:x:1000:\nusers:x:100:\n", "mateus");
	CHECK_STR(text, "root:x:0:\nsudo:x:27:ubuntu,mateus\nwheel:x:10:mateus\nusers:x:100:\nmateus:x:1000:\n");
	char *again = build_group(text, "mateus");
	CHECK_STR(again, text);
	free(text);
	free(again);
}

static void a_locked_password(void)
{
	char *text = build_shadow("root:*:19000:0:99999:7:::\nubuntu:!:19000:0:99999:7:::\n", "mateus", "ubuntu");
	CHECK_STR(text, "root:*:19000:0:99999:7:::\nmateus:!:19000:0:99999:7:::\n");
	char *again = build_shadow(text, "mateus", NULL);
	CHECK_STR(again, text);
	free(text);
	free(again);
}

static void finds_who_had_uid_1000(void)
{
	char name[64];
	CHECK(!find_user_with_uid(PASSWD, 1000, name, sizeof name));
	CHECK_STR(name, "ubuntu");
	CHECK(find_user_with_uid(PASSWD, 1234, name, sizeof name));
}

static void reads_the_login_shell(void)
{
	char shell[64];
	CHECK(!find_login_shell(PASSWD, "ubuntu", shell, sizeof shell));
	CHECK_STR(shell, "/bin/bash");
	CHECK(find_login_shell(PASSWD, "nobody", shell, sizeof shell));
	CHECK(find_login_shell("x:x:1:1::/:\n", "x", shell, sizeof shell)); /* empty shell field */
}

static void hosts_and_name_servers(void)
{
	char text[512];
	build_hosts("debian", text, sizeof text);
	CHECK_STR(text, "127.0.0.1\tlocalhost\n::1\tlocalhost ip6-localhost ip6-loopback\n127.0.1.1\tdebian\n");
	const char *servers[] = { "192.168.0.1", "2001:4860:4860::8888" };
	CHECK(!build_resolv_conf(servers, 2, text, sizeof text));
	CHECK_STR(text, "nameserver 192.168.0.1\nnameserver 2001:4860:4860::8888\n");
	const char *bad[] = { "1.1.1.1\nsearch evil" };
	CHECK(build_resolv_conf(bad, 1, text, sizeof text));
	CHECK(is_valid_address("10.0.0.1"));
	CHECK(!is_valid_address("10.0.0.1 x"));
}

int main(void)
{
	RUN(the_account_takes_uid_1000);
	RUN(the_group_and_sudo_membership);
	RUN(a_locked_password);
	RUN(finds_who_had_uid_1000);
	RUN(reads_the_login_shell);
	RUN(hosts_and_name_servers);
	return CHECK_DONE();
}
