/**
 * Test program using the library to access the storage system
 */

#include <stdio.h>
#include <stdlib.h>
/* Library header */
#include "kvlib.h"

int main(int argc, char **argv)
{
	int ret;

	if(argc != 3)
	{
		printf("two parameters needed\n");
		return EXIT_SUCCESS;
	}

	/* first let's format the partition to make sure we operate on a 
	 * well-known state */
	
	/* "set" operation test */
	ret = kvlib_set(argv[1], argv[2]);
	printf("Insert (%s, %s):\n", argv[1], argv[2]);
	printf(" returns: %d (should be 0)\n", ret);
	printf("ret: %d\n", ret);
	return EXIT_SUCCESS;
}
