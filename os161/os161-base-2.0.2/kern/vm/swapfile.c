#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <spinlock.h>
#include <mips/tlb.h>
#include <coremap.h>
#include <cpu.h>
#include <pt.h>
#include <swapfile.h>
#include <vfs.h>
#include <kern/iovec.h>
#include <uio.h>
#include <spl.h>
#include <stat.h>
#include <bitmap.h>
#include <synch.h>


struct vnode *swapFile;
off_t swapFileSize;
struct bitmap *swapMap;
static struct lock *swap_lock;
const char *swapname = "lhd2"; //this is SWAPFILE, it is automatically created while booting sys161 (we added an instruction in sys161.conf)
unsigned int short used = 0;


static void open_swapfile()
{
    int result;
    struct stat stats;
    result = vfs_swapon(swapname, &swapFile); //opens the swapfile
    KASSERT(result == 0 && swapFile != NULL);
    result = VOP_STAT(swapFile, &stats); 
    KASSERT(result == 0 && stats.st_size % PAGE_SIZE == 0);
    swapFileSize = stats.st_size; //size of the swapmap bitmap
    kprintf("SwapFile size : %d\n",(int)swapFileSize);
}


void swapmap_init()
{   
    open_swapfile();
    swapMap = bitmap_create(swapFileSize / PAGE_SIZE);
    KASSERT(swapMap!=NULL);
    swap_lock = lock_create("SWAP_lock");
    if(swap_lock == NULL)
        panic("Swap lock was not created succesfully\n");
}


void close_swapfile()
{
    lock_acquire(swap_lock);
    int result = vfs_swapoff(swapname);
    KASSERT(result == 0);
    bitmap_destroy(swapMap);
    lock_release(swap_lock);

}

int swap_in(paddr_t *swap_paddr, paddr_t ram_paddr, bool toRemove)
{ //ram_paddr settato precedentemente con swap_out
    lock_acquire(swap_lock);
    int result = load_segment(NULL, swapFile, (off_t)(*swap_paddr * PAGE_SIZE), ram_paddr, PAGE_SIZE, PAGE_SIZE, 0); //read the page from the swapfile
    if(result)
        return result;
    
    KASSERT(bitmap_isset(swapMap, *swap_paddr) != 0);
    if(toRemove) {
        bitmap_unmark(swapMap, *swap_paddr); //sets the swapmap entry which has the same index as the one stored in *swap_paddr as free
        used -= 1;
    }
    *swap_paddr = ram_paddr; //update the paddr with the new one
    lock_release(swap_lock);
    return result;
}

int swap_out(paddr_t *paddr)
{

    struct iovec iov;
    struct uio u;
    lock_acquire(swap_lock);
    if (used == swapFileSize) //if so, there is no free space in the swap file
        return ENOMEM;
    unsigned i;

     int err = bitmap_alloc(swapMap, &i); /*searches for a free slot in the bitmap, if there is 
                                           *one the index of it is put in i, the relative entry 
                                           is set as filled and the return value is 0, otherwise an error is returned.*/
     if(err) {
         lock_release(swap_lock);
         return err;
     }
     
    iov.iov_ubase = (void *)PADDR_TO_KVADDR(*paddr);
    iov.iov_len = PAGE_SIZE; // length of the memory space

    u.uio_iov = &iov;
    u.uio_iovcnt = 1;
    u.uio_resid = PAGE_SIZE; // amount to read from the file
    u.uio_offset = i * PAGE_SIZE;
    u.uio_segflg = UIO_SYSSPACE;
    u.uio_rw = UIO_WRITE;
    u.uio_space = NULL;

    int result = VOP_WRITE(swapFile, &u); //write to swapfile
    KASSERT(result == 0);

    *paddr = i;
    used += 1;
    lock_release(swap_lock);
    return 0;
}

void clear_swap(paddr_t paddr)
{
    lock_acquire(swap_lock);
    bitmap_unmark(swapMap, paddr);
    used -= 1;
    lock_release(swap_lock);
}

unsigned getAvailableSwap()
{
    lock_acquire(swap_lock);
    unsigned sz = swapFileSize - used;
    lock_release(swap_lock);
    return sz;
}