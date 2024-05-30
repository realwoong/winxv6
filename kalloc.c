// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include <stddef.h>
#include "proc.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld


void
add_to_lru_list(struct page *new_page);

pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc);

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;


//pa4 bitmap..

// ���� ������ �ִ� ������ ��
#define MAX_SWAP_PAGES 12437

// ��Ʈ ���� �Լ�
// PTE���� ���� �ƿ� ó�� �Լ�
void SET_out(pte_t *pte, int swap_index) {
    *pte = (swap_index << 10) | PTE_SWAP; // ���� �ε����� �����ϰ� PTE_SWAP ��Ʈ�� ����
    *pte &= ~PTE_P; // PTE_P ��Ʈ�� ����
}

// PTE���� ���� �� ó�� �Լ�
int SET_in(pte_t *pte) {
    int swap_index = (*pte >> 10) & 0x3FFFFF; // PTE���� ���� �ε����� ���� (���� 22��Ʈ ���)
    *pte &= ~PTE_SWAP; // PTE_SWAP ��Ʈ�� ����
    *pte |= PTE_P; // PTE_P ��Ʈ�� ����
    return swap_index; // ������ ���� �ε����� ��ȯ
}


// ���� ������ �����ϱ� ���� ��Ʈ�� �迭
char swap_space_bitmap[MAX_SWAP_PAGES / 8] = {0}; // 1��Ʈ�� �ϳ��� ������ ����

// ��Ʈ�ʿ��� ��� ������ ���� ����� ã�� �Լ�
int find_free_swap_block() {
    for (int i = 0; i < MAX_SWAP_PAGES; i++) { // �ִ� ���� ������ ����ŭ �ݺ�
        int byte_index = i / 8; // ����Ʈ �ε��� ��� (1����Ʈ = 8��Ʈ)
        int bit_index = i % 8;  // ��Ʈ �ε��� ���
        if (!(swap_space_bitmap[byte_index] & (1 << bit_index))) { // �ش� ��Ʈ�� 0���� Ȯ��
            swap_space_bitmap[byte_index] |= (1 << bit_index); // ��� ������ ǥ�� (1�� ����)
            return i; // ��� ������ ���� ��� �ε��� ��ȯ
        }
    }
    return -1; // �� ����� ������ -1 ��ȯ
}

// ��Ʈ�ʿ��� ���� ����� �����ϴ� �Լ�
void free_swap_block(int blockno) {
    int byte_index = blockno / 8; // ����Ʈ �ε��� ���
    int bit_index = blockno % 8;  // ��Ʈ �ε��� ���
    swap_space_bitmap[byte_index] &= ~(1 << bit_index); // �ش� ��Ʈ�� 0���� �����Ͽ� ��� ����
}

// ���� �ƿ� �Լ�
void swap_out_page(struct page *page) {
    int swap_index = find_free_swap_block(); // ��� ������ ���� ��� ã��
    if (swap_index == -1) { // ���� ������ �� �� ���
        panic("OOM : Out of memory\nSwap space is full\n"); // �д� �߻�
    }

    pte_t *pte = walkpgdir(page->pgdir, page->vaddr, 0); // �������� PTE ��������
    if (!pte) { // PTE�� ã�� �� ���� ���
        panic("PTE not found"); // �д� �߻�
    }

    // �������� ���� ������ ����
    swapwrite(page->vaddr, swap_index); // swapwrite �Լ� ȣ��

    // PTE�� ���� �ε����� �����ϰ� ���� ��Ʈ ����, PTE_P ��Ʈ Ŭ����
    SET_out(pte, swap_index); // SET_out �Լ� ȣ��

    // �������� freelist�� �߰�
    kfree(page->vaddr); // ���� �ƿ��� �������� free list�� �߰�
    
}


// ���� �� �Լ�
void swap_in_page(struct page *page) {
    pte_t *pte = walkpgdir(page->pgdir, page->vaddr, 0); // �������� PTE ��������
    if (!pte || !(*pte & PTE_SWAP)) { // �������� ���ҵ� ���°� �ƴ� ���
        panic("Page not swapped out"); // �д� �߻�
    }

    char *new_page = kalloc(); // ���ο� ���� �������� �Ҵ�
    if (!new_page) { // ������ �Ҵ翡 ������ ���
        panic("OOM: Out of memory during swap-in"); // �д� �߻�
    }

    int swap_index = SET_in(pte); // SET_in �Լ� ȣ��� ���� �ε��� ����
    swapread(new_page, swap_index); // ���� �������� �������� �о��
    *pte = V2P(new_page) | PTE_P | PTE_W | PTE_U; // PTE�� ������Ʈ�Ͽ� ���ο� ���� ������ �ּҸ� �����ϰ� PTE_P ��Ʈ�� ����

    free_swap_block(swap_index); // ���� ��� ����
    add_to_lru_list(page); // ���ο� �������� LRU ����Ʈ�� �߰�
}




//pa4 bitmap..


//pa4 skel
struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_free_pages;
int num_lru_pages;

//in param.h SWAPMAX = 100,000 - 500(SWAPBASE)
#define BITMAP_SIZE (SWAPMAX / 8)
//pa4 skel


//pa4 bitmap ���� �߰�

// 1) ��Ʈ�� �ʱ�ȭ �Լ�.
// Q : LRU ���� �ʱ�ȭ�� �ʿ䰡 ������..?

//pa4 bitmap ���� �߰�.

//

//

//pa4 �߰�

int start_LRU = 0;


int 
exist_in_lru(struct page *page) {
    struct page *current = page_lru_head; // LRU ����Ʈ�� �Ӹ� ��带 ���� ���� ����
    if (!current) { // LRU ����Ʈ�� ��� �ִ� ���
        return 0; // �������� LRU ����Ʈ�� �������� ������ ��ȯ
    }

    do {
        if (current == page) { // ���� ��尡 ã���� �ϴ� �������� ���
            return 1; // �������� LRU ����Ʈ�� �������� ��ȯ
        }
        current = current->next; // ���� ���� �̵�
    } while (current != page_lru_head); // �ٽ� �Ӹ� ���� ���ƿ� ������ �ݺ�

    return 0; // �������� LRU ����Ʈ�� �������� ������ ��ȯ
}




int
is_user_page(pde_t *pgdir, char *va)
{
  pte_t *pte = walkpgdir(pgdir, va, 0); // va�� ���� ������ ���̺� ��Ʈ���� ������
  if (pte && (*pte & PTE_U)) // pte�� �����ϰ� PTE_U ��Ʈ��  ������ (user page Ȯ��)
    return 1;
  return 0;
}

void
add_to_lru_list(struct page *new_page)
{
    num_lru_pages++;
    if (page_lru_head == 0) { // LRU ����Ʈ�� ����ִ� ���
        page_lru_head = new_page; // new_page�� LRU ����Ʈ�� �Ӹ� ���� ����
        new_page->next = new_page->prev = new_page; // new_page�� next�� prev�� �ڱ� �ڽ����� ���� (��ȯ ����Ʈ)
    } else { // LRU ����Ʈ�� �̹� ��尡 �ִ� ���
        new_page->next = page_lru_head; // new_page�� next�� ������ �Ӹ� ���� ����
        new_page->prev = page_lru_head->prev; // new_page�� prev�� ���� �Ӹ� ����� prev�� ����
        page_lru_head->prev->next = new_page; // ���� �Ӹ� ����� prev�� next�� new_page�� ����
        page_lru_head->prev = new_page; // ���� �Ӹ� ����� prev�� new_page�� ����
    }
}

void
remove_from_lru_list(struct page *target_page)
{
  num_lru_pages--;
  if (target_page->next != target_page) {
    target_page->next->prev = target_page->prev;
    target_page->prev->next = target_page->next;
    if (page_lru_head == target_page) {
      page_lru_head = target_page->next;
    }
  } else {
    page_lru_head = 0;
  }
}

struct page* find_victim_lru(void)
{
  struct page *victim = page_lru_head; // LRU ����Ʈ�� �Ӹ� ��带 victim���� ����
  pte_t *pte;

  if (!victim) {
    return 0;  // LRU ����Ʈ�� ��������� NULL ��ȯ
  }

  // Clock �˰��� ���
  while (1) {
    pte = walkpgdir(victim->pgdir, victim->vaddr, 0); // ������ ���̺� ��Ʈ�� ��������
    if (pte && (*pte & PTE_A)) { // �������� �ֱٿ� ���ٵǾ����� Ȯ��
      *pte &= ~PTE_A;  // PTE_A ��Ʈ�� Ŭ�����Ͽ� ���ٵ��� ������ ǥ��

      // LRU ����Ʈ�� ������ �̵�
      victim->next->prev = victim->prev; // victim�� ���� ���� ���� ��带 ����
      victim->prev->next = victim->next; // victim�� ����Ʈ���� ����
      victim->next = page_lru_head; // victim�� ����Ʈ�� �Ӹ��� �̵�
      victim->prev = page_lru_head->prev;
      page_lru_head->prev->next = victim;
      page_lru_head->prev = victim;
      page_lru_head = victim->next;  // head�� ���� �������� �̵�
    } else if (pte) {
      return victim; // victim ������ ��ȯ
    }
    victim = victim->next;  // ���� �������� �̵�
  }

  return 0;  // ��ü�� �������� ������ NULL ��ȯ
}




void print_num_lru_pages()
{
  cprintf("num_lru_pages : %d\n", num_lru_pages);
}
//pa4 �߰�



// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
  start_LRU = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  // v�� ������ ũ��(PGSIZE)�� ����� �ƴϰų�, 
  // v�� Ŀ���� �� �ּ�(end)���� �۰ų�, 
  // v�� ���� �ּҰ� PHYSTOP �̻��� ��� �д�
  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // �������� 1�� ä���� �޸� ��ġ ���� ������ ����
  memset(v, 1, PGSIZE);

  // kmem ����ü�� ���� ��� ���� ��� �� ȹ��
  if(kmem.use_lock)
    acquire(&kmem.lock);

  
  // LRU ����Ʈ�� �����ϴ� ���������� Ȯ��
  struct page *page_to_remove = &pages[V2P(v) / PGSIZE]; // ������ ������ ����

  // �������� LRU ����Ʈ�� �����ϴ� ���
  if (exist_in_lru(page_to_remove)) {
    remove_from_lru_list(page_to_remove); // LRU ����Ʈ���� ������ ����
    num_lru_pages--; // LRU ����Ʈ���� �������� ���ŵ� �� num_lru_pages ����
  }

  
  // run ����ü ������ r�� v�� ����
  r = (struct run*)v;
  
  // r�� ������ freelist �տ� ����
  r->next = kmem.freelist;
  kmem.freelist = r;

  // kmem ����ü�� ���� ��� ���� ��� �� ����
  if(kmem.use_lock)
    release(&kmem.lock);
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

int reclaim(void)
{
   struct page *victim = find_victim_lru(); // LRU ����Ʈ���� victim �������� ã��
    if (!victim) { // victim �������� ������.
        return -1; // reclaim ����, -1 ��ȯ   -> kalloc���� OOM ���� �˾Ƽ� �� ���ٰ���.
    }

    pte_t *pte = walkpgdir(victim->pgdir, victim->vaddr, 0); // victim �������� PTE ��������
    if (!pte) { // PTE�� ã�� �� ���� ���
        panic("reclaim: PTE not found"); // �д� �߻�
    }

    swap_out_page(victim); // victim �������� ���� �ƿ�

    return 1; // reclaim ����, 1 ��ȯ
}

char*
kalloc(void)
{
  struct run *r;

try_again:
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  
  if(!r)  // freelist�� ����ִ� -> �޸𸮿� �ڸ��� ���ٸ�.
  {
    int a = reclaim();
    if(a==1)    // reclaim�� 1�� ��ȯ�ߴٸ� == ���������� reclaim.
    {
      goto try_again;

    }
    if(a==-1)   // reclaim�� -1�� ��ȯ�ߴٸ� == reclaim�� �����Ͽ���. == OOM
    {
      cprintf("ERROR : OOM - Out of memory\n");
      
      if(kmem.use_lock)
        release(&kmem.lock);

      return 0; // �̶� kalloc�� 0�� ����.
    }
  }  

  if(r)
  {         // �׳� freelist�� ������ ���� ���. -> lru ���� ó�� �ʿ���.
    kmem.freelist = r->next;
    if(start_LRU)
    {
      // lru�� �߰��ϴ� 
      struct page *new_page = &pages[V2P((char*)r) / PGSIZE]; // ���ο� ������ ����
      struct proc *p = myproc(); // ���� ���μ����� ������

      // ���� ���μ����� �ִ� ��쿡�� LRU ����Ʈ�� �߰�
      if (p != NULL) {  
        new_page->pgdir = p->pgdir; // ���� ���μ����� ������ ���丮 ����
        new_page->vaddr = (char*)r; // ���ο� �������� ���� �ּ� ����

        // user page Ȯ��
        if (is_user_page(new_page->pgdir, new_page->vaddr)) { 
          add_to_lru_list(new_page); // LRU ����Ʈ�� ������ �߰�
          num_lru_pages++;  // LRU ����Ʈ�� �������� �߰��� �� count_LRU ����
        }
      }

      // lru�� �߰��ϴ� �ڵ� 
    }

  }

  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}


