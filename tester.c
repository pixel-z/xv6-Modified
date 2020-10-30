// #include "types.h"
// #include "stat.h"
// #include "user.h"

// // Parent forks two children, waits for them to exit and then finally exits
// int main(int argc, char* argv[]) {
// #ifdef FCFS
//     int count = 10, lim = 1e7;
//     for (int j = 0; j < count; j++) {
//         if (fork() == 0) {
//             volatile int a = 0;
//             for (volatile int i = 0; i <= lim; i++) {
//                 a += 3;
//             }
//             printf(1, "%d\n", a);
//             exit();
//         }
//     }

//     for (int i = 0; i < count; i++) {
//         wait();
//     }

//     exit();
// #endif
// #ifdef RR
//     int count = 10, lim = 1e7;
//     for (int j = 0; j < count; j++) {
//         if (fork() == 0) {
//             volatile int a = 0;
//             int pid = getpid();
//             for (volatile int i = 0; i <= lim; i++) {
//                 if (i % (lim / 10) == 0) {
//                     printf(1, "Completed %d by %d of pid %d\n", i / (lim / 10),
//                            10, pid);
//                 }
//                 a += 3;
//             }
//             printf(1, "%d\n", a);
//             exit();
//         }
//     }

//     for (int i = 0; i < count; i++) {
//         wait();
//     }

//     exit();
// #endif
// }


#include "types.h"
#include "user.h"

int number_of_processes = 5;

int main(int argc, char *argv[])
{
  int j;
  for (j = 0; j < number_of_processes; j++)
  {
    int pid = fork();
    if (pid < 0)
    {
      printf(1, "Fork failed\n");
      continue;
    }
    if (pid == 0)
    {
      volatile int i;
      for (volatile int k = 0; k < number_of_processes; k++)
      {
        if (k <= j)
        {
          sleep(200); //io time
        }
        else
        {
          for (i = 0; i < 100000000; i++)
          {
              if(i%100000000==0)
                printf(1,"%d\n",j); 
            ; //cpu time
          }
        }
      }
      printf(1, "Process: %d Finished\n", j);
      exit();
    }
    else{
        ;
    //   set_priority(100-(20+j),pid); // will only matter for PBS, comment it out if not implemented yet (better priorty for more IO intensive jobs)
    }
  }
  for (j = 0; j < number_of_processes+5; j++)
  {
    wait();
  }
  exit();
}