/*
 * p4.c
 * Copyright (C) 2017 user <user@debian>
 *
 * Distributed under terms of the MIT license.
 */

//#include "p4.h"

#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

#define WORKLOAD_CYCLE 100*1000*1000
#define NUM_OF_PTR 1024
#define WRITE_SIZE 4096
char* p[NUM_OF_PTR];
FILE* fp[NUM_OF_PTR];

void do_work(int s)
{
    int i, j;
    bool isfull = true;
    
    for(i=0; i<5; i++)
        printf("I'm pid %d, creating mem and files\n", getpid());
    printf("\n\n\n");
    
    // find a slot to save the ptr
    for( i=0; i<NUM_OF_PTR; i++ ) {
        if(p[i]) 
            continue;
        else {
            isfull = false;
            break;
        }
    }

    // if found 
    if(!isfull) {
        printf("write to mem and file slot %d\n", i);
        // add mem
        p[i] = malloc(sizeof(char)*WRITE_SIZE);
        if(!p[i]) {
            printf("CANNOT MALLOC!!!!!!!!!!!!!!\n");
            return;
        }

        //w
        memset(p[i], 'A', sizeof(char)*WRITE_SIZE); 

        //r
        char *ptr = p[i];
        for(j=0; j<100; j++)
            printf("%c ", *ptr++);
        printf("\n");

        // add file
        fp[i]=fopen("/dev/null", "r");
        if(!fp[i]) {
            printf("CANNOT OPEN FILE!!!!!!!!!!!!\n");
            goto fail1;
        }
    }
    else {
        printf("out of sapce for saving ptr\n");
    }
    return;

fail1:
    free(p[i]);
    return;
}

void do_work2(int s)
{
    int i;
    for(i=0; i<5; i++)
        printf("I'm pid %d, SIGUSR2 does exit()\n", getpid());
    
    for( i=0; i<NUM_OF_PTR; i++ ) {
        if(p[i]) {
            printf("freeing slot %d\n", i);
            free(p[i]);
            fclose(fp[i]);
        }
        else
            break;
    }

    printf("\n\n\n");
    exit(0);
}

void main(int argc, char** argv) 
{
    int i;
    for(i=0; i<4; i++)
        fork();
    
    printf("My ppid %d pid %d\n", getppid(), getpid());
    signal(SIGUSR1, do_work);
    signal(SIGUSR2, do_work2);
    while(1) {
        for(i=0; i<WORKLOAD_CYCLE; i++)
            ;
        sleep(1);
    }
}
