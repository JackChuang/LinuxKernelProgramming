/**
 * Test program using the library to access the storage system
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
/* Library header */
#include "kvlib.h"

int main(int argc, char *argv[])
{
      
	int RW; // read write times 
	int ret, i;
	FILE *fp;
	char *write_file = ("write_vanilla.csv");
	char *read_file = ("read_vanilla.csv");
	struct timespec
						start = {0,0},
						stop = {0,0};
        if(argc == 2){
		RW = atoi(argv[1]) -1;
	}
	else{
		RW = 640-1;
	}
	/* first let's format the partition to make sure we operate on a
	 * well-known state */
	ret = kvlib_format();
	printf("Formatting done:\n");
	printf(" returns: %d (should be 0)\n", ret);

	/* writing key val couple*/
	printf ("Insertion 0->%d (flash should be full after that)"
		", may take some time...\n",RW);
	fflush(stdout);
	//ret = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < RW; i++) {
		char key[128], val[128];
		sprintf(key, "key%d", i);
		sprintf(val, "val%d", i);
		ret += kvlib_set(key, val);
	}
	clock_gettime(CLOCK_MONOTONIC, &stop);
	printf("start time: [%li]s [%li]ns\n", start.tv_sec, start.tv_nsec);
	printf("stop time: [%li]s [%li]ns\n", stop.tv_sec, stop.tv_nsec);
	printf("writing takes [%.5f]ms\n", (stop.tv_sec*1.0e9+stop.tv_nsec - start.tv_sec*1.0e9+start.tv_nsec)/(1.0e6*(RW+1)));
	printf("returns: %d (should be 0)\n", ret);
        printf("writing to [%s]\n", write_file);
        fp = fopen(write_file, "a+");
        fprintf(fp, "[%4d],write_time(ms),%.5f\n",RW+1,(stop.tv_sec*1.0e9+stop.tv_nsec - start.tv_sec*1.0e9+start.tv_nsec)/(1.0e6*(RW+1)));
        fclose(fp);


	/* reading key val couple*/
 
	printf ("reading key 0->%d, may take some time...\n",RW);
	//ret = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < RW; i++) {
		char key[128], val[128];
		sprintf(key, "key%d", i);
		ret += kvlib_get(key, val);
	}
	clock_gettime(CLOCK_MONOTONIC, &stop);
	printf("returns: %d (should be 0)\n", ret);
	
    printf("start time: [%li]s [%li]ns\n", start.tv_sec, start.tv_nsec);
	printf("stop time: [%li]s [%li]ns\n", stop.tv_sec, stop.tv_nsec);
	printf("reading takes [%.5f]ms\n", (stop.tv_sec*1.0e9+stop.tv_nsec - start.tv_sec*1.0e9+start.tv_nsec)/(1.0e6*(RW+1)));
        printf("writing to [%s]\n", read_file);
        fp = fopen(read_file, "a+");
        fprintf(fp, "[%4d],read_time(ms),%.5f\n",RW+1,(stop.tv_sec*1.0e9+stop.tv_nsec - start.tv_sec*1.0e9+start.tv_nsec)/(1.0e6*(RW+1)));
        fclose(fp);
	/* Format again */
	ret = kvlib_format();

	return EXIT_SUCCESS;

}
