#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"



void sonprocess(int left[2])//left是和左边进程的管道(p_front[0])
{
    
    int pass_num;
    read(left[0],&pass_num,sizeof(int));
    if(pass_num==-1)
    {
        exit(0);//退出该程序
    }
    printf("prime %d\n",pass_num);//第一次读入得一定是素数
    int right[2];
    pipe(right);
    if(fork()==0)//进入子进程
    {
        close(right[1]);
        close(left[0]);
        sonprocess(right);
    }
    else
    {   
        close(right[0]);
        int buf;
        while (read(left[0],&buf,sizeof(int))&&buf!=-1)
        {
            if(buf%pass_num!=0)
            {
                write(right[1],&buf,sizeof(int));
            }
        }
        buf=-1;
        write(right[1],&buf,sizeof(int));
        wait(0);
        exit(0);

    }

}

int main(int argc, char *argv[])
{
   int input_p[2];
   pipe(input_p);
   if(fork()==0)
   {
    close(input_p[1]);
    sonprocess(input_p);
    exit(0);
    }
    else
    {
        close(input_p[0]);
        int i;
        for(int i=2;i<35;i++)
        {
            write(input_p[1],&i,sizeof(int));

        }
        i=-1;
        write(input_p[1],&i,sizeof(i));
    }
    wait (0);
    exit(0);
}

// void runprocess(int listenfd) {
//   int my_num = 0;
//   int forked = 0;
//   int passed_num = 0;
//   int pipes[2];
//   while (1) {
//     int read_bytes = read(listenfd, &passed_num, 4);

//     // left neighbor has no more number to provide
//     if (read_bytes == 0) {
//       close(listenfd);
//       if (fork()) {
//         // tell my children I have no more number to offer
//         close(pipes[1]);
//         // wait my child termination
//         int child_pid;
//         wait(&child_pid);
//       }
//       exit(0);
//     }

//     // if my initial read
//     if (my_num == 0) {
//       my_num = passed_num;
//       printf("prime %d\n", my_num);
//     }

//     // not my prime multiple, pass along
//     if (passed_num % my_num != 0) {
        
//       if (!forked) {
//         pipe(pipes);
//         forked = 1;
//         int ret = fork();
//         if (ret == 0) {
//           // i am the child
//           close(pipes[1]);
//           close(listenfd);
//           runprocess(pipes[0]);
//         } else {
//           // i am the parent
//           close(pipes[0]);
//         }
//       }
//       // pass the number to right neighbor
//       write(pipes[1], &passed_num, 4);
//     }
//   }
// }

// int main(int argc, char *argv[]) {
//   int pipes[2];
//   pipe(pipes);
//   for (int i = 2; i <= 35; i++) {
//     write(pipes[1], &i, 4);
//   }
//   close(pipes[1]);
//   runprocess(pipes[0]);
//   exit(0);
// }

