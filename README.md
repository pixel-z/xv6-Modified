## Usage
```bash
    make clean
    make qemu SCHEDULER=<RR,FCFS,PBS,MLFQ>
```

### waitx 

`waitx()` is a system call similar to wait() except that the calling function's runtime and wait time are also stored & retrieved.  
`time.c` has been added to check the functioning of waitx. It returns runtime and wait time of the process    
Usage: `time <command>`.
Eg: "`time ls`"     
`change_time()` function in proc.c has been added.

Files modified: 
- Makefile
- defs.h
- proc.h
- proc.c
- syscall.h
- syscall.c
- sysproc.c
- time.c
- trap.c 
- user.h
- usys.S


## Scheduling

RR - Round Robin (Default)  
Custom scheduling: `<RR,FCFS,PBS,MLFQ>`

To select a scheduler use: `make qemu SCHEDULER = <scheduler>`

### FCFS
Changes:
- proc.c - `scheduler()`
- trap.c - `trap()`

- FCFS simply queues processes in the order that they arrive in the ready queue.
- Preemption is not allowed.
- The process that comes first will be executed first and next process starts only after the previous gets fully executed.


### PBS
Default priority of processes are set to 60  
`set_priority()` system call implemented    
`checkPreempt()` system call implemented    

- Priority based scheduling (PBS) is a preemptive algorithm.
- Each process is assigned a priority. Process with highest priority (numerically least) is to be executed first and so on.
- Processes with same priority are executed in a round robin fashion.
- If a process of higher priority (numerically less) arrives while a lower priority process is being executed the lower priority process is preempted.

### MLFQ 

Limit for aging is kept 20.

- Processes are initially assigned the 0th queue out of total 5 queues.
- It allows a process to move between queues. If a process uses too much CPU time, it will be moved to a lower-priority queue. Similarly, a process that waits too long in a lower-priority queue may be moved to a higher-priority queue. Aging prevents starvation.


## Features
- ps : lists stats of active processes 
- tester : benchmark process (tester) for scheduling algorithms

## Explain in the report how could this be exploited by a process    

This can be exploited by a process, as just when the time-slice is about to expire, the process can voluntarily relinquish control of the CPU, and get inserted in the same queue again. This will make it retain its priority queue no. and never decrement. It will run again sooner than when it is run normally. 

## Performance Analysis of Scheduling Algorithms    

After running the following command on qemu:    
        `time tester`
- Round Robin - rtime=7, wtime=1997
- FCFS - rtime=4, wtime=2642
- PBS  - rtime=8, wtime=1999
- MLFQ - rtime=6, wtime=2170