#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define  BUF_LEN			    20
#define  LOTTERY_TASKS_NUM		10010

int g_go = 1;

struct lottery_tasks_config {
	int pid;
	unsigned long long tickets;
	double min_exec;
	int max_exec;
	double min_inter_arrival;
	double max_inter_arrival;
	double min_offset;
	double max_offset;
};

pid_t lottery_tasks_pid [LOTTERY_TASKS_NUM];
volatile int lottery_tasks_num=0;

void start_simulation()
{
	int i;
	printf("I will send a SIGUSR1 signal to start all tasks\n");
	for(i=0;i<lottery_tasks_num;i++){
		kill(lottery_tasks_pid[i],SIGUSR1);
	}

}

void end_simulation(int signal)
{
	int i;
	printf("I will send a SIGUSR2 signal to finish all tasks\n");
	for(i=0;i<lottery_tasks_num;i++){
		kill(lottery_tasks_pid[i],SIGUSR2);
	}

}
void goahead(int signal)
{    
    g_go = 1;
}
void help(char* name)
{
	fprintf(stderr, "Usage: %s file_name (system configuration)\n",	name);
	exit(0);
}

int main(int argc, char *argv[])
{

	int duration,i,j,k,n;
	char arg[LOTTERY_TASKS_NUM][BUF_LEN];
	char *parg[LOTTERY_TASKS_NUM];
    struct sched_param param;

    if(argc!=2)
        help(argv[0]);

	printf("parent pid %d\n",getpid());
    
    printf("%d\n", lottery_tasks_num);
    lottery_tasks_num = (int)atoi(argv[1]);
    printf("%d\n", lottery_tasks_num);
    param.sched_priority = 99;
    sched_setscheduler( 0, SCHED_FIFO, (struct sched_param *)&param );
	signal(SIGUSR2, goahead);

	for(i=0;i<lottery_tasks_num;i++){
		strcpy(arg[0],"lott");
		sprintf(arg[1],"%d",i);
        sprintf(arg[2],"%llu", 1);
		sprintf(arg[3],"%d", 1);
		sprintf(arg[4],"%d",getpid());
		
        
        //printf("i %f <  %d \n", (float)i, ((float)lottery_tasks_num*(float)(0.9)) );
        if( (float)i < (float)lottery_tasks_num*(float)(0.9)) {
            sprintf(arg[2],"%llu", 1);
            sprintf(arg[3],"%llu", 1);
        }
        else {
            sprintf(arg[2],"%llu", 99);
            sprintf(arg[3],"%llu", 100);
        }

		n=5;
		for(k=0;k<n;k++){
			parg[k]=arg[k];
		}
		parg[n]=NULL;

        lottery_tasks_pid[i]=fork();
		if(lottery_tasks_pid[i]==0){
			execv("./lottery_task", parg);
			perror("Error: execv\n");
			exit(0);
		}
        pause();        
	}

    sleep(1);
    start_simulation();  //time zero of the execution

    printf("Waiting for %d tasks done", lottery_tasks_num);
	for(i=0;i<lottery_tasks_num;i++){
		wait(NULL);
	}
	printf("All tasks have finished properly!!!\n");

	return 0;

}
