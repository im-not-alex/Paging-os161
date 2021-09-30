<h2>Project C1 : PAGING</h2>
<center><h3>Programmazione di Sistema, AY 2019/2020
	<br/>Nicolas Fergnani (<a href="https://github.com/NicoF96" target="_blank">@NicoF96</a>) &nbsp;&nbsp;&nbsp;&nbsp; Alexandru Valentin Onica (<a href="https://github.com/im-not-alex" target="_blank">@im-not-alex</a>)</h3></center><br/>

# Data Structures

## Coremap
The coremap maps the available physical memory after boot. <br/>
The size of the coremap is determined at bootstrap and it is obtained dividing the difference between the last and first physical addresses of the RAM by the page size. This will deny any future use of the function `ram_stealmem()`.
The coremap is an array of `c_entry`;
```c
typedef struct c_entry {
    vaddr_t vaddr; 
    struct addrspace *as; //useful when doing as_destroy
    uint32_t allocpages; //useful when allocating multiple pages at once
}c_entry;
```

## Page Table

We assume one level page table.
<br/>
The size of the page table is determined by the total number of the pages required by the segments of the address space.<br/>
The page table is an array of `pt_entry`, each of which keeps track of the virtual and physical addresses pairs, storing information on the location of the data and its access permissions.
It is allocated in `as_prepare_load()`, using the support function `ptAlloc`, which avoids pages reserved for the page table to be swapped.<br/>
The virtual addresses in the page table are then set in an incremental way, starting from the base virtual address of each segment, in steps of `PAGE_SIZE`.

```c
typedef struct pt_entry {
    paddr_t paddr;
	vaddr_t vaddr;
	bool in_mem : 1;
	bool in_swap : 1;
	unsigned rwx : 3; //read, write, execute flags
}pt_entry;
```

## Segments

The segments are allocated according to the information present in the ELF file, the last segment will always be the stack.
They are initialized in `load_elf()`, which uses `as_define region()` to also connect them in a linked list.
The stack segment is defined in `as_prepare_load()` and placed as the last segment of the list.

```c
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
```


## Address Space

```c
struct addrspace 
{
    pt_entry* as_pt; //array of the page table entries
	segment_t* as_segment; //linked list of the segments in the address space
	struct vnode *v; //points to the ELF, used to do on-demand page loading
	unsigned npages; //total number of pages in the page table
    struct lock *pt_lock;
    char* progname; //used to save the ELF name to be passed during as_copy
};
```

## Swap Map
The swapmap is a *bitmap*, used to keep track of which parts of the *SWAPFILE* are currently being used.<br/>
When the coremap max capacity is reached, one victim, chosen via the `swapvictim()` function, is evicted and swapped out in the *SWAPFILE*, which has a size of 9MB, if the victim is not read only; otherwise it is discarded and read again from the *ELF* file, when needed. </br>
The pointer to the *vnode* of the swapfile is declared in `swapfile.c` and defined in `vm_bootstrap()`.
After swapping out a page, its information in the page table is updated, so that it can be swapped in later on.

<br/><br/>


# TLB Management

## Context Switch
On switching threads we call `as_activate()`, which, calling the function `tlb_invalidate()` invalidates all the entries in the TLB, as done in `DUMBVM`.

```c
void tlb_invalidate() {
	int spl = splhigh();
    for (unsigned i = 0; i < NUM_TLB; i++)
	{
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i); //set each entry in the TLB as invalid
	}
	splx(spl);
	[..]
}
```

## TLB Fault

TLB faults are handled with `vm_fault`, which returns 0 on success, panics in case some problems arise, and returns `ENOMEM` in case the TLB write fails and `EFAULT` in case something is wrong with the addressspace or the current process.<br/>
If the required frame has been swapped out it is recovered from the *SWAPFILE*. Otherwise, if it is read only, not from the stack region and not yet loaded from the *ELF* file, with the function `load_elf_ondemand(...)` we load it. After either swapping in or loading from ELF a frame, we update its informations in the page table.
In the end we write into the tlb the *vaddr* - *paddr* pair with the function `tlb_loadentry(...)`.
  
 ```c
int vm_fault(int faulttype, vaddr_t faultaddress)
{
	[...]
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
	pt_entry *pt = as->as_pt;
	for(i = np; i < (np + seg->npages); i++) {
		if(pt[i].vaddr == faultaddress){
			break;}
	}
	if (i == np + seg->npages)
	{
		[...]
		panic("pt with addr 0x%x not initialized!\n", faultaddress);
		return 0;
	}
	switch (faulttype)
	{
	case VM_FAULT_READONLY:
	{
		/* We always create pages read-write, so we can't get this */
		[...]
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
                paddr_t new_paddr = getPages(1,faultaddress,as);
                result = swap_in(&(pt[i].paddr), new_paddr, true);
                pt[i].in_swap = 0; //update the page information in the page table
                [...]
            }
            else
            { //page is not in swap, load it from the ELF on-demand
                if (pt[i].paddr == 0) {
                    pt[i].paddr = getPages(1,faultaddress,as);
                } if (seg->next != NULL){
                    result = load_elf_ondemand(seg, pt[i].paddr, faultaddress);
                    [...]
                } else { //the required page is in kernel
                    bzero((void*)PADDR_TO_KVADDR(pt[i].paddr),PAGE_SIZE);
                    [...]
                }
            }
            pt[i].in_mem = 1; //update page's information
        }
        else {
            [...]
        }
    }
    break;
    [...]
    }
    result = tlb_loadentry(faultaddress, pt[i].paddr, !(pt[i].rwx & 2)); //load the new page in the TLB
    [...]
    return result;
}
```

## TLB Load Entry
Inserts a new entry in the TLB. If it has free space, it loads the entry in the first free slot.
Otherwise, if the TLB is full, it overwrites one of the already present entries, choosing the victim with the `tlb_get_rr_victim()` function.
Returns 0 if it succeeds, ENOMEM when it fails, but should never return the latter since the TLB write when the TLB is full is handled.
```c
int tlb_loadentry(vaddr_t faultaddress, paddr_t paddr, bool readOnly)
{
	int i,spl;
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
```

# Page Table Management

The following function returns a free RAM frame address, either by returning a free frame, or evicting one, potentially swapping it out.
It is used in on-demand page loading; when a page has to be loaded this function, it provides a physical address for that page.
```c
static paddr_t getPage(bool is_reserved, vaddr_t vaddr, struct addrspace* as)
{
    paddr_t addr;
    unsigned i;

    if(! _isCoremapActive()) {
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
            } else { //we swapped out already 9MB
                panic("Not enough memory in Swap File");
            }
        }
    } 
    return addr * PAGE_SIZE + firstpaddr;
}

```
<br/>

# Swap Management

The main data structure used for swap management is the `swapMap`. It uses the `lhd2` disk, which corresponds to `SWAPFILE` in the host system. `swapMap` size is defined at boot reading the size of the `SWAPFILE`  with the help of `VOP_STAT`.
```c
int swap_out(paddr_t *paddr)
{
    [...]
    if (used == swapFileSize) //if so, there is no free space in the swap file
        return ENOMEM;
    unsigned i;

     int err = bitmap_alloc(swapMap, &i); /*searches for a free slot in the bitmap, if there is 
                                           *one the index of it is put in i, the relative entry 
                                           is set as filled and the return value is 0, otherwise an error is returned.*/
     if(err) {
         [...]
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
    [...]
    return 0;
}

int swap_in(paddr_t *swap_paddr, paddr_t ram_paddr, bool toRemove)
{ //ram_paddr previously set with swap_out
    [...]
    int result = load_segment(NULL, swapFile, (off_t)(*swap_paddr * PAGE_SIZE), ram_paddr, PAGE_SIZE, PAGE_SIZE, 0); //read the page from the swapfile
    if(result)
        return result;
    
    KASSERT(bitmap_isset(swapMap, *swap_paddr) != 0);
    if(toRemove) {
        bitmap_unmark(swapMap, *swap_paddr); //sets the swapmap entry which has the same index as the one stored in *swap_paddr as free
        used -= 1;
    }
    *swap_paddr = ram_paddr; //update the paddr with the new one
    [...]
    return result;
}
```


# Bootstrapping


```c
void vm_bootstrap(void)
{
	vm_stats_init(); //initializes the counters for the stats to be printed when the vm is shut down
	swapmap_init();  //initializes the array for the swapmap, its dimension depends on SWAP_SIZE
	coremap_init();  /*initializes the array for the coremap, its size depends on the RAM
					   size(address of the last free physical page) and the address of the
					   first free physical page; the difference between these two is then
					   divided by PAGE_SIZE to get the number of entries in the coremap*/
}

void coremap_init(void)
{

    lastpaddr = ram_getsize();
    firstpaddr = ram_getfirstfree();
    firstpaddr = ALLIGN_NEXT_ADDR(firstpaddr);
    
    coremapSize = (lastpaddr - firstpaddr) / PAGE_SIZE;

    unsigned csize = DIVROUNDUP(coremapSize * sizeof(c_entry), PAGE_SIZE); //size of page table to reference the total pages
    coremapSize -= csize;                                             // to account for the pages taken up by the allocation of the coremap
    coremap = (c_entry *)PADDR_TO_KVADDR(firstpaddr);                 //this is the starting address of the coremap now, it represents its free entry
    
    bzero(coremap, sizeof(c_entry) * coremapSize);
    firstpaddr += csize * PAGE_SIZE; //this represents the first free entry of the ram

    spinlock_acquire(&coremap_lock);
    coremapActive = 1; //static int defined and used in coremap.c
    spinlock_release(&coremap_lock);
}

struct bitmap *swapMap; //bitmap defined and used in swapfile.c
const char* swapname = "lhd2"; //this is SWAPFILE, it is automatically created while booting sys161 (we added an instruction in sys161.conf)
struct vnode *swapFile;

void swapmap_init()
{
    open_swapfile();
    swapMap = bitmap_create(swapFileSize / PAGE_SIZE);
    KASSERT(swapMap!=NULL);
    swap_lock = lock_create("SWAP_lock");
}

void open_swapfile()
{
    int result;
    struct stat stats;
    result = vfs_swapon(swapname, &swapFile); //opens the swapfile
    KASSERT(result == 0 && swapFile != NULL);
    result = VOP_STAT(swapFile, &stats); 
    KASSERT(result == 0 && stats.st_size % PAGE_SIZE == 0);
    swapFileSize = stats.st_size; //size of the swapmap bitmap
    kprintf("swapsize %d\n",(int)swapFileSize);
}

```
---
# Statistics
All of the required statistics are handled with the functions defined in `vmstats.c`. To keep track of them we used an array of integers, in which every entry represents one type of statistic. The statistic types names are defined in an enum in `vmstats.h`.
The statistic counters are initialized in `vm_bootstrap` calling the function `vm_init`. To increase one specific statistic count the `vm_stats_inc` function is used and in order to print them, the function `vm_stats_print` is called the `shutdown` function in `main.c`.
```c
static unsigned int counters[STATS_TOT]; //STATS_TOT is defined equalto 10 in the header file

void
vm_stats_init()
{
    for (unsigned i=0; i<STATS_TOT; i++) {
        counters[i] = 0;
    }
    [...]
}

static void
_vm_stats_inc(unsigned int index)
{
  KASSERT(index < STATS_TOT);
  counters[index]++;
}
void
vm_stats_inc(unsigned int index)
{
  if(!checkcanLock()) {
  [...]
  _vm_stats_inc(index);
  [...]
  } else _vm_stats_inc(index);
}
```
<br/>
<br/>

# Testing
In order to test the vm we used the default tests provided with os161. In particular, we used *palin* to check the corrent implementation of the address space, coremap, page table and on-demand page loading. Then, once we succeded in implementing those, we used *sort*, *matmult* and *huge* to check the correct implementation of the swapping. We found the misalignment of the data segment in the *sort* test very useful for correcting a mistake we made: loading the segments in the default way does not take into account the leading zeroes of some virtual addresses.
