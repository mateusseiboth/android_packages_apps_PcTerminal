/* Who may connect, which Android user a request acts for, names and the container's host ids. */
#include "check.h"
#include "ids.h"

static void trusts_root_and_the_system_app_of_any_user(void)
{
	CHECK(is_trusted_uid(0));
	CHECK(is_trusted_uid(1000));
	CHECK(is_trusted_uid(1001000)); /* the system app of user 10 */
	CHECK(!is_trusted_uid(2000));    /* shell */
	CHECK(!is_trusted_uid(1010123)); /* an app of user 10 */
	CHECK(!is_trusted_uid(10123));   /* an app of user 0 */
}

static void the_android_user_comes_from_the_uid(void)
{
	CHECK(find_android_user(0) == 0);
	CHECK(find_android_user(1001000) == 10);
	CHECK(find_android_user(1101000) == 11);
}

/* Container ids sit past every uid Android can give out (users stop at 21474), 100000 per user so a
 * container uid keeps its value as the app id netd's eBPF looks at. */
static void container_ids_never_meet_android_uids(void)
{
	CHECK(find_host_id_base(0) == 2147500000u);
	CHECK(find_host_id_base(10) == 2148500000u);
	CHECK(find_host_id_base(10) % 100000 == 0);
	CHECK(find_host_id_base(10) > 21474u * 100000u + 99999u);
	CHECK(find_host_id_base(MAX_ANDROID_USER) + CONTAINER_ID_COUNT - 1 < 4294967295u);
	CHECK(CONTAINER_ID_COUNT == 65536);
}

static void container_names(void)
{
	CHECK(is_valid_name("debian"));
	CHECK(is_valid_name("ubuntu-24"));
	CHECK(is_valid_name("0"));
	CHECK(is_valid_name("abcdefghijklmnopqrstuvwxyz01234")); /* 31 */
	CHECK(!is_valid_name("abcdefghijklmnopqrstuvwxyz012345"));
	CHECK(!is_valid_name(""));
	CHECK(!is_valid_name("-x"));
	CHECK(!is_valid_name("Debian"));
	CHECK(!is_valid_name("a/b"));
	CHECK(!is_valid_name(".."));
	CHECK(!is_valid_name("a.b"));
	CHECK(!is_valid_name(NULL));
}

static void user_names(void)
{
	CHECK(is_valid_user_name("user"));
	CHECK(is_valid_user_name("_apt"));
	CHECK(is_valid_user_name("mateus-s_2"));
	CHECK(!is_valid_user_name("root"));
	CHECK(!is_valid_user_name("2x"));
	CHECK(!is_valid_user_name("Mateus"));
	CHECK(!is_valid_user_name("a:b"));
	CHECK(!is_valid_user_name(""));
	CHECK(!is_valid_user_name("abcdefghijklmnopqrstuvwxyz0123456"));
	CHECK(!is_valid_user_name(NULL));
}

int main(void)
{
	RUN(trusts_root_and_the_system_app_of_any_user);
	RUN(the_android_user_comes_from_the_uid);
	RUN(container_ids_never_meet_android_uids);
	RUN(container_names);
	RUN(user_names);
	return CHECK_DONE();
}
