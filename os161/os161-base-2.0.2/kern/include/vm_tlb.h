#ifndef _VM_TLB_H_
#define _VM_TLB_H_
#include <types.h>

int tlb_loadentry(vaddr_t faultaddress, paddr_t paddr, bool readOnly);
int tlb_get_rr_victim(void);
void tlb_invalidate(void);
void tlb_invalidate_vaddr(vaddr_t vaddr);
#endif