#ifndef VM_STATS_H
#define VM_STATS_H

enum {
    TLB_FAULT, 
    TLB_FAULT_WITH_FREE, 
    TLB_FAULT_WITH_REPLACE, 
    TLB_INVALIDATION, 
    TLB_RELOAD, 
    PAGE_FAULT_ZEROED, 
    PAGE_FAULT_DISK, 
    ELF_READ, 
    SWAP_READ, 
    SWAP_WRITE, 
};

#define STATS_TOT 10

void vm_stats_init(void);                    

void vm_stats_inc(unsigned int index);   

void vm_stats_print(void);                    

#endif /* VM_STATS_H */