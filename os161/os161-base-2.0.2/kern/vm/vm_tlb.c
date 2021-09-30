#include <types.h>
#include <lib.h>
#include <mips/types.h>
#include <vm.h>
#include <mips/tlb.h>
#include <vmstats.h>
#include <kern/errno.h>
#include <vm_tlb.h>
#include <spl.h>
static int tlb_full = 0;

int tlb_loadentry(vaddr_t faultaddress, paddr_t paddr, bool readOnly)
{
	int i,spl;
;
	uint32_t ehi, elo;

	spl = splhigh();

	if (tlb_full == 0)
	{
		for (i = 0; i < NUM_TLB; i++)
		{
			tlb_read(&ehi, &elo, i);
			if (!(elo & TLBLO_VALID))
			{
				ehi = faultaddress;
				elo = paddr | (readOnly ? 0 : TLBLO_DIRTY) | TLBLO_VALID;
				tlb_write(ehi, elo, i);
				splx(spl);
				vm_stats_inc(TLB_FAULT_WITH_FREE);
				return 0;
			}
		}
		tlb_full = 1;
	}
	if (tlb_full)
	{
		ehi = faultaddress;
		elo = paddr | (readOnly ? 0 : TLBLO_DIRTY) | TLBLO_VALID;
		int k = tlb_get_rr_victim();
		
		tlb_write(ehi, elo, k);
		splx(spl);
		vm_stats_inc(TLB_FAULT_WITH_REPLACE);
		return 0;
	}

	//Should never get here
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return ENOMEM;
}

int tlb_get_rr_victim(void)
{
	int victim;
	static unsigned int next_victim = 0;
	victim = next_victim;
	next_victim = (next_victim + 1) % NUM_TLB;
	return victim;
}

void vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void tlb_invalidate() {
	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	/* Invalidate the TLB */
		
    for (unsigned i = 0; i < NUM_TLB; i++)
	{
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i); //set each entry in the TLB as invalid
	}
	splx(spl);
	vm_stats_inc(TLB_INVALIDATION);
}

void tlb_invalidate_vaddr(vaddr_t vaddr) {
	int spl = splhigh();
	vaddr &= PAGE_FRAME;
	int i;
	if((i = tlb_probe(vaddr,0)) >= 0)
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);

	splx(spl);
}