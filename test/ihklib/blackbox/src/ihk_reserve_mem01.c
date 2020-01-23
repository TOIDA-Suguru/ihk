#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "init_fini.h"

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	const char param[] = "/dev/mcd0";
	const char *values[] = {
		 "before insmod",
		 "after insmod",
		};

	struct mems mems_input[2] = { 0 };
	for (i = 0; i < 2; i++) {
		int excess;

		ret = mems_ls(&mems_input[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}
	}

	int ret_expected[] = { -ENOENT, 0 };

	struct mems mems_margin[2] = { 0 };

	for (i = 0; i < 2; i++) {
		int excess;

		ret = mems_ls(&mems_margin[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_margin[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_margin[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}

		mems_fill(&mems_margin[i], 4UL << 20);
	}

	struct mems *mems_expected[] = { NULL, &mems_input[1] };

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_reserved(mems_expected[i],
						  &mems_margin[i]);
			OKNG(ret == 0, "reserved as expected\n");

			/* Clean up */
			ret = mems_query_and_release();
			INTERR(ret != 0, "mems_query_and_release returned %d\n", ret);
		}

		/* Precondition */
		if (i == 0) {
			ret = insmod(params.uid, params.gid);
			NG(ret == 0, "insmod returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	rmmod(0);
	return ret;
}
