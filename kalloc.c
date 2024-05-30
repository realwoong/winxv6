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

// 스왑 가능한 최대 페이지 수
#define MAX_SWAP_PAGES 12437

// 비트 설정 함수
// PTE에서 스왑 아웃 처리 함수
void SET_out(pte_t *pte, int swap_index) {
    *pte = (swap_index << 10) | PTE_SWAP; // 스왑 인덱스를 설정하고 PTE_SWAP 비트를 설정
    *pte &= ~PTE_P; // PTE_P 비트를 해제
}

// PTE에서 스왑 인 처리 함수
int SET_in(pte_t *pte) {
    int swap_index = (*pte >> 10) & 0x3FFFFF; // PTE에서 스왑 인덱스를 추출 (상위 22비트 사용)
    *pte &= ~PTE_SWAP; // PTE_SWAP 비트를 해제
    *pte |= PTE_P; // PTE_P 비트를 설정
    return swap_index; // 추출한 스왑 인덱스를 반환
}


// 스왑 공간을 추적하기 위한 비트맵 배열
char swap_space_bitmap[MAX_SWAP_PAGES / 8] = {0}; // 1비트당 하나의 페이지 추적

// 비트맵에서 사용 가능한 스왑 블록을 찾는 함수
int find_free_swap_block() {
    for (int i = 0; i < MAX_SWAP_PAGES; i++) { // 최대 스왑 페이지 수만큼 반복
        int byte_index = i / 8; // 바이트 인덱스 계산 (1바이트 = 8비트)
        int bit_index = i % 8;  // 비트 인덱스 계산
        if (!(swap_space_bitmap[byte_index] & (1 << bit_index))) { // 해당 비트가 0인지 확인
            swap_space_bitmap[byte_index] |= (1 << bit_index); // 사용 중으로 표시 (1로 설정)
            return i; // 사용 가능한 스왑 블록 인덱스 반환
        }
    }
    return -1; // 빈 블록이 없으면 -1 반환
}

// 비트맵에서 스왑 블록을 해제하는 함수
void free_swap_block(int blockno) {
    int byte_index = blockno / 8; // 바이트 인덱스 계산
    int bit_index = blockno % 8;  // 비트 인덱스 계산
    swap_space_bitmap[byte_index] &= ~(1 << bit_index); // 해당 비트를 0으로 설정하여 블록 해제
}

// 스왑 아웃 함수
void swap_out_page(struct page *page) {
    int swap_index = find_free_swap_block(); // 사용 가능한 스왑 블록 찾기
    if (swap_index == -1) { // 스왑 공간이 꽉 찬 경우
        panic("OOM : Out of memory\nSwap space is full\n"); // 패닉 발생
    }

    pte_t *pte = walkpgdir(page->pgdir, page->vaddr, 0); // 페이지의 PTE 가져오기
    if (!pte) { // PTE를 찾을 수 없는 경우
        panic("PTE not found"); // 패닉 발생
    }

    // 페이지를 스왑 공간에 저장
    swapwrite(page->vaddr, swap_index); // swapwrite 함수 호출

    // PTE에 스왑 인덱스를 설정하고 스왑 비트 설정, PTE_P 비트 클리어
    SET_out(pte, swap_index); // SET_out 함수 호출

    // 페이지를 freelist에 추가
    kfree(page->vaddr); // 스왑 아웃한 페이지를 free list에 추가
    
}


// 스왑 인 함수
void swap_in_page(struct page *page) {
    pte_t *pte = walkpgdir(page->pgdir, page->vaddr, 0); // 페이지의 PTE 가져오기
    if (!pte || !(*pte & PTE_SWAP)) { // 페이지가 스왑된 상태가 아닌 경우
        panic("Page not swapped out"); // 패닉 발생
    }

    char *new_page = kalloc(); // 새로운 물리 페이지를 할당
    if (!new_page) { // 페이지 할당에 실패한 경우
        panic("OOM: Out of memory during swap-in"); // 패닉 발생
    }

    int swap_index = SET_in(pte); // SET_in 함수 호출로 스왑 인덱스 추출
    swapread(new_page, swap_index); // 스왑 공간에서 페이지를 읽어옴
    *pte = V2P(new_page) | PTE_P | PTE_W | PTE_U; // PTE를 업데이트하여 새로운 물리 페이지 주소를 설정하고 PTE_P 비트를 설정

    free_swap_block(swap_index); // 스왑 블록 해제
    add_to_lru_list(page); // 새로운 페이지를 LRU 리스트에 추가
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


//pa4 bitmap 관련 추가

// 1) 비트맵 초기화 함수.
// Q : LRU 관련 초기화는 필요가 없겠지..?

//pa4 bitmap 관련 추가.

//

//

//pa4 추가

int start_LRU = 0;


int 
exist_in_lru(struct page *page) {
    struct page *current = page_lru_head; // LRU 리스트의 머리 노드를 현재 노드로 설정
    if (!current) { // LRU 리스트가 비어 있는 경우
        return 0; // 페이지가 LRU 리스트에 존재하지 않음을 반환
    }

    do {
        if (current == page) { // 현재 노드가 찾고자 하는 페이지인 경우
            return 1; // 페이지가 LRU 리스트에 존재함을 반환
        }
        current = current->next; // 다음 노드로 이동
    } while (current != page_lru_head); // 다시 머리 노드로 돌아올 때까지 반복

    return 0; // 페이지가 LRU 리스트에 존재하지 않음을 반환
}




int
is_user_page(pde_t *pgdir, char *va)
{
  pte_t *pte = walkpgdir(pgdir, va, 0); // va에 대한 페이지 테이블 엔트리를 가져옴
  if (pte && (*pte & PTE_U)) // pte가 존재하고 PTE_U 비트가  있으면 (user page 확인)
    return 1;
  return 0;
}

void
add_to_lru_list(struct page *new_page)
{
    num_lru_pages++;
    if (page_lru_head == 0) { // LRU 리스트가 비어있는 경우
        page_lru_head = new_page; // new_page를 LRU 리스트의 머리 노드로 설정
        new_page->next = new_page->prev = new_page; // new_page의 next와 prev를 자기 자신으로 설정 (순환 리스트)
    } else { // LRU 리스트에 이미 노드가 있는 경우
        new_page->next = page_lru_head; // new_page의 next를 현재의 머리 노드로 설정
        new_page->prev = page_lru_head->prev; // new_page의 prev를 현재 머리 노드의 prev로 설정
        page_lru_head->prev->next = new_page; // 현재 머리 노드의 prev의 next를 new_page로 설정
        page_lru_head->prev = new_page; // 현재 머리 노드의 prev를 new_page로 설정
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
  struct page *victim = page_lru_head; // LRU 리스트의 머리 노드를 victim으로 설정
  pte_t *pte;

  if (!victim) {
    return 0;  // LRU 리스트가 비어있으면 NULL 반환
  }

  // Clock 알고리즘 사용
  while (1) {
    pte = walkpgdir(victim->pgdir, victim->vaddr, 0); // 페이지 테이블 엔트리 가져오기
    if (pte && (*pte & PTE_A)) { // 페이지가 최근에 접근되었는지 확인
      *pte &= ~PTE_A;  // PTE_A 비트를 클리어하여 접근되지 않음을 표시

      // LRU 리스트의 끝으로 이동
      victim->next->prev = victim->prev; // victim의 이전 노드와 다음 노드를 연결
      victim->prev->next = victim->next; // victim을 리스트에서 제거
      victim->next = page_lru_head; // victim을 리스트의 머리로 이동
      victim->prev = page_lru_head->prev;
      page_lru_head->prev->next = victim;
      page_lru_head->prev = victim;
      page_lru_head = victim->next;  // head를 다음 페이지로 이동
    } else if (pte) {
      return victim; // victim 페이지 반환
    }
    victim = victim->next;  // 다음 페이지로 이동
  }

  return 0;  // 교체할 페이지가 없으면 NULL 반환
}




void print_num_lru_pages()
{
  cprintf("num_lru_pages : %d\n", num_lru_pages);
}
//pa4 추가



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

  // v가 페이지 크기(PGSIZE)의 배수가 아니거나, 
  // v가 커널의 끝 주소(end)보다 작거나, 
  // v의 물리 주소가 PHYSTOP 이상일 경우 패닉
  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // 페이지를 1로 채워서 메모리 덩치 남는 참조를 잡음
  memset(v, 1, PGSIZE);

  // kmem 구조체의 락을 사용 중인 경우 락 획득
  if(kmem.use_lock)
    acquire(&kmem.lock);

  
  // LRU 리스트에 존재하는 페이지인지 확인
  struct page *page_to_remove = &pages[V2P(v) / PGSIZE]; // 제거할 페이지 설정

  // 페이지가 LRU 리스트에 존재하는 경우
  if (exist_in_lru(page_to_remove)) {
    remove_from_lru_list(page_to_remove); // LRU 리스트에서 페이지 제거
    num_lru_pages--; // LRU 리스트에서 페이지가 제거된 후 num_lru_pages 감소
  }

  
  // run 구조체 포인터 r을 v로 설정
  r = (struct run*)v;
  
  // r을 현재의 freelist 앞에 삽입
  r->next = kmem.freelist;
  kmem.freelist = r;

  // kmem 구조체의 락을 사용 중인 경우 락 해제
  if(kmem.use_lock)
    release(&kmem.lock);
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

int reclaim(void)
{
   struct page *victim = find_victim_lru(); // LRU 리스트에서 victim 페이지를 찾음
    if (!victim) { // victim 페이지가 없으면.
        return -1; // reclaim 실패, -1 반환   -> kalloc에서 OOM 띄우고 알아서 잘 해줄거임.
    }

    pte_t *pte = walkpgdir(victim->pgdir, victim->vaddr, 0); // victim 페이지의 PTE 가져오기
    if (!pte) { // PTE를 찾을 수 없는 경우
        panic("reclaim: PTE not found"); // 패닉 발생
    }

    swap_out_page(victim); // victim 페이지를 스왑 아웃

    return 1; // reclaim 성공, 1 반환
}

char*
kalloc(void)
{
  struct run *r;

try_again:
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  
  if(!r)  // freelist가 비어있다 -> 메모리에 자리가 없다면.
  {
    int a = reclaim();
    if(a==1)    // reclaim이 1을 반환했다면 == 성공적으로 reclaim.
    {
      goto try_again;

    }
    if(a==-1)   // reclaim이 -1을 반환했다면 == reclaim에 실패하였음. == OOM
    {
      cprintf("ERROR : OOM - Out of memory\n");
      
      if(kmem.use_lock)
        release(&kmem.lock);

      return 0; // 이때 kalloc는 0을 리턴.
    }
  }  

  if(r)
  {         // 그냥 freelist에 여유가 있을 경우. -> lru 관련 처리 필요함.
    kmem.freelist = r->next;
    if(start_LRU)
    {
      // lru에 추가하는 
      struct page *new_page = &pages[V2P((char*)r) / PGSIZE]; // 새로운 페이지 설정
      struct proc *p = myproc(); // 현재 프로세스를 가져옴

      // 현재 프로세스가 있는 경우에만 LRU 리스트에 추가
      if (p != NULL) {  
        new_page->pgdir = p->pgdir; // 현재 프로세스의 페이지 디렉토리 설정
        new_page->vaddr = (char*)r; // 새로운 페이지의 가상 주소 설정

        // user page 확인
        if (is_user_page(new_page->pgdir, new_page->vaddr)) { 
          add_to_lru_list(new_page); // LRU 리스트에 페이지 추가
          num_lru_pages++;  // LRU 리스트에 페이지가 추가된 후 count_LRU 증가
        }
      }

      // lru에 추가하는 코드 
    }

  }

  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}


