#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "init_fini.h"

const char param[] = "cpus";
const char *messages[] = {
	 "NULL",
	 "assigned",
	 "assigned + 1",
	 "assigned - 1",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = insmod(params.uid, params.gid);
	INTERR(ret, "insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	struct cpus cpus_input[4] = {
		{ .ncpus = 1, .cpus = NULL },
		{ 0 },
		{ 0 },
		{ 0 },
	};

	for (i = 1; i < 4; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	/* Plus one */
	ret = cpus_push(&cpus_input[2],
			cpus_max_id(&cpus_input[2]) + 1);
	INTERR(ret, "cpus_push returned %d\n", ret);

	/* Minus one */
	ret = cpus_pop(&cpus_input[3], 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	int ret_expected[] = {
		  -EFAULT,
		  0,
		  -EINVAL,
		  0,
		};

	struct cpus cpus_after_release[4] = { 0 };

	/* all */
	for (i = 0; i < 4; i++) {
		ret = cpus_reserved(&cpus_after_release[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	/* Empty */
	ret = cpus_shift(&cpus_after_release[1],
			 cpus_after_release[1].ncpus);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	/* Last one */
	ret = cpus_shift(&cpus_after_release[3],
			 cpus_after_release[3].ncpus - 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	struct cpus *cpus_expected[] = {
		  &cpus_after_release[0],
		  &cpus_after_release[1],
		  &cpus_after_release[2],
		  &cpus_after_release[3],
	};

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: : %s\n", messages[i]);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = ihk_os_release_cpu(0, cpus_input[i].cpus,
				      cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = cpus_check_assigned(cpus_expected[i]);
		OKNG(ret == 0, "released as expected\n");

		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release returned %d\n", ret);
	}

	ret = 0;
 out:
	rmmod(0);
	return ret;
}
