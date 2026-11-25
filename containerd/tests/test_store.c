/* Containers on disk: /data/linux/<userId>/<name>/{rootfs,config.json}, one folder per Android user. */
#include <stdlib.h>
#include <sys/stat.h>

#include "check.h"
#include "store.h"

static char data_root[64];

static void make_data_root(void)
{
	snprintf(data_root, sizeof data_root, "/tmp/pccstore-XXXXXX");
	CHECK(mkdtemp(data_root) != NULL);
}

static struct container sample(const char *name)
{
	struct container c = { .created = 1764072000, .id_base = 2148500000u };
	snprintf(c.name, sizeof c.name, "%s", name);
	snprintf(c.distro, sizeof c.distro, "debian");
	snprintf(c.release, sizeof c.release, "trixie");
	snprintf(c.user, sizeof c.user, "mateus");
	return c;
}

static void config_round_trip(void)
{
	char path[256], text[1024];
	struct container c = sample("debian"), back = { 0 };
	make_data_root();
	snprintf(path, sizeof path, "%s/config.json", data_root);
	CHECK(!save_container_config(path, &c));
	FILE *f = fopen(path, "r");
	size_t n = fread(text, 1, sizeof text - 1, f);
	fclose(f);
	text[n] = 0;
	CHECK_STR(text, "{\"version\":1,\"distro\":\"debian\",\"release\":\"trixie\",\"created\":1764072000,\"user\":\"mateus\","
			"\"uid_map\":{\"inside\":0,\"outside\":2148500000,\"count\":65536}}\n");
	CHECK(!read_container_config(path, &back));
	CHECK_STR(back.distro, "debian");
	CHECK_STR(back.release, "trixie");
	CHECK_STR(back.user, "mateus");
	CHECK(back.created == 1764072000 && back.id_base == 2148500000u);
}

static void a_config_without_a_valid_user_is_rejected(void)
{
	char path[256];
	struct container c = { 0 };
	snprintf(path, sizeof path, "%s/bad.json", data_root);
	FILE *f = fopen(path, "w");
	fputs("{\"version\":1,\"distro\":\"x\",\"release\":\"y\",\"created\":1,\"user\":\"Bad:User\"}\n", f);
	fclose(f);
	CHECK(read_container_config(path, &c));
}

static void lists_the_containers_of_one_user_sorted(void)
{
	struct store st = { data_root };
	struct container list[8];
	struct container b = sample("zeta"), a = sample("alpha"), other = sample("other");
	CHECK(list_containers(&st, 10, list, 8) == 0);
	CHECK(!add_container_dirs(&st, 10, "zeta") && !save_container(&st, 10, &b));
	CHECK(!add_container_dirs(&st, 10, "alpha") && !save_container(&st, 10, &a));
	CHECK(!add_container_dirs(&st, 11, "other") && !save_container(&st, 11, &other));
	CHECK(add_container_dirs(&st, 10, "alpha")); /* already there */
	char path[256];
	snprintf(path, sizeof path, "%s/10/half-made", data_root); /* a folder without config.json is not listed */
	CHECK(!mkdir(path, 0700));
	CHECK(list_containers(&st, 10, list, 8) == 2);
	CHECK_STR(list[0].name, "alpha");
	CHECK_STR(list[1].name, "zeta");
	struct container found;
	CHECK(!find_container(&st, 11, "other", &found) && !strcmp(found.name, "other"));
	CHECK(find_container(&st, 10, "other", &found));
	CHECK(find_container(&st, 10, "../11/other", &found));
	build_container_path(&st, 10, "alpha", "rootfs", path, sizeof path);
	char expected[256];
	snprintf(expected, sizeof expected, "%s/10/alpha/rootfs", data_root);
	CHECK_STR(path, expected);
}

static void renames_and_removes(void)
{
	struct store st = { data_root };
	struct container found;
	CHECK(!rename_container(&st, 10, "alpha", "beta"));
	CHECK(find_container(&st, 10, "alpha", &found));
	CHECK(!find_container(&st, 10, "beta", &found));
	CHECK(rename_container(&st, 10, "beta", "zeta")); /* taken */
	char cmd[512];
	snprintf(cmd, sizeof cmd, "mkdir -p %s/10/beta/rootfs/usr/bin && ln -s / %s/10/beta/rootfs/up && chmod 000 %s/10/beta/rootfs/usr",
		 data_root, data_root, data_root);
	CHECK(!system(cmd));
	CHECK(!remove_container(&st, 10, "beta"));
	CHECK(find_container(&st, 10, "beta", &found));
	struct stat s;
	CHECK(!stat("/tmp", &s)); /* the link to / was removed, not followed */
	snprintf(cmd, sizeof cmd, "rm -rf %s", data_root);
	CHECK(!system(cmd));
}

int main(void)
{
	RUN(config_round_trip);
	RUN(a_config_without_a_valid_user_is_rejected);
	RUN(lists_the_containers_of_one_user_sorted);
	RUN(renames_and_removes);
	return CHECK_DONE();
}
