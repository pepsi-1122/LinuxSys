#include<stdio.h>
#include<unistd.h>
#include<string.h>

int main()
{
    int fd[2];
    int ret = pipe(fd); 

    if(ret<0)
    {
        perror("creat pipe error");
        return -1;
    }

    ret = fork();
    if(ret>0)
    {
        //father
        char buf[1024] = "hello";
        write(fd[1],buf,sizeof(buf));

    }
    else if(ret == 0)
    {
        //child
        char buf_output[1024] = {0};
        read(fd[0],buf_output,sizeof(buf_output)-1);
        printf("%s\n",buf_output);
    }
    else
    {
        perror("fork error");
    }
    close(fd[1]);
    close(fd[0]);

    return 0;
}