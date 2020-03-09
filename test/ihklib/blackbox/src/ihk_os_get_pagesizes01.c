#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

const char param[] = "existence of OS instance";
const char *values[] = {
	"without OS instance",
	"with OS instance",
};

int pagesize_cmp(long *acquired, long *expected, int num_entries)
{
	int i, ret = 0;

	if (!acquired || !expected) {
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < num_entries; i++) {
		printf("acquired: %ld expected: %ld\n",
				acquired[i], expected[i]);
		if (acquired[i] != expected[i]) {
			ret = 1;
			goto out;
		}
	}

	ret = 0;
out:
	return ret;
}

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	long pagesizes_input[2][IHK_MAX_NUM_PGSIZES] = { 0 };
	int ret_expected[2] = {
		-ENOENT,
		0,
	};

	int ret_expected_compare[2] = {
		1,
		0,
	};

	long expected_pagesizes[IHK_MAX_NUM_PGSIZES] = {
		1UL << 12,
		1UL << 16,
		1UL << 21,
		1UL << 25,
		1UL << 30,
		1UL << 34,
		1UL << 29,
		1UL << 42,
	};

	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		/* Precondition */
		if (i == 1) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);

			ret = cpus_os_assign();
			INTERR(ret, "cpus_os_assign returned %d\n", ret);

			ret = mems_os_assign();
			INTERR(ret, "mems_os_assign returned %d\n", ret);

			ret = os_load();
			INTERR(ret, "os_load returned %d\n", ret);

			ret = os_kargs();
			INTERR(ret, "os_kargs returned %d\n", ret);

			ret = ihk_os_boot(0);
			INTERR(ret, "ihk_os_boot returned %d\n", ret);
		}

		ret = ihk_os_get_pagesizes(0, pagesizes_input[i],
				IHK_MAX_NUM_PGSIZES);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = pagesize_cmp(pagesizes_input[i], expected_pagesizes,
				IHK_MAX_NUM_PGSIZES);
		OKNG(ret == ret_expected_compare[i],
				"get pagesizes as expected\n");

		if (i == 1) {
			ret = ihk_os_shutdown(0);
			INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);

			ret = cpus_os_release();
			INTERR(ret, "cpus_os_release returned %d\n", ret);

			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);
		}

	}

out:
	if (ihk_get_num_os_instances(0)) {
		ihk_os_shutdown(0);
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	mems_release();
	cpus_release();
	linux_rmmod(0);

	return ret;
}

