#ifndef _SWAPFILE_H_
#define _SWAPFILE_H_ 

#include <vnode.h>
#include <addrspace.h>
#include <mips/types.h>


#define SWAP_VALID   0x00000200
void swapmap_init(void);
void close_swapfile(void);

int swap_in(paddr_t *swap_paddr, paddr_t ram_paddr, bool toRemove);
int swap_out(paddr_t *paddr);
void clear_swap(paddr_t paddr);
unsigned getAvailableSwap(void);
#endif