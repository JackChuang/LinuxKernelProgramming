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

	ret = kvlib_format();
	printf("format done.\nret: %d\n", ret);
	return EXIT_SUCCESS;
}
