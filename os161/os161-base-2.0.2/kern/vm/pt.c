#include <pt.h>
#include <lib.h>
#include <addrspace.h>
#include <types.h>
#include <proc.h>
#include <synch.h>
//save the index of the page corresponding to the given virtual addr, return 1 if it succeeds, 0 otherwise
int getPageV (vaddr_t vaddr, unsigned *j) {
    int retVal;
    struct addrspace *as = proc_getas();
	if (as != NULL)
	{
        lock_acquire(as->pt_lock);
        pt_entry *pt = as->as_pt;
        for(unsigned i = 0; i < as->npages; i++) {
            if(pt[i].vaddr == vaddr) {
                *j = i;
                retVal = 1;
                break;
            }
        }
        retVal= -1;
        lock_release(as->pt_lock);
    } else retVal = 0;
    return retVal;
}

//save the index of the page corresponding to the given physical addr, return 1 if it succeeds, 0 otherwise
int getPageP (paddr_t paddr, unsigned *j) {
    int retVal;
    struct addrspace *as = proc_getas();
	if (as != NULL)
	{
        lock_acquire(as->pt_lock);
        pt_entry *pt = as->as_pt;
        for(unsigned i = 0; i < as->npages; i++) {
            if(pt[i].paddr == paddr) {
                *j = i;
                retVal = 1;
                break;
            }
         }
        retVal = -1;
        lock_release(as->pt_lock);
    } else retVal = 0;
    
    return retVal;
}

//save the physical address of the page corresponding to the given virtual addr, return 1 if succeeds, 0 otherwise
int getPageVtoP (vaddr_t vaddr, paddr_t *paddr) {
    int retVal;
    struct addrspace *as = proc_getas();
	if (as != NULL)
	{
        int l = checkcanLock();
        if(!l)
            lock_acquire(as->pt_lock);
        pt_entry *pt = as->as_pt;
        for(unsigned i = 0; i < as->npages; i++) {
            if(pt[i].vaddr == vaddr) {
                *paddr = pt[i].paddr;
                retVal = 1;
                break;
            }
        }
        retVal = -1; 
        if(!l)     
            lock_release(as->pt_lock); 
    } else retVal = 0;
   
    return retVal;
}

//save the virtual address of the page corresponding to the given physical addr, return 1 if succeeds, 0 otherwise
int getPagePtoV (paddr_t paddr, vaddr_t *vaddr) {
    int retVal;
    struct addrspace *as = proc_getas();
	if (as != NULL)
	{
        lock_acquire(as->pt_lock);
        pt_entry *pt = as->as_pt;
        for(unsigned i = 0; i < as->npages; i++) {
            if(pt[i].paddr == paddr){
                *vaddr = pt[i].vaddr;
                retVal = 1;
            }
        }
        retVal = -1;        
        lock_release(as->pt_lock);
    } else retVal = 0;
    return retVal;
}