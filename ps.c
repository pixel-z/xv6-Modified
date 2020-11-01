#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char *argv[])
{
    if (argc != 1)
        printf(1,"Usage: ps\n");
    else
    {
        printf(1,"PID Priority   State   r_time w_time  n_tun  cur_q  | q0  q1  q2  q3  q4  q5  q6\n");
        
        printpinfos();  // prints all the information
    }
    
    exit();
}