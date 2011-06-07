#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include <tos.h>
#include <pool.h>
#include "amd64.h"
#include "ureg.h"
#include "io.h"

/*
 * NIX code run at the AC.
 * This is the "AC kernel".
 */

/*
 * FPU:
 *
 * The TC handles the FPU by keeping track of the state for the
 * current process. If it has been used and must be saved, it is saved, etc.
 * When a process gets to the AC, we handle the FPU directly, and save its
 * state before going back to the TC (or the TC state would be stale).
 *
 * Because of this, each time the process comes back to the AC and
 * uses the FPU it will get a device not available trap and
 * the state will be restored. This could be optimized because the AC
 * is single-process, and we do not have to disable the FPU while
 * saving, so it does not have to be restored.
 */

extern char* acfpunm(Ureg* ureg, void*);
extern char* acfpumf(Ureg* ureg, void*);
extern char* acfpuxf(Ureg* ureg, void*);
extern void acfpusysprocsetup(Proc*);

extern void _acsysret(void);
extern void _actrapret(void);

ACVctl *acvctl[256];

/*
 * Check if the AC kernel (mach) stack has more than 4*KiB free.
 * Do not call panic, the stack is gigantic.
 */
static void
acstackok(void)
{
	char dummy;
	char *sstart;

	sstart = (char *)m - PGSZ - 4*PTPGSZ - MACHSTKSZ;
	if(&dummy < sstart + 4*KiB){
		print("ac kernel stack overflow, cpu%d stopped\n", m->machno);
		DONE();
	}
}

/*
 * Main scheduling loop done by the application core.
 * Some of functions run will not return.
 * The system call handler will reset the stack and
 * call acsched again.
 * We loop because some functions may return and we should
 * wait for another call.
 */
void
acsched(void)
{
	acmmuswitch();
	for(;;){
		acstackok();
		m->load = 0;
		while(*m->icc->fn == nil)
			;
		if(m->icc->flushtlb)
			acmmuswitch();
		DBG("acsched: cpu%d: fn %#p\n", m->machno, m->icc->fn);
		m->load = 100;
		m->icc->fn();
		DBG("acsched: cpu%d: idle\n", m->machno);
		mfence();
		m->icc->fn = nil;
	}
}

void
acmmuswitch(void)
{
	cr3put(m->pml4->pa);
}

/*
 * Beware: up is not set when this function is called.
 */
void
actouser(void)
{
	void xactouser(u64int);
	uintptr sp;
	Ureg *u;

	acfpusysprocsetup(m->proc);

	memmove(&sp, m->icc->data, sizeof(sp));
	u = m->proc->dbgreg;
	DBG("cpu%d: touser usp = %#p entry %#p\n", m->machno, sp, u->ip);

	m->load = 100;
	xactouser(u->sp);
	panic("actouser");
}

void
actrapret(void)
{
	/* done by actrap() */
}


/*
 * Entered in AP core context, upon traps and system calls.
 * using up->dbgreg means cores MUST be homogeneous.
 *
 * BUG: We should setup some trapenable() mechanism for the AC,
 * so that code like fpu.c could arrange for handlers specific for
 * the AC, instead of doint that by hand here.
 */
void
actrap(Ureg *u)
{
	char *n;
	ACVctl *v;

	n = nil;
	if(u->type < nelem(acvctl)){
		v = acvctl[u->type];
		if(v != nil){
			DBG("actrap: cpu%d: %ulld\n", m->machno, u->type);
			n = v->f(u, v->a);
			if(n != nil)
				goto Post;
			return;
		}
	}
	switch(u->type){
	case IdtIPI:
		m->intr++;
		DBG("actrap: cpu%d: IPI\n", m->machno);
		/*
		 * Beware: BUG: we can get now IPIs while in kernel mode,
		 * after declaring the end of the interrupt.
		 * The code is not prepared for that.
		 */
		apiceoi(IdtIPI);
		break;
	case IdtPF:
		/* this case is here for debug only */
		m->pfault++;
		DBG("actrap: cpu%d: PF\n", m->machno);
		break;
	default:
		print("actrap: cpu%d: %ulld\n", m->machno, u->type);
	}
Post:
	m->icc->rc = ICCTRAP;
	m->cr2 = cr2get();
	memmove(m->proc->dbgreg, u, sizeof *u);
	m->icc->note = n;
	fpuprocsave(m->proc);
	mfence();
	m->icc->fn = nil;
	ready(m->proc);

	m->load = 0;
	while(*m->icc->fn == nil)
		;

	m->load = 100;
	if(m->icc->flushtlb)
		acmmuswitch();
	DBG("actrap: ret\n");
	if(m->icc->fn != actrapret)
		acsched();
	memmove(u, m->proc->dbgreg, sizeof *u);
}

void
acsyscall(void)
{
	/*
	 * If we saved the Ureg into m->proc->dbgregs,
	 * There's nothing else we have to do.
	 * Otherwise, we should m->proc->dbgregs = u;
	 */
	DBG("acsyscall: cpu%d\n", m->machno);
	m->syscall++;	/* would also count it in the TS core */
	m->icc->rc = ICCSYSCALL;
	m->cr2 = cr2get();
	fpuprocsave(m->proc);
	mfence();
	m->icc->fn = nil;
	ready(m->proc);
	m->load = 0;
	/*
	 * The next call is probably going to make us jmp
	 * into user code, forgetting all our state in this
	 * stack, upon the next syscall.
	 * We don't nest calls in the current stack for too long.
	 */
	acsched();
}

/*
 * Called in AP core context, to return from system call.
 */
void
acsysret(void)
{
	DBG("acsysret\n");
	_acsysret();
}

void
dumpreg(void *u)
{
	print("reg is %p\n", u);
	ndnr();
}


void
acmodeset(int mode)
{
	switch(mode){
	case NIXAC:
	case NIXKC:
	case NIXTC:
		break;
	default:
		panic("apmodeset: bad mode %d", mode);
	}
	m->nixtype = mode;
}

void
acinit(void)
{
	/*
	 * Lower the priority of the apic to 0,
	 * to accept interrupts.
	 * Raise it later if needed to disable them.
	 */
	apicpri(0);
}
