typedef struct Conf Conf;
typedef struct Confmem Confmem;
typedef struct Fxsave Fxsave;
typedef struct ICC ICC;
typedef struct ICCparms ICCparms;
typedef struct ISAConf ISAConf;
typedef struct Label Label;
typedef struct Lock Lock;
typedef struct MCPU MCPU;
typedef struct MFPU MFPU;
typedef struct MMMU MMMU;
typedef struct NIX NIX;
typedef struct Mach Mach;
typedef u64int Mreg;    			/* Msr - bloody UART */
typedef struct Page Page;
typedef struct Pcidev Pcidev;
typedef struct PFPU PFPU;
typedef struct PMMU PMMU;
typedef struct PNOTIFY PNOTIFY;
typedef u64int PTE;
typedef struct Proc Proc;
typedef struct Sys Sys;
typedef struct Ureg Ureg;
typedef struct Vctl Vctl;
typedef struct ACVctl ACVctl;
typedef struct PmcCtr PmcCtr;
typedef struct PmcCtl PmcCtl;

#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, afd, mpt, flag, arg) */

/*
 *  parameters for sysproc.c
 */
#define AOUT_MAGIC	(S_MAGIC)

/*
 *  machine dependent definitions used by ../port/portdat.h
 */
struct Lock
{
	u32int	key;
	Mreg	sr;
	uintptr	pc;
	Proc*	p;
	Mach*	m;
	int	isilock;
};

struct Label
{
	uintptr	sp;
	uintptr	pc;
};

struct Fxsave {
	u16int	fcw;			/* x87 control word */
	u16int	fsw;			/* x87 status word */
	u8int	ftw;			/* x87 tag word */
	u8int	zero;			/* 0 */
	u16int	fop;			/* last x87 opcode */
	u64int	rip;			/* last x87 instruction pointer */
	u64int	rdp;			/* last x87 data pointer */
	u32int	mxcsr;			/* MMX control and status */
	u32int	mxcsrmask;		/* supported MMX feature bits */
	uchar	st[128];		/* shared 64-bit media and x87 regs */
	uchar	xmm[256];		/* 128-bit media regs */
	uchar	ign[96];		/* reserved, ignored */
};

/*
 *  FPU stuff in Proc
 */
struct PFPU {
	int	fpustate;
	uchar	fxsave[sizeof(Fxsave)+15];
	void*	fpusave;
};

/*
 *  MMU stuff in Proc
 */
#define NCOLOR 1
struct PMMU
{
	Page*	mmuptp[4];		/* page table pages for each level */
};

/*
 *  things saved in the Proc structure during a notify
 */
struct PNOTIFY
{
	void	emptiness;
};

struct Confmem
{
	uintptr	base;
	usize	npage;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	Confmem	mem[4];		/* physical memory */
	uvlong	npage;		/* total physical pages of memory */
	usize	upages;		/* user page pool */
	ulong	copymode;	/* 0 is copy on write, 1 is copy on reference */
	ulong	ialloc;		/* max interrupt time allocation in bytes */
	ulong	pipeqsize;	/* size in bytes of pipe queues */
	ulong	nimage;		/* number of page cache image headers */
	ulong	nswap;		/* number of swap pages */
	int	nswppo;		/* max # of pageouts per segment pass */
};

#include "../port/portdat.h"

/*
 *  CPU stuff in Mach.
 */
struct MCPU
{
	u32int	cpuinfo[32][4];		/* CPUID instruction output E[ABCD]X */
	int	ncpuinfos;		/* number of standard entries */
	int	ncpuinfoe;		/* number of extended entries */
};

/*
 *  FPU stuff in Mach.
 */
struct MFPU
{
	u16int	fcw;			/* x87 control word */
	u32int	mxcsr;			/* MMX control and status */
	u32int	mxcsrmask;		/* supported MMX feature bits */
};

struct NIX
{
	ICC*	icc;	/* inter-core call */
	int nixtype;	
};

/*
 *  MMU stuff in Mach.
 */
struct MMMU
{
	uintptr cr2;
	Page*	pml4;			/* pml4 for this processor */
	PTE*	pmap;			/* unused as of yet */

	Page	pml4kludge;		/* NIX KLUDGE: we need a page */
};

/*
 * Inter core calls
 */
enum
{
	ICCLNSZ =	128,	/* Cache line size for inter core calls */


	ICCOK = 0,		/* Return codes: Ok; trap; syscall */
	ICCTRAP,
	ICCSYSCALL
};

struct ICC
{
	/* fn is kept in its own cache line */
	union{
		void	(*fn)(void);
		uchar	_ln1_[ICCLNSZ];
	};
	int	flushtlb;	/* on the AC, before running fn */
	int	rc;		/* return code from AC to TC */
	char*	note;		/* to be posted in the TC after returning */
	uchar	data[ICCLNSZ];	/* sent to the AC */
};

/*
 * hw perf counters
 */
struct PmcCtl {
	u32int coreno;
	int enab;
	int user;
	int os;
	int nodesc;
	char descstr[KNAMELEN];
	int reset;
};

struct PmcCtr{
	int stale;
	Rendez r;
	u64int ctr;
	int ctrset;
	PmcCtl;
	int ctlset;
};

enum {
	PmcMaxCtrs = 4,
	PmcIgn = 0,
	PmcGet = 1,
	PmcSet = 2,
};

/*
 * Per processor information.
 *
 * The offsets of the first few elements may be known
 * to low-level assembly code, so do not re-order:
 *	machno	- no dependency, convention
 *	splpc	- splhi, spllo, splx
 *	proc	- syscallentry
 */
struct Mach
{
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc of last caller to splhi */

	Proc*	proc;			/* current process on this processor */
	uintptr	stack;

	int	apicno;
	int	online;

	MMMU;

	uchar*	vsvm;
	void*	gdt;
	void*	tss;

	ulong	ticks;			/* of the clock since boot time */
	Label	sched;			/* scheduler wakeup */
	Lock	alarmlock;		/* access to alarm list */
	void*	alarm;			/* alarms bound to this clock */
	int	inclockintr;

	Proc*	readied;		/* for runproc */
	ulong	schedticks;		/* next forced context switch */

	int	tlbfault;
	int	tlbpurge;
	int	pfault;
	int	cs;
	int	syscall;
	int	load;
	int	intr;
	int	mmuflush;		/* make current proc flush it's mmu state */
	int	ilockdepth;
	Perf	perf;			/* performance counters */

	int	lastintr;

	Lock	apictimerlock;
	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */
	vlong	cpuhz;
	int	cpumhz;

	MFPU;
	MCPU;

	Lock pmclock;
	PmcCtr pmc[PmcMaxCtrs];

	NIX;
};

/*
 * This is the low memory map, between 0x100000 and 0x110000.
 * It is located there to allow fundamental datastructures to be
 * created and used before knowing where free memory begins
 * (e.g. there may be modules located after the kernel BSS end).
 * The layout is known in the bootstrap code in l32p.s.
 * It is logically two parts: the per processor data structures
 * for the bootstrap processor (stack, Mach, vsvm, and page tables),
 * and the global information about the system (syspage, ptrpage).
 * Some of the elements must be aligned on page boundaries, hence
 * the unions.
 */
struct Sys {
	uchar	machstk[MACHSTKSZ];

	PTE	pml4[PTPGSZ/sizeof(PTE)];	/*  */
	PTE	pdp[PTPGSZ/sizeof(PTE)];
	PTE	pd[PTPGSZ/sizeof(PTE)];
	PTE	pt[PTPGSZ/sizeof(PTE)];

	uchar	vsvmpage[PGSZ];

	union {
		Mach	mach;
		uchar	machpage[MACHSZ];
	};

	union {
		struct {
			u64int	memstart;	/* boot-time physical memory */
			u64int	memend;
		};
		uchar	syspage[PGSZ];
	};

	union {
		Mach*	machptr[MACHMAX];
		uchar	ptrpage[PGSZ];
	};

	uchar	_57344_[2][PGSZ];		/* unused */
};

extern Sys* sys;

/*
 * Fake kmap
 */
typedef void		KMap;
#define VA(k)		PTR2UINT(k)
#define kmap(p)		(KMap*)((p)->pa|kseg0)
#define kunmap(k)

struct
{
	Lock;
	uint	machs;			/* bitmap of active CPUs (must go) */
	int	exiting;		/* shutdown */
	int	ispanic;		/* shutdown in response to a panic */
	int	thunderbirdsarego;	/* lets the added processors continue */
}active;

/*
 *  a parsed plan9.ini line
 */
#define NISAOPT		8

struct ISAConf {
	char	*type;
	uintptr	port;
	int	irq;
	ulong	dma;
	uintptr	mem;
	usize	size;
	ulong	freq;

	int	nopt;
	char	*opt[NISAOPT];
};

/*
 * The Mach structures must be available via the per-processor
 * MMU information array machptr, mainly for disambiguation and access to
 * the clock which is only maintained by the bootstrap processor (0).
 */
#define MACHP(n)	(sys->machptr[n])

extern register Mach* m;			/* R15 */
extern register Proc* up;			/* R14 */

extern uintptr kseg0;

/*
 * Horrid.
 */
#ifdef _DBGC_
#define DBGFLG		(dbgflg[_DBGC_])
#else
#define DBGFLG		(0)
#endif /* _DBGC_ */

#define DBG(...)	if(!DBGFLG){}else dbgprint(__VA_ARGS__)

extern char dbgflg[256];

#define dbgprint	print		/* for now */
