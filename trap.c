#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

extern struct page pages[];

// Function prototypes for functions used in trap.c
pte_t *walkpgdir(pde_t *pgdir, const void *va, int alloc);
char *kalloc(void);
void swapread(char *ptr, int blkno);
int SET_in(pte_t *pte);
void add_to_lru_list(struct page *new_page);
void reclaim(void);
void swap_in_page(struct page *page);


void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  
  case T_PGFLT: { // 페이지 폴트 트랩 번호
    uint addr = rcr2(); // 접근한 가상 주소를 cr2 레지스터에서 가져옴
    struct proc *p = myproc(); // 현재 실행 중인 프로세스를 가져옴
    
    pte_t *pte = walkpgdir(p->pgdir, (void*)addr, 0); // 페이지 디렉토리에서 가상 주소에 해당하는 PTE를 가져옴
    if (!pte) { // PTE를 찾을 수 없는 경우
      panic("trap: PTE not found"); // 패닉 발생
    }

    if (*pte & PTE_P) { // PTE_P 비트가 설정된 경우 (이미 페이지가 메모리에 있는 경우)
      panic("trap: Page fault but PTE_P is set"); // 패닉 발생
    }

    if (*pte & PTE_SWAP) { // PTE_SWAP 비트가 설정된 경우 (스왑 아웃된 페이지인 경우)
      //swapin의 코드!
      struct page *page = &pages[V2P((char*)addr) / PGSIZE];
      swap_in_page(page); // swap_in_page 함수 호출

      //swapin의 코드
      } else {
      panic("trap: Unexpected page fault"); // 예상치 못한 페이지 폴트인 경우 패닉 발생
    }
    break;
  }




  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
