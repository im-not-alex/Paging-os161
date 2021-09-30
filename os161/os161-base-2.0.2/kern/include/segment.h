#ifndef _SEGMENT_H_
#define _SEGMENT_H_

#include <mips/types.h>

typedef struct segment_t
{
        vaddr_t start;
        size_t size;
        size_t filesize;
        size_t npages;
        unsigned rwx : 3; //read-write-execute flags -> used to set them in the page table entries
        off_t offset; //offset in the ELF file
        size_t initoffset;      //this and all of the above are used to read from the
                                //ELF file when doing on-demand page loading
        struct segment_t *next; //points to the next segment in the list
} segment_t;

#endif