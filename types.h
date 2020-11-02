typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef uint pde_t;

struct proc *queue[5][64];
int q_size[5];   // gives no of processes in each queue (0 index-based)

// shifting queue from q_initial to q_final (aging)
// can be used as push and pop into queue as well when q_i or q_f is -1
int shift_proc_q(struct proc*, int , int );
void change_q_flag(struct proc*);
void incr_curr_ticks(struct proc*);
