// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for (int i=0;i<NCPU;i++) {
    char lock_name[5];
    snprintf(lock_name, 5, "kmem%d", i);
    initlock(&kmem[i].lock, lock_name);
  }
  // it will allocate all free pages to the first cpu,
  // so it will be so probable that other cpu's have to steal from him.
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cpu_id = cpuid();
  pop_off();

  acquire(&kmem[cpu_id].lock);
  r->next = kmem[cpu_id].freelist;
  kmem[cpu_id].freelist = r;
  release(&kmem[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpu_id = cpuid();
  pop_off();

  acquire(&kmem[cpu_id].lock);

  if(!kmem[cpu_id].freelist){
    for (int i=0;i<NCPU;i++) {
      if (i == cpu_id) continue;
      acquire(&kmem[i].lock);
      if(kmem[i].freelist && kmem[i].freelist->next){
        kmem[cpu_id].freelist = kmem[i].freelist->next;
        // split the freelist into 2 equal lists
        splitLinkedList(i);
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
    ;
  }

  r = kmem[cpu_id].freelist;
  if(r)
    kmem[cpu_id].freelist = r->next;
  release(&kmem[cpu_id].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void splitLinkedList(int i)
{
  struct run *my_cpu_linker = kmem[i].freelist->next;
  struct run *other_cpu_linker = kmem[i].freelist;
  for (;;)
  {
    if (other_cpu_linker)
    {
      if (other_cpu_linker->next)
      {
        other_cpu_linker->next = other_cpu_linker->next->next;
      }
      other_cpu_linker = other_cpu_linker->next;
    }
    if (my_cpu_linker)
    {
      if (my_cpu_linker->next)
      {
        my_cpu_linker->next = my_cpu_linker->next->next;
      }
      my_cpu_linker = my_cpu_linker->next;
    }
    if ((!my_cpu_linker) && (!other_cpu_linker))
    {
      break;
    }
  }
}
