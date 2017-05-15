/**
 * Test program using the library to access the storage system
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* Library header */
#include "kvlib.h"

#define ITER 10

int main(void)
{
	int ret = 0;
	int i;

    printf("======================\n");
    printf("=== UPDATE&GC test ===\n");
    printf("======================\n");
	for (i=0; i < 10000; i++)
	{
		char *key = "key100000";
		char *val = "val100000";
		ret += kvlib_set(key,val);
	}
	printf("set returns: %d (should be 0)\n\n", ret);

	return EXIT_SUCCESS;
}
