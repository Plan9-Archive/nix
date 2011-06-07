/*
 * Size memory and create the kernel page-tables on the fly while doing so.
 * Called from main(), this code should only be run by the bootstrap processor.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "amd64.h"

enum {
	MemUPA		= 0,		/* unbacked physical address */
	MemRAM		= 1,		/* physical memory */
	MemUMB		= 2,		/* upper memory block (<16MiB) */
	MemACPI		= 3,		/* acpi Reclaim Memory */
	NMemType,

	MemMinMiB	= 4,		/* minimum physical memory (<=4MiB) */
	MemMaxMiB	= (128),	/* maximum physical memory to check */

	NMemBase	= 10,
};

typedef struct Map Map;
struct Map {
	uintptr	addr;
	uvlong	size;
};

typedef struct RMap RMap;
struct RMap {
	char*	name;
	Map*	map;
	Map*	mapend;

	Lock;
};

static Map mapupa[16];
static RMap rmapupa = {
	"unallocated unbacked physical memory",
	mapupa,
	&mapupa[nelem(mapupa)-1],
};

static Map xmapupa[16];
static RMap xrmapupa = {
	"unbacked physical memory",
	xmapupa,
	&xmapupa[nelem(xmapupa)-1],
};

static Map mapram[16];
static RMap rmapram = {
	"physical memory",
	mapram,
	&mapram[nelem(mapram)-1],
};

static Map mapacpi[16];
static RMap rmapacpi = {
	"ACPI Reclaim memory",
	mapacpi,
	&mapacpi[nelem(mapacpi)-1],
};

void
mapprint(RMap* rmap)
{
	Map *mp;

	print("%s\n", rmap->name);
	for(mp = rmap->map; mp->size; mp++){
		print("\t%#16.16p %#16.16p %#16.16p\n",
			mp->addr, mp->size, mp->addr+mp->size);
	}
}

void
memdebug(void)
{
	uintptr maxpa, maxpa1, maxpa2;

	if(!DBGFLG)
		return;

	maxpa = (nvramread(0x18)<<8)|nvramread(0x17);
	maxpa1 = (nvramread(0x31)<<8)|nvramread(0x30);
	maxpa2 = (nvramread(0x16)<<8)|nvramread(0x15);
	print("maxpa = %#p -> %#p, maxpa1 = %#p maxpa2 = %#p\n",
		maxpa, MiB+maxpa*KiB, maxpa1, maxpa2);

	mapprint(&rmapram);
	mapprint(&rmapupa);
}

void
mapfree(RMap* rmap, uintptr addr, usize size)
{
	Map *mp;
	uintptr t;

	lock(rmap);
	for(mp = rmap->map; mp->addr <= addr && mp->size; mp++)
		;

	if(mp > rmap->map && (mp-1)->addr+(mp-1)->size == addr){
		(mp-1)->size += size;
		if(addr+size == mp->addr){
			(mp-1)->size += mp->size;
			while(mp->size){
				mp++;
				(mp-1)->addr = mp->addr;
				(mp-1)->size = mp->size;
			}
		}
	}
	else{
		if(addr+size == mp->addr && mp->size){
			mp->addr -= size;
			mp->size += size;
		}
		else do{
			if(mp >= rmap->mapend){
				print("mapfree: %s: losing %#p, %lud\n",
					rmap->name, addr, size);
				break;
			}
			t = mp->addr;
			mp->addr = addr;
			addr = t;
			t = mp->size;
			mp->size = size;
			mp++;
		}while(size = t);
	}
	unlock(rmap);
}

uintptr
mapalloc(RMap* rmap, uintptr addr, usize size, usize align)
{
	Map *mp;
	uintptr maddr, oaddr;

	lock(rmap);
	for(mp = rmap->map; mp->size; mp++){
		maddr = mp->addr;

		if(addr){
			/*
			 * A specific address range has been given:
			 *   if the current map entry is greater then
			 *   the address is not in the map;
			 *   if the current map entry does not overlap
			 *   the beginning of the requested range then
			 *   continue on to the next map entry;
			 *   if the current map entry does not entirely
			 *   contain the requested range then the range
			 *   is not in the map.
			 */
			if(maddr > addr)
				break;
			if(mp->size < addr - maddr)	/* maddr+mp->size < addr, but no overflow */
				continue;
			if(addr - maddr > mp->size - size)	/* addr+size > maddr+mp->size, but no overflow */
				break;
			maddr = addr;
		}

		if(align > 0)
			maddr = ((maddr+align-1)/align)*align;
		if(mp->addr+mp->size-maddr < size)
			continue;

		oaddr = mp->addr;
		mp->addr = maddr+size;
		mp->size -= maddr-oaddr+size;
		if(mp->size == 0){
			do{
				mp++;
				(mp-1)->addr = mp->addr;
			}while((mp-1)->size = mp->size);
		}

		unlock(rmap);
		if(oaddr != maddr)
			mapfree(rmap, oaddr, maddr-oaddr);

		return maddr;
	}
	unlock(rmap);

	return ~0ull;
}

static uintptr
ramptalloc(int)
{
	uintptr pa;

	pa = mapalloc(&rmapram, 0, PTPGSZ, PTPGSZ);
	memset(KADDR(pa), 0, PTPGSZ);

	return pa;
}

static void
ramscan(uvlong maxmem)
{
	Map *mp;
	uvlong size;
	PTE *pml4, *pte;
	uintptr pa, maxpa, va;

	DBG("ramscan %#llux\n", maxmem);
	mapprint(&rmapram);

	/*
	 * The bootstrap code has has created a prototype page
	 * table which maps the first MemMinMiB of physical memory to KZERO.
	 * The page directory is at m->pml4 and the first page of
	 * free memory is the first address in rmapram.
	 * There are lots of assumptions here, any one could break it
	 * in mysterious ways.
	 */
	pml4 = UINT2PTR(m->pml4->va);
	for(mp = rmapram.map; mp->size; mp++){
		/*
		 * Up to MemMinMiB is already set up.
		 */
		if(mp->addr < MemMinMiB*MiB){
			if(mp->addr+mp->size <= MemMinMiB*MiB)
				continue;
			pa = MemMinMiB*MiB;
			size = mp->size - MemMinMiB*MiB-mp->addr;
		}
		else{
			pa = mp->addr;
			size = mp->size;
		}

		/*
		 */
		maxpa = pa+size;
		if(maxpa > maxmem)
			maxpa = maxmem;
		DBG("start: pa %#p maxpa %#p\n", pa, maxpa);
		while(pa < maxpa){
			va = PTR2UINT(KADDR(pa));		/* VA()? */
			if(!(pa & (2*MiB-1)) && maxpa-pa >= 2*MiB){
				pte = mmuwalk(pml4, va, 2, ramptalloc);
				if(pte == nil){
					panic("2*MiB urk!\n");
					for(;;);
				}
				*pte = (PPN(pa))|PtePS|PteRW|PteP;
				pa += 2*MiB;
			}
			else{
				pte = mmuwalk(pml4, va, 1, ramptalloc);
				if(pte == nil){
					panic("4*KiB urk! pa %#p\n", pa);
					for(;;);
				}
				*pte = (PPN(pa))|PteRW|PteP;
				pa += 4*KiB;
			}
		}
		mmuflushtlb(m->pml4->pa);
		DBG("finish: pa %#p maxpa %#p\n", pa, maxpa);
	}
	mapprint(&rmapram);
}

void
meminit(void)
{
	int i;
	Map *mp;
	Confmem *cm;
	uvlong lim, lost, maxmem;
	char *p;

	if(p = getconf("*maxmem"))
		maxmem = strtoull(p, 0, 0);
	else
		maxmem = MemMaxMiB*MiB;

	/*
	 * Set special attributes for memory between 640KiB and 1MiB:
	 *   VGA memory is writethrough;
	 *   BIOS ROM's/UMB's are uncached;
	 * then scan for useful memory.
	 *
	 * Need to set this up with mtrr/pat.
	 */
	mmuflushtlb(m->pml4->pa);

	ramscan(maxmem);

	/*
	 * Set the conf entries describing banks of allocatable memory.
	 *
	 * Conf must go - replace this with something more flexible.
	 */
	lost = 0;
	for(i=0; i<nelem(mapram) && i<nelem(conf.mem) && lost == 0ull; i++){
		mp = &rmapram.map[i];
		if(mp->addr >= maxmem)
			break;
		cm = &conf.mem[i];
		cm->base = mp->addr;
		lim = mp->addr+mp->size;
		if(lim > maxmem){
			lost = lim-maxmem;
			cm->npage = (maxmem-mp->addr)/BY2PG;
		}
		else
			cm->npage = mp->size/BY2PG;
	}

	for(; i<nelem(mapram); i++)
		lost += rmapram.map[i].size;
	if(lost)
		print("meminit - lost %llud bytes\n", lost);

	if(DBGFLG)
		memdebug();
}

void
mapraminit(uvlong addr, uvlong len)
{
	uvlong l;
	uintptr a;

	/*
	 * Careful - physical address.
	 * Boot time only to populate map.
	 */
	a = PGROUND(addr);
	l = a - addr;
	if(l >= len)
		return;
	l = (len - l) & ~(BY2PG-1);

	for(addr = a+l; a < addr; a += l){
		l = addr - a;
		if(l > 2*GiB)
			l = 2*GiB;
		mapfree(&rmapram, a, l);
	}
}

void
mapupainit(uvlong addr, usize size)
{
	usize s;
	uintptr a;

	/*
	 * Careful - physical address.
	 * Boot time only to populate map.
	 */
	a = PGROUND(addr);
	s = a - addr;
	if(s >= size)
		return;
	mapfree(&rmapupa, a, size-s);
}

void
upareserve(uintptr pa, usize size)
{
	uintptr a;
	
	a = mapalloc(&rmapupa, pa, size, 0);
	if(a != pa){
		/*
		 * This can happen when we're using the E820
		 * map, which might have already reserved some
		 * of the regions claimed by the pci devices.
		print("upareserve: cannot reserve pa=%#llux size=%ud\n", pa, size);
		 */
		if(a != 0)
			mapfree(&rmapupa, a, size);
	}
}
