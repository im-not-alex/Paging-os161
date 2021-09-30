#include <types.h>
#include <lib.h>
#include <synch.h>
#include <spl.h>
#include "vmstats.h"
#include <spinlock.h>
#include <addrspace.h>
/* Counters for tracking statistics */
static unsigned int counters[STATS_TOT]; //STATS_TOT is defined equalto 10 in the header file

struct lock *stats_lock;

/* Strings used in printing out the statistics */
static const char *names[] = {
  "TLB Faults", 
  "TLB Faults with Free",
  "TLB Faults with Replace",
  "TLB Invalidations",
  "TLB Reloads",
  "Page Faults (Zeroed)",
  "Page Faults (Disk)",
  "Page Faults from ELF",
  "Page Faults from Swapfile",
  "Swapfile Writes",
};

void
vm_stats_init()
{

  for (unsigned i=0; i<STATS_TOT; i++) {
    counters[i] = 0;
  }

  stats_lock = lock_create("STATS_lock");
  if(stats_lock == NULL)
    panic("Stats lock was not created succesfully\n");
}

void
vm_stats_print()
{
  lock_acquire(stats_lock);

  kprintf("Statistics:\n");
  for (unsigned i=0; i<STATS_TOT; i++) {
    kprintf("%30s = %10d\n", names[i], counters[i]);
  }

  unsigned faults = counters[TLB_FAULT];
  unsigned tot_tlb = counters[TLB_FAULT_WITH_FREE] + counters[TLB_FAULT_WITH_REPLACE];
  unsigned dzr_sum = counters[PAGE_FAULT_DISK] + counters[PAGE_FAULT_ZEROED] + counters[TLB_RELOAD];
  unsigned disk_sum = counters[ELF_READ] + counters[SWAP_READ];
  unsigned disk = counters[PAGE_FAULT_DISK];

  kprintf("%s + %s = %d\n", names[TLB_FAULT_WITH_FREE],names[TLB_FAULT_WITH_REPLACE],tot_tlb);
  if (faults != tot_tlb) {
    kprintf("INCONSISTENCY: %s (%d) != %s + %s (%d)\n",
      names[TLB_FAULT], faults, names[TLB_FAULT_WITH_FREE], names[TLB_FAULT_WITH_REPLACE], tot_tlb); 
  }

  kprintf("%s + %s + %s = %d\n", names[PAGE_FAULT_DISK],names[PAGE_FAULT_ZEROED], names[TLB_RELOAD], dzr_sum);
  if (faults != dzr_sum) {
    kprintf("INCONSISTENCY: %s (%d) != %s + %s + %s (%d)\n",  names[TLB_FAULT], faults, names[PAGE_FAULT_DISK],names[PAGE_FAULT_ZEROED], names[TLB_RELOAD], dzr_sum); 
  }

  kprintf("%s + %s = %d\n", names[ELF_READ] , names[SWAP_READ], disk_sum);
  if (disk != disk_sum) {
    kprintf("INCONSISTENCY: %s (%d) != %s + %s (%d)\n", names[PAGE_FAULT_DISK], disk, names[ELF_READ], names[SWAP_READ], disk_sum);
  }

  lock_release(stats_lock);
  lock_destroy(stats_lock);
}

static void
_vm_stats_inc(unsigned int index)
{
  KASSERT(index < STATS_TOT);
  counters[index]++;
}
void
vm_stats_inc(unsigned int index)
{
  if(!checkcanLock()) {
  lock_acquire(stats_lock);
  _vm_stats_inc(index);
  lock_release(stats_lock);
  } else _vm_stats_inc(index);
}