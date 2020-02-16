#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "mod.h"

const char param[] = "existance of OS instance";
const char *messages[] = {
	"wihout OS instance",
	"with OS instance",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	int ret_expected[] = { -ENOENT, 0 };

	/* Precondition */
	ret = insmod(params.uid, params.gid);
	INTERR(ret, "insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, messages[i]);

		if (ihk_get_num_os_instances(0)) {
			/* wait until os status changes to running */
			ret = os_wait_for_status(IHK_STATUS_RUNNING);
			INTERR(ret, "os status didn't change to %d\n",
			       IHK_STATUS_RUNNING);
		}

		INFO("trying to shutdown os\n");
		ret = ihk_os_shutdown(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (ihk_get_num_os_instances(0)) {
			/* wait until os status changes to inactive */
			os_wait_for_status(IHK_STATUS_INACTIVE);
			ret = ihk_os_get_status(0);
			OKNG(ret == IHK_STATUS_INACTIVE,
			     "status: %d, expected: %d\n",
			     ret, IHK_STATUS_INACTIVE);
		}

		/* Clean up */
		if (ihk_get_num_os_instances(0)) {
			ret = cpus_os_release();
			INTERR(ret, "cpus_os_assign returned %d\n", ret);

			ret = mems_os_release();
			INTERR(ret, "mems_os_assign returned %d\n", ret);

			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);
		}

		/* Precondition */
		if (i == 0) {
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
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0)) {
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	rmmod(1);

	return ret;
}
