/**
 * Test program using the library to access the storage system
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* Library header */
#include "kvlib.h"

#define ITER 5

int main(void)
{
	int ret;
	int i, j;
	char buffer[128];
	buffer[0] = '\0';

	/* first let's format the partition to make sure we operate on a 
	 * well-known state */
	//ret = kvlib_format();
	//printf("Formatting done:\n");
	//printf(" returns: %d (should be 0)\n", ret);

	/* "set" operation test */
/*
	ret = kvlib_set("key1", "val1");
	printf("Insert 1 (key1, val1):\n");
	printf(" returns: %d (should be 0)\n", ret);

	ret = kvlib_set("key2", "val2");
	printf("Insert 2 (key2, val2):\n");
	printf(" returns: %d (should be 0)\n", ret);
*/
/*	
	ret = kvlib_set("key1", "val1");
	printf("Insert 3 (key1, val1):\n");
	printf(" returns: %d (should not be 0)\n", ret);
*/
	ret = 0;
    for(j = 0; j < ITER; j++) {
        /* Now let's fill an entire block lplus an additional page (we assume 
         * there are 64 pages per block) */
        for (i = 1; i < 65; i++) {
            char key[128], val[128];
            sprintf(key, "key%d", i);
            sprintf(val, "val%d", i);
            ret += kvlib_set(key, val);
        }
	}
	//printf("Insert 3->100:\n");
    printf("write returns: %d (should be 0)\n", ret);

		for (i=1; i <65; i++) {  
			char key[128], val[128];
            sprintf(key, "key%d", i);
            sprintf(val, "val%d", i);
            ret = kvlib_get(key, buffer);
            printf("Reading the value of key:\n");
            printf("returns: %d (should be 0), read: %s (should be %s)\n", ret, buffer, val);
            //ret += kvlib_set(key, val);
			//printf("%s:%s\n", key,val);
        }
        // dbg
	    //kvlib_print();
        // GC
        //kvlib_gc();
        // dbg
	    //kvlib_print();
        //sleep(1);
        // delele in order to tirgger GC
        /*
        ret = 0;
        for (i = 3; i < 100; i++) {
            char key[128];
            sprintf(key, "key%d", i);
            ret += kvlib_del(key);
        }
        printf("Delete key3~key100:\n");
        printf(" returns: %d (should be 0)\n", ret);
        */

#if 0
	/* "get" operation test */
	ret = kvlib_get("key1", buffer);
	printf("Reading the value of key1:\n");
	printf(" returns: %d (should be 0), read: %s (should be val1)\n", ret,
		buffer);

	ret = kvlib_get("key35", buffer);
	printf("Reading the value of key35:\n");
	printf(" returns: %d (should be 0), read: %s (should be val35)\n", ret,
		buffer);

	/* trying to set a value for an already existing key */
	ret = kvlib_set("key1", "crazyval");
	printf("Trying to insert an already existing key:\n");
	printf(" returns: %d (should be -4)\n", ret);

	/* trying to get the value of a non-existing key */
	ret = kvlib_get("key2000", buffer);
	printf("Trying to get the value of a non-existing key:\n");
	printf(" returns: %d (should be -3)\n", ret);
	ret = kvlib_print();

	/* Let's format again before we fill all the flash */
	ret = kvlib_format();
	printf("Formatting:\n");
	printf(" returns: %d (should be 0)\n", ret);

	/* Fill the flash completely, note that we assume here a small partition
	 * of 10 blocks, each containing 64 pages */
	printf ("Insertion 0->639 (flash should be full after that)"
		", may take some time...\n");
	fflush(stdout);
	ret = 0;
	for (i = 0; i < 639; i++) {
		char key[128], val[128];
		sprintf(key, "key%d", i);
		sprintf(val, "val%d", i);
		ret += kvlib_set(key, val);
	}
	printf(" returns: %d (should be 0)\n", ret);

	/* The flash is full and the system should be read-only, let's try to
	 * add an additional key/value: */
	ret = kvlib_set("key640", "val640");
	printf("Trying to insert another key/val:\n");
	printf(" returns: %d (should be -5)\n", ret);
	ret = kvlib_print();
	
	/* Format again */
	ret = kvlib_format();
	printf("Formatting:\n");
	printf(" returns: %d (should be 0)\n", ret);

	/* Let's try to add a key/value again: */
	ret = kvlib_set("key640", "val640");
	printf("Insert a key/val after formatting:\n");
	printf(" returns: %d (should be 0)\n", ret);

	/* get the value we just inserted */
	ret = kvlib_get("key640", buffer);
	printf("Reading the val of key640:\n");
	printf(" returns: %d, read: %s (should be val640)\n", ret, buffer);


	ret = kvlib_print();
#endif
	
	//ret = kvlib_format();
	//ret = kvlib_print();


	return EXIT_SUCCESS;

}
