/**
 * Test program using the library to access the storage system
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* Library header */
#include "kvlib.h"

#define ITER 1
#define NUM_KEYS 1000

int main(void)
{
	int ret = 0;
	int i, j;

    printf("=====================\n");
    printf("=== FLUSH_get test ===\n");
    printf("======================\n");

    for(j = 0; j < ITER; j++) {
        ret = 0;
        for (i = 1; i <= NUM_KEYS; i++) {
            char key[128], val[128];
            sprintf(key, "key%d", i);
            sprintf(val, "val%d", i);
            ret += kvlib_set(key, val);
        }
        printf("Insert 1->1000:\n");
        printf(" returns: %d (should be 0)\n\n", ret);
    }
	return EXIT_SUCCESS;

}
