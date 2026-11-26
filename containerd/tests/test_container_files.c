/* What the init writes inside the container at every start (here below a temporary folder). */
#include <stdlib.h>
#include <sys/stat.h>

#include "check.h"
#include "container_files.h"

static char root[64];

static char *read_back(const char *rel)
{
	static char text[1024];
	char path[256];
	snprintf(path, sizeof path, "%s/%s", root, rel);
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
	if (f)
		fclose(f);
	text[n] = 0;
	return text;
}

static void writes_account_hosts_and_dns(void)
{
	char cmd[512];
	snprintf(root, sizeof root, "/tmp/pccfiles-XXXXXX");
	CHECK(mkdtemp(root) != NULL);
	snprintf(cmd, sizeof cmd,
		 "cd %s && mkdir -p etc/sudoers.d bin && printf 'root:x:0:0::/root:/bin/sh\\n' > etc/passwd && "
		 "printf 'root:x:0:\\nwheel:x:10:\\n' > etc/group && printf 'root:*:1:0:::::\\n' > etc/shadow && "
		 "touch bin/bash && chmod 755 bin/bash && ln -s ../run/systemd/resolve/stub-resolv.conf etc/resolv.conf",
		 root);
	CHECK(!system(cmd));
	struct container_files f = { root, "debian", "mateus", "nameserver 1.1.1.1\n" };
	CHECK(!write_container_files(&f));
	CHECK_STR(read_back("etc/passwd"), "root:x:0:0::/root:/bin/sh\nmateus:x:1000:1000:mateus:/home/mateus:/bin/bash\n");
	CHECK_STR(read_back("etc/group"), "root:x:0:\nwheel:x:10:mateus\nmateus:x:1000:\n");
	CHECK_STR(read_back("etc/shadow"), "root:*:1:0:::::\nmateus:!:19000:0:99999:7:::\n");
	CHECK_STR(read_back("etc/hosts"), "127.0.0.1\tlocalhost\n::1\tlocalhost ip6-localhost ip6-loopback\n127.0.1.1\tdebian\n");
	CHECK_STR(read_back("etc/hostname"), "debian\n");
	CHECK_STR(read_back("etc/resolv.conf"), "nameserver 1.1.1.1\n");
	CHECK_STR(read_back("etc/sudoers.d/pc-containerd"), "mateus ALL=(ALL:ALL) NOPASSWD: ALL\n");
	struct stat st;
	snprintf(cmd, sizeof cmd, "%s/etc/resolv.conf", root);
	CHECK(!lstat(cmd, &st) && S_ISREG(st.st_mode)); /* the link was replaced, not followed */
	snprintf(cmd, sizeof cmd, "%s/run/user/1000", root);
	CHECK(!stat(cmd, &st) && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700);

	struct container_files keep = { root, "debian", "mateus", NULL };
	CHECK(!write_container_files(&keep));
	CHECK_STR(read_back("etc/resolv.conf"), "nameserver 1.1.1.1\n"); /* no dns: left as it was */
	snprintf(cmd, sizeof cmd, "rm -rf %s", root);
	CHECK(!system(cmd));
}

/* init starts the daemon with umask 077: the container's user must still read what it writes. */
static void files_keep_their_modes_under_any_umask(void)
{
	char cmd[512];
	snprintf(root, sizeof root, "/tmp/pccfiles-XXXXXX");
	CHECK(mkdtemp(root) != NULL);
	snprintf(cmd, sizeof cmd, "cd %s && mkdir -p etc && printf 'root:x:0:0::/root:/bin/sh\\n' > etc/passwd && "
		 "printf 'root:x:0:\\n' > etc/group && printf 'root:*:1:0:::::\\n' > etc/shadow", root);
	CHECK(!system(cmd));
	mode_t old = umask(077);
	struct container_files f = { root, "debian", "mateus", "nameserver 1.1.1.1\n" };
	CHECK(!write_container_files(&f));
	umask(old);
	const char *readable[] = { "etc/resolv.conf", "etc/hosts", "etc/hostname", "etc/passwd", "etc/group" };
	struct stat st;
	for (size_t i = 0; i < sizeof readable / sizeof *readable; i++) {
		snprintf(cmd, sizeof cmd, "%s/%s", root, readable[i]);
		CHECK(!stat(cmd, &st) && (st.st_mode & 0777) == 0644);
	}
	snprintf(cmd, sizeof cmd, "rm -rf %s", root);
	CHECK(!system(cmd));
}

static int has_mode(const char *rel, unsigned mode)
{
	char path[256];
	struct stat st;
	snprintf(path, sizeof path, "%s/%s", root, rel);
	return !lstat(path, &st) && S_ISDIR(st.st_mode) && (st.st_mode & 07777) == mode;
}

/* /run/user is crossable, so uid 1000 reaches /run/user/1000 (PulseAudio, PipeWire, the bus live there). */
static void each_user_reaches_their_runtime_folder(void)
{
	char cmd[512];
	snprintf(root, sizeof root, "/tmp/pccfiles-XXXXXX");
	CHECK(mkdtemp(root) != NULL);
	snprintf(cmd, sizeof cmd, "cd %s && mkdir -p etc run && printf 'root:x:0:0::/root:/bin/sh\\n' > etc/passwd", root);
	CHECK(!system(cmd));
	mode_t old = umask(077);
	struct container_files f = { root, "debian", "mateus", NULL };
	CHECK(!write_container_files(&f));
	umask(old);
	CHECK(has_mode("run/user", 0755));
	CHECK(has_mode("run/user/0", 0700));
	CHECK(has_mode("run/user/1000", 0700));
	snprintf(cmd, sizeof cmd, "rm -rf %s", root);
	CHECK(!system(cmd));
}

int main(void)
{
	RUN(writes_account_hosts_and_dns);
	RUN(files_keep_their_modes_under_any_umask);
	RUN(each_user_reaches_their_runtime_folder);
	return CHECK_DONE();
}
