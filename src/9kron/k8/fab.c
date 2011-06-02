#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "iob.h"

/*
 * TO DO
 *	- share the allocation map (allocate and free with CAS of pid [say])
 *		- how then to wait for free blocks?
 *		- poll with exponential backoff i suppose
 *	- if io memory not in all address spaces, need different va for kernel and user to prevent va alias
 *		- can just use normal va in kernel (except must dcinval/dcflush)
 *	- distinguish user alloc and kernel alloc (user to free and kernel to free) in Block.free
 */

extern	Physseg iobufseg;
extern	Physseg ioallocseg;

enum{
	Hdrlen=	32,
	Bigsize=	64*1024,
	Poolsize=	4*1024*1024,	/* if not mapped into kernel all at once, need not match a TLB size */
	Nbuf=	Poolsize/Bigsize,
	Npage=	Poolsize/BY2PG,
};

static struct{
	Lock;
	uchar*	va;	/* user virtual address of buffer pool */
	uchar*	vatop;
	Block*	blocks[Nbuf];
	Block*	free;
	Page		pages[Npage];

	/* wait for free blocks */
	QLock	q;
	Rendez	r;
	int	need;
} bigfellas;

static void
bigfree(Block *b)
{
	ilock(&bigfellas);
	b->flag = 0xFA;
	b->rp = b->wp = (void*)(0xBAD0BEEF);
	b->list = bigfellas.free;
	bigfellas.free = b;
	iunlock(&bigfellas);
	if(bigfellas.need){
		bigfellas.need = 0;
		wakeup(&bigfellas.r);
	}
}

#define	TLBURD	(TLBUR|TLBSR)
#define	TLBUWR	(TLBUW|TLBSW)
#define	TLBCC	(TLBWL1|TLBU2|TLBM)	/* cached, coherent */
#define	IOSEG	0xd0000000

static void*
mkphysseg(Physseg *io, physaddr pa, uint nb, void *kva)
{
	void *iova;

	if(kva != nil)
		io->pa = PADDR(kva);
	else
		io->pa = pa;
	if(io->pa & KSEGM)
		panic("mkphysseg: pa too high");
	io->gva = IOSEG | io->pa;
	iova = UINT2PTR(io->gva);
	if(kva != nil){
		/* map the IO segment into the kernel with user r/w permission */
		/* TO DO: note that this is currently only effective during system initialisation */
		dcflush(PTR2UINT(kva), nb);
		if(kmapphys(io->gva, io->pa, nb, TLBURD|TLBUWR|TLBCC) != iova)
			panic("mkphysseg: bad map");
	}
	/* otherwise kernel has only pa for dma, and must access via kmap if needed */
	if(addphysseg(io) < 0)
		panic("mkphysseg: addphysseg");
	return iova;
}

void
iobufinit(void)
{
	Block *b;
	Page *pg;
	physaddr pa;
	uchar *va;
	int i;

//	if(sys->ionode)
//		return;
	if(iobufseg.pa != 0)
		return;
	pa = bal(Poolsize);
	if(pa == 0){
		print("iobufinit: no mem\n");
		return;
	}
	va = mkphysseg(&iobufseg, pa, Poolsize, nil);
	b = malloc(Nbuf*sizeof(*b));
	if(b == nil)
		panic("iobufinit: no Blocks");
	bigfellas.va = va;
	bigfellas.vatop = va+Poolsize;
	for(i = 0; i < Nbuf; i++, b++){
		bigfellas.blocks[i] = b;
		b->base = va;		/* n.b. user va, can't be used in kernel without kmap */
		b->lim = va + Bigsize;
		b->list = nil;
		b->free = bigfree;
		b->flag = 0xFA;
		b->rp = b->wp = (void*)(0xBAD0BEEF);
		b->list = bigfellas.free;
		bigfellas.free = b;
		va += Bigsize;
	}
	va = bigfellas.va;
	for(i = 0; i < Npage; i++){
		pg = &bigfellas.pages[i];
		pg->va = PTR2UINT(va);
		pg->pa = PADDR(va);
		va += BY2PG;
	}
}

Block*
bigalloc(void)
{
	Block *b;

	ilock(&bigfellas);
	b = bigfellas.free;
	if(b != nil)
		bigfellas.free = b->list;
	iunlock(&bigfellas);
	if(b == nil)
		return nil;
	b->rp = b->wp = b->base;
	b->flag = 0;
	//b->ref = 1;
	b->list = nil;
	return b;
}

static int
blockavail(void*)
{
	return bigfellas.free != nil;
}

Block*
sbigalloc(void)
{
	Block *b;

	while((b = bigalloc()) == nil){
		qlock(&bigfellas.q);
		if(waserror()){
			qunlock(&bigfellas.q);
			nexterror();
		}
		bigfellas.need = 1;
		sleep(&bigfellas.r, blockavail, nil);
		qunlock(&bigfellas.q);
	}
	return b;
}

int
isbigblock(Block *b)
{
	return b->free == bigfree;
}

Block*
va2block(void *va)
{
	Block *b;
	uintptr off;
	int i;

	off = (uchar*)va - bigfellas.va;
	if(off >= Poolsize)
		return nil;
	i = off/Bigsize;
	if(i >= Nbuf)
		panic("va2block");
	b = bigfellas.blocks[i];
	if(b->flag == 0xFA)
		return nil;	/* shouldn't be free */
	return b;
}

static Page*
iopgalloc(Segment *s, uintptr addr)
{
	int soff;
	Page *pg;

	/* segment is locked */
	soff = addr-s->base;
	if(soff < 0 || soff >= (nelem(bigfellas.pages)<<PGSHIFT))
		panic("iopgalloc");
	pg = &bigfellas.pages[soff>>PGSHIFT];
	pg->ref++;
	return pg;
}

static void
iopgfree(Page *p)
{
	lock(p);
	p->ref--;
	unlock(p);
}

Physseg	iobufseg = {
	.attr = SG_PHYSICAL|SG_CACHED|SG_CEXEC, .name = "iobuf", .pa = 0, .size = Npage, .pgalloc = iopgalloc, .pgfree = iopgfree,
};

Physseg	ioallocseg = {
	.attr = SG_PHYSICAL|SG_CACHED|SG_CEXEC, .name = "ioalloc", .pa = 0, .size = 1, .pgalloc = iopgalloc, .pgfree = iopgfree,
};
