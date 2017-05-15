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
    char buffer[128];
    buffer[0] = '\0';

    printf("======================\n");
    printf("=== FLUSH_set test ===\n");
    printf("======================\n");

    for(j = 0; j < ITER; j++) {
        for (i=1; i <= NUM_KEYS; i++) {  
            char key[128];//, val[128];
            sprintf(key, "key%d", i);
            //sprintf(val, "val%d", i);
            ret += kvlib_get(key, buffer);
        }
        printf(" returns: %d (should be 0)\n", ret);

        printf("=======================\n");
        if(!ret)
            printf("======== PASS =========\n");
        else
            printf("======== FAIL ========\n");
        printf("=======================\n\n");
    }
	return EXIT_SUCCESS;

}
