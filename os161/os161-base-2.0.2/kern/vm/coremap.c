#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>
#include <coremap.h>
#include <mips/types.h>
#include <spinlock.h>
#include <addrspace.h>
#include <proc.h>
#include <swapfile.h>
#include <vmstats.h>
#include <vm_tlb.h>
#include <synch.h>

static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;

unsigned int coremapSize;
unsigned int ram_used = 0;
static paddr_t firstpaddr; /* address of first free physical page */
static paddr_t lastpaddr;  /* one past end of last free physical page */
static int coremapActive = 0;

static int swapvictim()
{
    int svic;
	static unsigned int next_svic = 0;
	svic = next_svic;
	next_svic = (next_svic + 1) % coremapSize;
	return svic;
}

static int _isCoremapActive()
{
    return coremapActive;
}

int isCoremapActive()
{
    int active;
    spinlock_acquire(&coremap_lock);
    active = _isCoremapActive();
    spinlock_release(&coremap_lock);
    return active;
}

void coremap_init(void)
{
    lastpaddr = ram_getsize();
    firstpaddr = ram_getfirstfree();
    firstpaddr = ALLIGN_NEXT_ADDR(firstpaddr);
    
    coremapSize = (lastpaddr - firstpaddr) / PAGE_SIZE;

    unsigned csize = DIVROUNDUP(coremapSize * sizeof(c_entry), PAGE_SIZE); //size of page table to reference the total pages
    coremapSize -= csize; // to account for the pages taken up by the allocation of the coremap
    coremap = (c_entry *)PADDR_TO_KVADDR(firstpaddr);  //this is the starting address of the coremap now, it represents its free entry
    
    bzero(coremap, sizeof(c_entry) * coremapSize);
    firstpaddr += csize * PAGE_SIZE; //this represents the first free entry of the ram

    spinlock_acquire(&coremap_lock);
    coremapActive = 1;
    spinlock_release(&coremap_lock);
}

static void set_coreentry(int i, vaddr_t vaddr, bool is_reserved, struct addrspace *as)
{
    coremap[i].vaddr = vaddr | (is_reserved ? 0x3 : 0x2);
    coremap[i].as = is_reserved ? NULL : as;
}

static void set_empty(int i)
{
    coremap[i].vaddr = 0;
    coremap[i].as = NULL;
}


void freeAs(struct addrspace *as)
{
    spinlock_acquire(&coremap_lock);
    for (unsigned i = 0; i < coremapSize; i++)
    {
        if (coremap[i].as == as)
        {
            set_empty(i);
        }
    }
    spinlock_release(&coremap_lock);
}

static paddr_t getPage(bool is_reserved, vaddr_t vaddr, struct addrspace* as)
{
    paddr_t addr;
    unsigned i;
    spinlock_acquire(&coremap_lock);

    if(! _isCoremapActive()) {
        spinlock_release(&coremap_lock);
        return 0;
    }
        
    for (i = 0; i < coremapSize; i++)
    {
        //look for an available entry in the coremap
        if (!CUSED(i) && !CRES(i))
        {
            addr = i ;
            set_coreentry(i,vaddr,is_reserved, as); //mark the entry in the coremap as filled
            coremap[i].allocpages = 1;
            spinlock_release(&coremap_lock);
            break;
        }
    }
    if (i == coremapSize) //no available entries in the coremap
    {
        addr = 0;
        if (as != NULL)
        {
            if (getAvailableSwap()) //check if the swap file has free space
            {
                pt_entry *pt = as->as_pt;
                while(1) {
                    // look for an entry that is not reserved and in the same 	
	                // address space of the current process
                    i = swapvictim();
                    if(!(coremap[i].vaddr & 0x1))
                        if(coremap[i].as == as)
                            break;
                } 
                   
                spinlock_release(&coremap_lock);
                lock_acquire(as->pt_lock);
                for (unsigned j = 0; j<as -> npages; j++)
                {
                    if (pt[j].paddr == i * PAGE_SIZE + firstpaddr)
                    {
                        if(pt[j].rwx & 2) { // if page is not readonly, swap it out
                            KASSERT(swap_out(&pt[j].paddr) == 0);
                            vm_stats_inc(SWAP_WRITE);
                            pt[j].in_swap = 1;
                        } else pt[j].paddr = 0; //just erase the entry, it will be read again from the ELF if needed
                        pt[j].in_mem = 0; //update its info in the page table
                        tlb_invalidate_vaddr(pt[j].vaddr); //invalidate the entry in the TLB
                        addr = i ;
                        break; //swaps out first page found
                    }
                }
                lock_release(as->pt_lock);
            } else { //we swapped out already 9MB
                panic("Not enough memory in Swap File");
            }
        }
    } 
    return addr * PAGE_SIZE + firstpaddr;
}

static paddr_t getMultiplePages(unsigned npages, bool is_reserved, struct addrspace* as)
{
	paddr_t addr;
	unsigned i,first = 0;
    bool found = false;
    
    spinlock_acquire(&coremap_lock);

    if(! _isCoremapActive()) {
        spinlock_release(&coremap_lock);
        return 0;
    }
        
    for (i = 0; i < coremapSize; i++)
    {
        if (!CUSED(i) && !CRES(i))
        {
            if(i == 0 || CUSED(i-1) || CRES(i-1))
                first = i;
            if(i - first + 1 >= npages) {
                found = true;
                break;
            }
        }
    }

	if (found)
	{
        coremap[first].allocpages = npages;
		for (i = first; i < first + npages; i++)
		{
            set_coreentry(i,PADDR_TO_KVADDR(i*PAGE_SIZE+firstpaddr),is_reserved, as);
		}
		addr = first ;
	}
	else
	{
        panic("Multiple page swapping is not required for on demand page loading");
	}

    spinlock_release(&coremap_lock);

	return addr * PAGE_SIZE + firstpaddr;
}

paddr_t getPages(int npages, vaddr_t vaddr, struct addrspace* as) {
    return npages > 1 ? getMultiplePages(npages,false,as) : getPage(false,vaddr,as);
}

void freepages(paddr_t paddr) {
    if(paddr < firstpaddr) {
        kprintf("trying to free kernel memory\n");
                return;

    }
    unsigned i = (paddr-firstpaddr) / PAGE_SIZE;
    spinlock_acquire(&coremap_lock);
    unsigned npages = coremap[i].allocpages;
    coremap[i].allocpages = 0;
    for(unsigned j = i; j < i + npages; j++)
        set_empty(j);
    spinlock_release(&coremap_lock);
}

static unsigned getAvailableRam() {
    spinlock_acquire(&coremap_lock);
    unsigned ret = coremapSize - ram_used;
    spinlock_release(&coremap_lock);
    return ret;
}

paddr_t ptAlloc(unsigned npages)
{   
    unsigned numpages = DIVROUNDUP(sizeof(pt_entry)*npages,PAGE_SIZE);
    
    if (getAvailableRam() > numpages)
        return (numpages > 1 ? getMultiplePages(numpages,true,NULL) : getPage(true,0,NULL));
    else
        panic("No available space for pageTable in RAM\n");
        return 0;
}

