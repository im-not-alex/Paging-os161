/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include "opt-paging.h"

#if OPT_PAGING

#include <cpu.h>
#include <mips/spinlock.h>
#include <mips/tlb.h>
#include <current.h>
#include <elf.h>
#include <vfs.h>
#include <coremap.h>
#include <swapfile.h>
#include <vm_tlb.h>
#include <vmstats.h>
#include <kern/fcntl.h>
#include <synch.h>
#define STACKPAGES 18

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

static void can_sleep(void)
{
	if (CURCPU_EXISTS())
	{
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		KASSERT(curthread->t_in_interrupt == 0);
	}
}

/*
 *    as_create - create a new empty address space. You need to make
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
*/

struct addrspace *
as_create(char *prog_name, int *retVal)
{
	struct addrspace *as;
	can_sleep();
	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL)
	{
		*retVal = ENOMEM;
		return NULL;
	}
	/* Open the file. */
	if(vfs_open(prog_name, O_RDONLY, 0, &(as->v))) {
		*retVal = ENOENT;
		return NULL;
	}
	as->progname = kstrdup(prog_name);
	as->as_segment = NULL;
	as->as_pt = NULL;
	as->pt_lock = lock_create("PT_lock");
	if(as->pt_lock  == NULL) {
		kprintf("Page Table Lock was not created succesfully\n");
		vfs_close(as->v);
		kfree(as);
		*retVal = ENOMEM;
		return NULL;
	}
	*retVal = 0;
	return as;
}

/*
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 */
int as_copy(struct addrspace *old, struct addrspace **ret)
{
	int err;
	
	struct addrspace *newas = as_create(old->progname, &err);
	if (newas == NULL)
	{
		return err;
	}
	lock_acquire(old->pt_lock);
	segment_t *seg, *new_seg, *curseg;
	
	for (seg = old->as_segment; seg != NULL; seg = seg->next)
	{
		new_seg = (segment_t *)kmalloc(sizeof(segment_t));
		if (new_seg == NULL)
			return ENOMEM; 
		
		new_seg->start = seg->start;
		new_seg->size = seg->size;
		new_seg->filesize = seg->filesize;
		new_seg->npages = seg->npages;
		new_seg->rwx = seg->rwx;
		new_seg->offset = seg->offset;
		new_seg->initoffset = seg->initoffset;
		new_seg->next = NULL;

		if (newas->as_segment == NULL)
		{
			newas->as_segment = new_seg;
		}
		else
		{
			curseg->next = new_seg;
		}
		curseg = new_seg;
	}

	newas->npages = old->npages;
	
	paddr_t ptloc = ptAlloc(newas->npages);
	if(ptloc)
		newas->as_pt = (pt_entry *)PADDR_TO_KVADDR(ptloc);
	else
		return ENOMEM;

	pt_entry *tmp;
	paddr_t paddr;

	for(unsigned i = 0 ; i < newas->npages ; i++)
	{
		tmp = &newas->as_pt[i];
		tmp->in_mem = 0;
		tmp->in_swap = 0;
		tmp->rwx = old->as_pt[i].rwx;
		tmp->vaddr = old->as_pt[i].vaddr;
		paddr = 0;
		if(old->as_pt[i].in_swap) {
			paddr = getPages(1,tmp->vaddr,newas);
			err = swap_in(&(tmp->paddr), paddr, false);
			if(err)
				return err;
			tmp->in_swap = 0;
			tmp->in_mem = 1;

		} else if(old->as_pt[i].in_mem) {
			paddr = getPages(1,tmp->vaddr,newas);
			memmove((void *)PADDR_TO_KVADDR(paddr),(const void *)PADDR_TO_KVADDR(old->as_pt[i].paddr),PAGE_SIZE);
			tmp->paddr = paddr;
			tmp->in_mem = 1;
		} else {
			tmp->paddr = 0;
		}

	}
	*ret = newas;
	lock_release(old->pt_lock);
	return 0;
}

/*
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 */
void as_destroy(struct addrspace *as)
{
	can_sleep();
	
	vfs_close(as->v);

	segment_t *seg, *seg_new;
	for (seg = as->as_segment, seg_new = as->as_segment; seg_new != NULL; seg = seg_new)
	{
		seg_new = seg->next;
		kfree(seg);
	}
	lock_acquire(as->pt_lock);
	pt_entry *pt = as->as_pt;
	for (unsigned i=0; i<as->npages; i++)
	{
		if (pt[i].in_swap)
			clear_swap(pt[i].paddr);
	}
	lock_release(as->pt_lock);
	lock_destroy(as->pt_lock);
	freepages((paddr_t)as->as_pt - MIPS_KSEG0);
	freeAs(as);
	kfree(as);
}

/*
 *    as_activate - make curproc's address space the one currently
 *                "seen" by the processor.
 */
void as_activate(void)
{
	struct addrspace *as;
	as = proc_getas();
	if (as == NULL)
	{
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/* Invalidate the TLB */
	tlb_invalidate();
}

/*
 *    as_deactivate - unload curproc's address space so it isn't
 *                currently "seen" by the processor. This is used to
 *                avoid potentially "seeing" it while it's being
 *                destroyed.
 */
void as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 *    as_define_region - set up a region of memory within the address
 *                space.
*/
/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize, size_t filesize,
					 int readable, int writeable, int executable, off_t offset)
{
	segment_t *seg = (segment_t *)kmalloc(sizeof(segment_t));
	if (seg == NULL)
		return ENOMEM; 

	/*allign the region, first the base*/
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	seg->next = NULL;
	seg->start = vaddr & PAGE_FRAME;
	seg->size = memsize;
	seg->filesize = filesize;
	seg->npages = memsize / PAGE_SIZE;
	seg->rwx = (readable & 0x4) | (writeable & 0x2) | (executable & 0x1);
	seg->offset = offset;
	seg->initoffset = vaddr & ~(vaddr_t)PAGE_FRAME;
	if (as->as_segment == NULL)
	{
		as->as_segment = seg;
	}
	else
	{
		segment_t *curseg;
		for (curseg = as->as_segment; curseg->next != NULL; curseg = curseg->next);
		curseg->next = seg;
	}

	return 0;
}

/*
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 */
int as_prepare_load(struct addrspace *as)
{
	size_t size = STACKPAGES * PAGE_SIZE;
	if (as_define_region(as, USERSTACK - size, size, size, PF_R, PF_W, 0, 0) != 0)
		return ENOMEM;
	segment_t *curseg;	
	vaddr_t v;
	as->npages = 0;
	for (curseg = as->as_segment; curseg != NULL; curseg = curseg->next)
	{
		as->npages+=curseg->npages;
	}

	lock_acquire(as->pt_lock);

	paddr_t ptloc = ptAlloc(as->npages);
	
	if(ptloc)
		as->as_pt = (pt_entry *)PADDR_TO_KVADDR(ptloc);
	else
		return ENOMEM;
	bzero(as->as_pt, sizeof(pt_entry)* as->npages);
	int j = 0;
	pt_entry *pt = as->as_pt;

	for (curseg = as->as_segment; curseg != NULL; curseg = curseg->next)
	{
		v = curseg->start;

		for (size_t i = 0; i < curseg->npages; i++)
		{
			pt[j].paddr = 0;
			pt[j].vaddr = v;
			pt[j].in_mem = 0;
			pt[j].in_swap = 0;
			v += PAGE_SIZE;
			pt[j].rwx = curseg->rwx;
			j++;
		}
	}
	lock_release(as->pt_lock);
	return 0;
}

/*
 *    as_complete_load - this is called when loading from an executable  
 *                is complete.
 */
int as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

/*
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
*/
int as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}


static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

void vm_bootstrap(void)
{
	vm_stats_init(); //initializes the counters for the stats to be printed when the vm is shut down
	swapmap_init();  //initializes the array for the swapmap, its dimension depends on SWAP_SIZE
	coremap_init();  /*initializes the array for the coremap, its size depends on the RAM
					   size(address of the last free physical page) and the address of the
					   first free physical page; the difference between these two is then
					   divided by PAGE_SIZE to get the number of entries in the coremap*/
}

static paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	/* try freed pages first */
	addr = getPages((int)npages, 0, NULL);
	
	if (addr == 0)
	{
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
	}
	return addr;
}

vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;

	can_sleep();
	pa = getppages(npages);
	if (pa == 0)
	{
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void free_kpages(vaddr_t addr)
{
	if (isCoremapActive())
	{
		paddr_t paddr;
		int result = getPageVtoP(addr, &paddr);
		if(result > 0) {
			freepages(paddr);
		} else if(result == 0)  {
			paddr_t paddr = addr - MIPS_KSEG0;
			freepages(paddr);
		} else {
			panic("vaddr 0x%x not found in the page table\n", addr);
		} 
	}
}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct addrspace *as;
	vm_stats_inc(TLB_FAULT);

	DEBUG(DB_VM, "#VM FAULT, type: %d, vaddr: %d\n", faulttype, faultaddress);

	faultaddress &= PAGE_FRAME;

	if (curproc == NULL)
	{
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL)
	{
		/*
		 * No address space set up. This is probably a kernel
		 * fault early in boot. Return EFAULT so as to panic
		 * instead of getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	DEBUG(DB_VM, "vm: fault: 0x%x\n", faultaddress);
	int result;
	
	
	segment_t *seg = as->as_segment;
	unsigned np = 0;

	//Search for the first vaddress of the segment of interest
	while (seg->next != NULL)
	{
		if (faultaddress < seg->next->start)
			break;
		np += seg->npages;
		seg = seg->next;
	}

	//Search for the actual page in the page table
	unsigned i;
	lock_acquire(as->pt_lock);
	pt_entry *pt = as->as_pt;
	for(i = np; i < (np + seg->npages); i++) {
		if(pt[i].vaddr == faultaddress){
			break;}
	}
	if (i == np + seg->npages)
	{
		lock_release(as->pt_lock);
		panic("pt with addr 0x%x not initialized!\n", faultaddress);
		return 0;
	}
	switch (faulttype)
	{
	case VM_FAULT_READONLY:
	{
		/* We always create pages read-write, so we can't get this */
		lock_release(as->pt_lock);
		panic("vm: got VM_FAULT_READONLY at address 0x%x\n", faultaddress); //signal process to terminate.
	}
	break;
	case VM_FAULT_READ:
	case VM_FAULT_WRITE:
	{
		if (pt[i].in_mem == 0)
		{
			if (pt[i].in_swap) //if the page is in the swapfile, get it from there
			{
				lock_release(as->pt_lock);
				paddr_t new_paddr = getPages(1,faultaddress,as);
				lock_acquire(as->pt_lock);
				result = swap_in(&(pt[i].paddr), new_paddr, true);
				pt[i].in_swap = 0; //update the page information in the page table
				vm_stats_inc(SWAP_READ);
				vm_stats_inc(PAGE_FAULT_DISK);
			}
			else
			{ //page is not in swap, load it from the ELF on-demand
				if (pt[i].paddr == 0) {
					lock_release(as->pt_lock);
					pt[i].paddr = getPages(1,faultaddress,as);
					lock_acquire(as->pt_lock);
				} if (seg->next != NULL){
					result = load_elf_ondemand(seg, pt[i].paddr, faultaddress);
					vm_stats_inc(ELF_READ);
					vm_stats_inc(PAGE_FAULT_DISK);
				} else { //the required page is in kernel
					bzero((void*)PADDR_TO_KVADDR(pt[i].paddr),PAGE_SIZE);
					vm_stats_inc(PAGE_FAULT_ZEROED);
				}
			}
			pt[i].in_mem = 1; //update page's information
		}
		else {
			vm_stats_inc(TLB_RELOAD);
		}
	}
	break;
	default:
		lock_release(as->pt_lock);
		return EINVAL;
	}
	result = tlb_loadentry(faultaddress, pt[i].paddr, !(pt[i].rwx & 2)); //load the new page in the TLB
	lock_release(as->pt_lock);
	return result;
}

int checkcanLock() {
  if (CURCPU_EXISTS())
	{
		if (curcpu->c_spinlocks == 0 && curthread->t_in_interrupt == 0)
      return 0;
	}
  return 1;
}

#endif
