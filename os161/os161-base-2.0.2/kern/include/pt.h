#ifndef _PT_H_
#define _PT_H_

#include <types.h>
#include <mips/types.h>

typedef struct pt_entry {
    paddr_t paddr;
	vaddr_t vaddr;
	bool in_mem : 1;
	bool in_swap : 1;
	unsigned rwx : 3; //read, write, execute flags
}pt_entry;

int getPageV (vaddr_t vaddr, unsigned *j);
int getPageP (paddr_t paddr, unsigned *j); 
int getPageVtoP (vaddr_t vaddr, paddr_t *paddr);
int getPagePtoV (paddr_t paddr, vaddr_t *vaddr);
#endif