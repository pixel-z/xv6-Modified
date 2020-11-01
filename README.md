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

### PBS

`set_priority()` system call implemented
`checkPreempt()` system call implemented

### MLFQ 

Limit for aging is kept 20.