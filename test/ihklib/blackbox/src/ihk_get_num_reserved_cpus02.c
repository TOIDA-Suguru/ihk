#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "input_vector.h"
#include "params.h"
#include "init_fini.h"
#include "check.h"

int main(int argc, char **argv)
{
	int ret;
	int i;
	
	params_getopt(argc, argv);

	/* Prepare one with NULL and zero-clear others */

	const char *messages[] =
		{
		 "0",
		 "1",
		 "all",
		};
	
	struct cpus cpu_inputs[] = 
		{
		 {
		  .cpus = NULL,
		  .ncpus = 0,
		 },
		 { 0 },
		 { 0 },
		};

	ret = cpus_push(&cpu_inputs[1], 12);
	INTERR(ret, "cpus_push returned %d\n", ret);

	cpus_dump(&cpu_inputs[1]);

	/* All of McKernel CPUs */
	for (i = 2; i < 3; i++) { 
		ret = cpus_ls(&cpu_inputs[i]);
		INTERR(ret, "cpus_ls returned %d\n", ret);

		/* Spare two cpus for Linux */
		ret = cpus_shift(&cpu_inputs[i], 2);
		INTERR(ret, "cpus_shift returned %d\n", ret);
	}
	
	int ret_expected_reserve_cpu[] = { -EINVAL, 0, 0 };

	int ret_expected[] = { cpu_inputs[0].ncpus,
			       cpu_inputs[1].ncpus,
			       cpu_inputs[2].ncpus };
	struct cpus *cpus_expected[] = 
		{
		  NULL, /* don't care */
		  &cpu_inputs[1],
		  &cpu_inputs[2],
		};

	/* Precondition */
	ret = insmod(params.uid, params.gid);
	INTERR(ret != 0, "insmod returned %d\n", ret);
	
	/* Activate and check */
	for (i = 0; i < 3; i++) {
		INFO("test-case: cpus: %s\n", messages[i]);
		
		ret = ihk_reserve_cpu(0, cpu_inputs[i].cpus, cpu_inputs[i].ncpus);
		INTERR(ret != ret_expected_reserve_cpu[i],
		     "ihk_reserve_cpu returned %d\n", ret);

		ret = ihk_get_num_reserved_cpus(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = check_reserved_cpu(cpus_expected[i]);
			OKNG(ret == 0, "reserved as expected\n");
			
			/* Clean up */
			ret = ihk_release_cpu(0, cpu_inputs[i].cpus,
					      cpu_inputs[i].ncpus);
			INTERR(ret != 0, "ihk_release_cpu returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	rmmod(0);
	return ret;
}
