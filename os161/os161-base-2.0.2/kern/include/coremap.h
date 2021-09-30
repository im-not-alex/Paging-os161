#ifndef _COREMAP_H_
#define _COREMAP_H_
#include <mips/types.h>
#include <addrspace.h>
// #define CGETVPN(i) ((uint32_t)(coremap[(i)].vaddr >> 12))       //get virtual page NUMBER (not address)
// #define CGETVA(i) ((uint32_t)CGETVPN((i))*4096)          //returns virtual address of frame at index a
// #define CENTRY_GET_PID(a) ((uint32_t)((a)&0xfff) >> 2) //returns owner process' id
#define CRES(i) ((int)(coremap[(i)].vaddr & 0x3) && (0x1))                     //tells if the frame is reserved or not
#define CUSED(i) ((int)(coremap[(i)].vaddr & 0x2) && (0x2))            //tells if the frame is valid or not

typedef struct c_entry {
    vaddr_t vaddr; 
    struct addrspace *as; //useful when doing as_destroy
    uint32_t allocpages; //useful when allocating multiple pages at once
}c_entry;
c_entry *coremap;

void coremap_init(void);
int isCoremapActive(void);
paddr_t getPages(int npages, vaddr_t vaddr, struct addrspace* as);
void freeAs(struct addrspace *as);
void freepages(paddr_t paddr);
paddr_t ptAlloc(unsigned npages);
#endif