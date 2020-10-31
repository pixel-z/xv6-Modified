#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf(1,"Usage: setPriority <new_priority> <pid>\n");
    }
    else
    {
        int new_priority = atoi(argv[1]);
        int pid = atoi(argv[2]);

        if (new_priority<0 || new_priority>100)
        {
            printf(1,"<new_priority> should be between 0 and 100\n");
            exit();
        }
        int old_priority = set_priority(new_priority, pid);
        if (old_priority<0)
            printf(1,"PID not found\n");
        else
            printf(1,"Process %d priority changed: Old priority %d -> New priority %d\n", pid, old_priority, new_priority);
    }

    exit();
}