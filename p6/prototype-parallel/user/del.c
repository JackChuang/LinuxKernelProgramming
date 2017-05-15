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
	char buffer[128];
	
	if(argc != 2)
		return EXIT_SUCCESS;

	buffer[0] = '\0';
	ret = kvlib_del(argv[1]);
	printf("%s:%s\n", argv[1],buffer);
	printf("ret: %d\n",ret);
	return EXIT_SUCCESS;
}
