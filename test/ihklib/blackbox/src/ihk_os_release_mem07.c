#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "os status";
const char *values[] = {
	"before boot",
	"after boot",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	struct mems mems_input[2] = { 0 };
	struct mems mems_after_release[2] = { 0 };
	/* All */
	for (i = 0; i < 2; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		ret = mems_reserved(&mems_after_release[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	ret = mems_shift(&mems_after_release[0],
			 mems_after_release[0].num_mem_chunks);
	INTERR(ret, "mems_shift returned %d\n", ret);

	int ret_expected[] = {
		0,
		-EBUSY,
	};

	struct mems *mems_expected[] = {
		&mems_after_release[0],
		&mems_after_release[1],
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		if (i == 1) {
			ret = os_load();
			INTERR(ret, "os_load returned %d\n", ret);

			ret = os_kargs();
			INTERR(ret, "os_kargs returned %d\n", ret);

			ret = ihk_os_boot(0);
			INTERR(ret, "ihk_os_boot returned %d\n", ret);
		}

		ret = ihk_os_release_mem(0, mems_input[i].mem_chunks,
				mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {

			ret = mems_check_assigned(mems_expected[i]);
			OKNG(ret == 0, "released as expected\n");

			/* Clean up */
			if (i == 1) {
				ret = ihk_os_shutdown(0);
				INTERR(ret, "ihk_os_boot returned %d\n", ret);
			}

			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);

			ret = cpus_os_release();
			INTERR(ret, "cpus_os_release returned %d\n",
			       ret);

			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	cpus_release();
	mems_release();

	linux_rmmod(0);
	return ret;
}
