#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "mod.h"

const char param[] = "/dev/mcd0";
const char *values[] = {
	"before insmod",
	"after insmod",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);
	/* Precondition */
	ret = insmod(params.uid, params.gid);
	INTERR(ret == 0, "insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	/* All of McKernel CPUs */
	struct cpus cpus_input[2] = { 0 };

	for (i = 1; i < 2; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	int ret_expected_reserve_cpu[] = {
		-ENOENT,
		0
	};
	int ret_expected[] = {
		-ENOENT,
		cpus_input[1].ncpus
	};
	struct cpus *cpus_expected[] = {
		NULL,
		&cpus_input[1]
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_cpu(0, cpus_input[i].cpus,
				      cpus_input[i].ncpus);
		INTERR(ret != ret_expected_reserve_cpu[i],
		       "ihk_reserve_cpu returned %d\n", ret);

		ret = ihk_get_num_reserved_cpus(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_check_reserved(cpus_expected[i]);
			OKNG(ret == 0, "reserved as expected\n");

			/* Clean up */
			ret = ihk_release_cpu(0, cpus_input[i].cpus,
					      cpus_input[i].ncpus);
			INTERR(ret, "ihk_release_cpu returned %d\n", ret);
		}


	}

	ret = 0;
 out:
	rmmod(0);
	return ret;
}
