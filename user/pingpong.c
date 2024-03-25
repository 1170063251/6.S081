#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    //pipe管道中 0是读 1是写
    int p1[2];//父-子 
    int p2[2];//子-父
    pipe(p1);
    pipe(p2);
    int pid =fork();
    if(pid==0)//子进程程序
    {
        char buf_son[8];
        char pong[]="pong";
        close (p1[1]);
        read(p1[0],buf_son,sizeof(buf_son));
        close(p1[0]);

        printf("%d:received %s",getpid(),buf_son);
        close(p2[0]);
        write(p2[1],pong,sizeof(pong));
        close(p2[1]);
        exit(0);
    }
    else
    {
        char buf_father[8];
        close(p1[0]);
        char ping[]="ping";
        write(p1[1],ping,sizeof(ping));
        close(p1[1]);
        wait(0);
        //wait((int *)0);
        close(p2[1]);
        read(p2[0],buf_father,sizeof(buf_father));
        printf("%d:received %s",getpid(),buf_father);
        close(p2[0]);
        exit(0);



    }

}