/* Extracting a rootfs tarball: every entry type the images use, ids shifted into the container's range,
 * and nothing ever written outside the root. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "tar.h"
#include "tarmaker.h"

static char base_dir[64];
static char root_dir[128];

struct chown_log {
	int count;
	char last_name[256];
	unsigned last_uid, last_gid;
	unsigned root_uid;
};

static int record_chown(void *ctx, int dirfd, const char *name, unsigned uid, unsigned gid)
{
	struct chown_log *log = ctx;
	(void)dirfd;
	log->count++;
	snprintf(log->last_name, sizeof log->last_name, "%s", name);
	log->last_uid = uid;
	log->last_gid = gid;
	if (!*name)
		log->root_uid = uid;
	return 0;
}

static void reset_tree(void)
{
	char cmd[256];
	if (*base_dir) {
		snprintf(cmd, sizeof cmd, "rm -rf %s", base_dir);
		CHECK(!system(cmd));
	}
	snprintf(base_dir, sizeof base_dir, "/tmp/pcctar-XXXXXX");
	CHECK(mkdtemp(base_dir) != NULL);
	snprintf(root_dir, sizeof root_dir, "%s/rootfs", base_dir);
	CHECK(!mkdir(root_dir, 0755));
}

static const char *path_in(const char *rel)
{
	static char out[4][PATH_MAX];
	static int next;
	char *p = out[next++ % 4];
	snprintf(p, PATH_MAX, "%s/%s", base_dir, rel);
	return p;
}

static char *read_file(const char *rel)
{
	static char text[256];
	FILE *f = fopen(path_in(rel), "r");
	size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
	if (f)
		fclose(f);
	text[n] = 0;
	return f ? text : NULL;
}

static int open_stream(struct tarball *t)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/stream.tar", base_dir);
	FILE *f = fopen(path, "w");
	fwrite(t->data, 1, t->len, f);
	fclose(f);
	return open(path, O_RDONLY | O_CLOEXEC);
}

static const char *extract(struct tarball *t, struct chown_log *log, struct tar_progress *progress)
{
	int fd = open_stream(t);
	int root = open(root_dir, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
	struct tar_target target = { .root_fd = root, .id_base = 2148500000u, .chown_at = record_chown, .ctx = log };
	const char *error = extract_tar(fd, &target, progress);
	close(fd);
	close(root);
	return error;
}

static void extracts_files_dirs_and_links(void)
{
	struct tarball t = { 0 };
	struct chown_log log = { 0 };
	struct tar_progress progress = { 0 };
	reset_tree();
	tar_dir(&t, "./");
	tar_dir(&t, "./etc/");
	tar_file(&t, "./etc/os-release", "ID=debian\n");
	tar_entry(&t, "./usr/bin/tool", '0', 04755, "#!/bin/sh\n", NULL); /* parent folders made on the way */
	tar_symlink(&t, "./bin", "usr/bin");
	tar_entry(&t, "./etc/os-release.hard", '1', 0644, NULL, "./etc/os-release");
	tar_entry(&t, "./run/fifo", '6', 0600, NULL, NULL);
	tar_end(&t);
	CHECK(extract(&t, &log, &progress) == NULL);
	CHECK_STR(read_file("rootfs/etc/os-release"), "ID=debian\n");
	CHECK_STR(read_file("rootfs/bin/tool"), "#!/bin/sh\n");
	CHECK_STR(read_file("rootfs/etc/os-release.hard"), "ID=debian\n");
	char link[64] = { 0 };
	CHECK(readlink(path_in("rootfs/bin"), link, sizeof link - 1) == 7);
	CHECK_STR(link, "usr/bin");
	struct stat st;
	CHECK(!stat(path_in("rootfs/usr/bin/tool"), &st) && (st.st_mode & 07777) == 04755);
	CHECK(!lstat(path_in("rootfs/run/fifo"), &st) && S_ISFIFO(st.st_mode));
	CHECK(!stat(path_in("rootfs/etc/os-release"), &st) && st.st_nlink == 2);
	CHECK(progress.files == 7);
	CHECK(progress.bytes == t.len);
	CHECK(log.root_uid == 2148500000u);
	free(t.data);
}

static void shifts_owners_into_the_container_range(void)
{
	struct tarball t = { 0 };
	struct chown_log log = { 0 };
	reset_tree();
	tar_header(&t, "./home/user/", '5', 0755, 1000, 1000, 0, NULL);
	tar_end(&t);
	CHECK(extract(&t, &log, NULL) == NULL);
	CHECK_STR(log.last_name, "user");
	CHECK(log.last_uid == 2148500000u + 1000 && log.last_gid == 2148500000u + 1000);

	t.len = 0;
	tar_header(&t, "./x", '0', 0644, 70000, 5, 0, NULL); /* past the range: nobody */
	tar_end(&t);
	CHECK(extract(&t, &log, NULL) == NULL);
	CHECK(log.last_uid == 2148500000u + 65534 && log.last_gid == 2148500000u + 5);
	free(t.data);
}

static void reads_long_names_and_pax_records(void)
{
	struct tarball t = { 0 };
	struct chown_log log = { 0 };
	char longname[300];
	reset_tree();
	snprintf(longname, sizeof longname, "./%0150d/%0120d", 1, 2);
	tar_entry(&t, "././@LongLink", 'L', 0644, longname, NULL);
	tar_file(&t, "./truncated", "gnu");
	tar_pax(&t, "path", "./pax/name with spaces.txt");
	tar_file(&t, "./ignored", "pax");
	tar_pax(&t, "linkpath", "/usr/share/zoneinfo/America/Sao_Paulo");
	tar_symlink(&t, "./etc/localtime", "short");
	tar_end(&t);
	CHECK(extract(&t, &log, NULL) == NULL);
	char rel[400];
	snprintf(rel, sizeof rel, "rootfs/%s", longname + 2);
	CHECK_STR(read_file(rel), "gnu");
	CHECK_STR(read_file("rootfs/pax/name with spaces.txt"), "pax");
	CHECK(read_file("rootfs/ignored") == NULL);
	char link[128] = { 0 };
	CHECK(readlink(path_in("rootfs/etc/localtime"), link, sizeof link - 1) > 0);
	CHECK_STR(link, "/usr/share/zoneinfo/America/Sao_Paulo");
	free(t.data);
}

static void never_writes_outside_the_root(void)
{
	struct tarball t = { 0 };
	struct chown_log log = { 0 };
	reset_tree();
	CHECK(!mkdir(path_in("outside"), 0755));
	tar_symlink(&t, "./escape", "../outside"); /* inside the root that is <root>/outside: missing */
	tar_file(&t, "./escape/through-link", "no");
	tar_end(&t);
	CHECK(extract(&t, &log, NULL) != NULL);
	CHECK(read_file("outside/through-link") == NULL);
	t.len = 0;
	tar_symlink(&t, "./abs", "/");
	tar_file(&t, "./abs/through-abs", "yes");
	tar_end(&t);
	CHECK(extract(&t, &log, NULL) == NULL);
	CHECK(read_file("through-abs") == NULL);
	const char *inside = read_file("rootfs/through-abs");
	CHECK(inside && !strcmp(inside, "yes"));

	static const char *const BAD[] = { "../evil", "./a/../../evil", "/etc/../../evil" };
	for (size_t i = 0; i < sizeof BAD / sizeof *BAD; i++) {
		t.len = 0;
		tar_file(&t, BAD[i], "no");
		tar_end(&t);
		CHECK_STR(extract(&t, &log, NULL), "bad_archive");
	}
	CHECK(read_file("evil") == NULL);
	free(t.data);
}

static void a_symlink_over_a_file_replaces_it(void)
{
	struct tarball t = { 0 };
	struct chown_log log = { 0 };
	reset_tree();
	tar_file(&t, "./etc/resolv.conf", "old");
	tar_symlink(&t, "./etc/resolv.conf", "/run/resolv.conf");
	tar_file(&t, "./etc/hosts", "first");
	tar_file(&t, "./etc/hosts", "second");
	tar_end(&t);
	CHECK(extract(&t, &log, NULL) == NULL);
	char link[64] = { 0 };
	CHECK(readlink(path_in("rootfs/etc/resolv.conf"), link, sizeof link - 1) > 0);
	CHECK_STR(link, "/run/resolv.conf");
	CHECK_STR(read_file("rootfs/etc/hosts"), "second");
	free(t.data);
}

static void rejects_what_is_not_a_tar_stream(void)
{
	struct tarball t = { 0 };
	struct chown_log log = { 0 };
	reset_tree();
	tar_put(&t, "\xfd" "7zXZ\0 compressed, not decoded", 31);
	tar_pad(&t);
	CHECK_STR(extract(&t, &log, NULL), "bad_archive");
	t.len = 0;
	tar_file(&t, "./a", "0123456789");
	t.len -= 512; /* data cut short */
	CHECK_STR(extract(&t, &log, NULL), "bad_archive");
	t.len = 0;
	CHECK_STR(extract(&t, &log, NULL), "bad_archive"); /* empty */
	free(t.data);
}

/* The system tar's own formats, as the image builders write them. */
static void extracts_what_gnu_tar_writes(void)
{
	static const char *const FORMATS[] = { "gnu", "posix" };
	for (size_t i = 0; i < sizeof FORMATS / sizeof *FORMATS; i++) {
		struct chown_log log = { 0 };
		char cmd[4096];
		reset_tree();
		snprintf(cmd, sizeof cmd,
			 "cd %s && mkdir -p src/usr/lib/a-rather-long-directory-name-for-the-test/%0110d src/etc && "
			 "echo hi > src/usr/lib/a-rather-long-directory-name-for-the-test/%0110d/file.so && "
			 "ln -s usr/lib src/lib && ln src/etc/../usr/lib/a-rather-long-directory-name-for-the-test/%0110d/file.so "
			 "src/etc/hard && tar --format=%s -C src -cf in.tar .",
			 base_dir, 7, 7, 7, FORMATS[i]);
		CHECK(!system(cmd));
		int fd = open(path_in("in.tar"), O_RDONLY | O_CLOEXEC);
		int root = open(root_dir, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
		struct tar_target target = { .root_fd = root, .id_base = 2148500000u, .chown_at = record_chown, .ctx = &log };
		CHECK(extract_tar(fd, &target, NULL) == NULL);
		close(fd);
		close(root);
		CHECK_STR(read_file("rootfs/etc/hard"), "hi\n");
		char rel[256];
		snprintf(rel, sizeof rel, "rootfs/lib/a-rather-long-directory-name-for-the-test/%0110d/file.so", 7);
		const char *text = read_file(rel);
		CHECK(text && !strcmp(text, "hi\n"));
	}
}

int main(void)
{
	RUN(extracts_files_dirs_and_links);
	RUN(shifts_owners_into_the_container_range);
	RUN(reads_long_names_and_pax_records);
	RUN(never_writes_outside_the_root);
	RUN(a_symlink_over_a_file_replaces_it);
	RUN(rejects_what_is_not_a_tar_stream);
	RUN(extracts_what_gnu_tar_writes);
	char cmd[128];
	snprintf(cmd, sizeof cmd, "rm -rf %s", base_dir);
	if (system(cmd))
		return 1;
	return CHECK_DONE();
}
