#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char *argv[])
{
    int wtime, rtime;
    int pid = fork();

    if (pid==0)
    {
        // we will do similar to execvp but argv[0] should be removed
        for (int i = 0; i < argc-1; i++)
        {
            argv[i] = argv[i+1];
        }
        argv[argc-1] = 0;
        exec(argv[0], argv);
    }
    else
    {
        waitx(&wtime,&rtime);
        printf(1,"rtime = %d, wtime = %d\n",rtime,wtime);
    }
    
    exit();
}