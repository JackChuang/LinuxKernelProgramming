#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h> /* For SYS_xxx definitions */

/*
 * 323 common  my_syscall1     sys_my_syscall1 
 * 324 common  my_syscall2     sys_my_syscall2
 */

int main(void)
{
    int ret;
	char message[] ="hello, world!";
    char message2[128], message3[129];
    memset(message2, 'o', sizeof(message2)-1);
    message2[127]='\0';
    memset(message3, 'o', sizeof(message3)-1);
    message3[128]='\0';


    ret = syscall(323, 100, 3);
	printf("syscall1: ret %d\n\n", ret);

	printf("syscall2:\n", ret);
    printf("before: %s\n", message);
    ret = syscall(324, message);
    printf("after:  %s ret %d\n\n", message, ret);
    
    printf("before: %s\n", message2);
    ret = syscall(324, message2);
    printf("after:  %s ret %d\n\n", message2, ret);

    printf("before: %s\n", message3);
    ret = syscall(324, message3);
    printf("after:  %s ret %d\n\n", message3, ret);
    return 0;
}
