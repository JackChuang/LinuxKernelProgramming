CC=/usr/bin/gcc

all: lottery_system lottery_task lottery_system_fair lottery_task_fair

lottery_system: lottery_system.c 
	${CC} -ggdb -lrt -lm lottery_system.c -o lottery_system

lottery_task: lottery_task.c 
	${CC} -ggdb -lrt -lm lottery_task.c -o lottery_task

lottery_system_fair: lottery_system_fair.c 
	${CC} -ggdb -lrt -lm lottery_system_fair.c -o lottery_system_fair

lottery_task_fair: lottery_task_fair.c 
	${CC} -ggdb -lrt -lm lottery_task_fair.c -o lottery_task_fair

clean:
	rm -f *.o *.a *~ lottery_task  lottery_system lottery_task_fair lottery_system_fair
