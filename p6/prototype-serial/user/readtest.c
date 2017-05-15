/**
 * Test program using the library to access the storage system
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* Library header */
#include "kvlib.h"

#define SIZE 640

int main(void)
{
	int ret;
	int i;

	char buffer[128];
	buffer[0] = '\0';

	ret = 0;

	for (i=0; i < SIZE; i++) 
	{  
		char key[128];
        sprintf(key, "key%d", i);
        ret += kvlib_get(key, buffer);
		if (ret != 0)	
			printf("*");
	
		printf("%s:%s\n", key,buffer);
	}
    printf("read returns: %d (should be 0)\n", ret);

	return EXIT_SUCCESS;
}
