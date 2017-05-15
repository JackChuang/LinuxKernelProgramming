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
	int ret;
	int i;
#if 0
	int	j;

	char buffer[128];
	buffer[0] = '\0';

	ret = 0;
    for(j = 0; j < ITER; j++) {
        /* Now let's fill an entire block lplus an additional page (we assume 
         * there are 64 pages per block) */
        for (i = 0; i < 1000; i++) 
		{
            char key[128], val[128];
            sprintf(key, "key%d", i);
            sprintf(val, "val%d", i);
            ret += kvlib_set(key, val);
			//kvlib_print();
        }
		printf("ITER :%d\n",j);
	}
    printf("write returns: %d (should be 0)\n", ret);

	for (i=0; i <1000; i++) 
	{  
		char key[128];
        sprintf(key, "key%d", i);
        ret += kvlib_get(key, buffer);
		//printf("%s:%s\n", key,buffer);
	}
    printf("read returns: %d (should be 0)\n", ret);
#endif
	//kvlib_format();

	for (i=0; i < 10000; i++)
	{
		char *key = "key100000";
		char *val = "val100000";
		ret += kvlib_set(key,val);
	}
	printf("set returns: %d (should be 0)\n", ret);

	//kvlib_print();

	return EXIT_SUCCESS;
}
