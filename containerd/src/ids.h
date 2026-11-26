/*
 * Peers, Android users, names and the host ids of a container (PROTOCOL.md, "Names" and "Container
 * layout"; README, "User ids").
 */
#pragma once

#define AID_SYSTEM 1000
#define AID_USER_OFFSET 100000u
/* UserHandle.MAX_SECONDARY_USER_ID: Integer.MAX_VALUE / PER_USER_RANGE. */
#define MAX_ANDROID_USER 21474u
/* The first container host id, a multiple of AID_USER_OFFSET below 2^31: netd routes only uids
 * 0..2147483647 (its ip rules' uidrange), so higher uids have no network. Android users reach it only from
 * user 20000 on. */
#define HOST_ID_START 2000000000u
/* The last Android user whose container range still ends below 2^31. */
#define MAX_CONTAINER_USER ((2147483647u - HOST_ID_START - CONTAINER_ID_COUNT + 1u) / AID_USER_OFFSET)
#define CONTAINER_ID_COUNT 65536u
/* The person's account inside every container. */
#define CONTAINER_USER_ID 1000u

/* Root, or the system app (android.uid.system) of any user: HSUM runs it as user 10 (uid 1001000). */
int is_trusted_uid(unsigned uid);
unsigned find_android_user(unsigned uid);
/* Host uid and gid of a container's id 0 for this Android user. */
unsigned find_host_id_base(unsigned user_id);
/* [a-z0-9][a-z0-9-]{0,30} */
int is_valid_name(const char *name);
/* [a-z_][a-z0-9_-]{0,31}, never "root" */
int is_valid_user_name(const char *name);
