/**
 * Test program using the library to access the storage system
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* Library header */
#include "kvlib.h"

#define ITER 10000

int main(void)
{
	int ret;
	int i, j;
    for(j = 0; j < ITER; j++) {
        /* Now let's fill an entire block lplus an additional page (we assume 
         * there are 64 pages per block) */
        ret = 0;
        for (i = 1; i < 20; i++) {
            char key[128], val[128];
            sprintf(key, "key%d", i);
            sprintf(val, "val%d", i);
            ret += kvlib_set(key, val);
        }
        //printf("Insert 1->20:\n");
    }
    printf(" returns: %d (should be 0)\n", ret);

	return EXIT_SUCCESS;

}
