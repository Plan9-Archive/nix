#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"


/*
 * Support for user-level optimistic semaphores.
 *
 * CAUTION: Do not use adec/ainc for semaphores, they would
 * trap if the integer is negative, but that's ok for semaphores.
 */

static void
semsleep(Sem *s)
{
	DBG("semsleep up %#p sem %#p\n", up, s->np);
	lock(s);
	if(s->nwakes > 0){
		s->nwakes--;
		unlock(s);
		goto Done;
	}
	s->q = realloc(s->q, (s->nq+1) * sizeof s->q[0]);
	if(s->q == nil)
		panic("semsleep: no memory");
	s->q[s->nq++] = up;
	up->waitsem = nil;
	unlock(s);
	DBG("semsleep up %#p blocked\n", up);
	up->state = Semdown;
	sched();
Done:
	DBG("semsleep up %#p awaken\n", up);
	if(up->waitsem == nil)
		semainc(s->np);	/* we are no longer waiting; killed */
}

void
syssemsleep(Ar0*, va_list list)
{
	int *np;
	Sem *s;
	Segment *sg;

	/*
	 * void semsleep(int*);
	 */
	np = va_arg(list, int*);
	np = validaddr(np, sizeof *np, 1);
	evenaddr(PTR2UINT(np));
	if((sg = seg(up, PTR2UINT(np), 0)) == nil)
		error(Ebadarg);
	s = segmksem(sg, np);
	semsleep(s);
}

static void
semwakeup(Sem *s, int didwake)
{
	Proc *p;

	DBG("semwakeup up %#p sem %#p\n", up, s->np);
	lock(s);
	if(s->nq == 0){
		s->nwakes++;
		unlock(s);
		return;
	}
	p = s->q[0];
	s->nq--;
	s->q[0] = s->q[s->nq];
	unlock(s);
	if(didwake){
		DBG("semwakeup up %#p waking up %#p\n", up, p);
		p->waitsem = s;
		ready(p);
	}
}

void
syssemwakeup(Ar0*, va_list list)
{
	int *np;
	Sem *s;
	Segment *sg;

	/*
	 * void semwakeup(int*);
	 */
	np = va_arg(list, int*);
	np = validaddr(np, sizeof *np, 1);
	evenaddr(PTR2UINT(np));
	if((sg = seg(up, PTR2UINT(np), 0)) == nil)
		error(Ebadarg);
	s = segmksem(sg, np);
	semwakeup(s, 1);
}

static void
semdequeue(Sem *s)
{
	int i;

	assert(s != nil);
	lock(s);
	for(i = 0; i < s->nq; i++)
		if(s->q[i] == up)
			break;

	if(i == s->nq){
		/*
		 * We didn't do a down on s, but are no longer queued
		 * on it; it must be because someone is giving us its
		 * ticket during up. We must put it back in s.
		 */
		if(*s->np >= 0){
			semainc(s->np);
			unlock(s);
		}else{
			unlock(s);
			semwakeup(s, 0);
		}
		return;
	}
	s->nq--;
	s->q[i] = s->q[s->nq];
	unlock(s);
}

/*
 * Alternative down of a Sem in ss[].
 */
static int
semalt(Sem *ss[], int n)
{
	int i, j, r;
	Sem *s;

	DBG("semalt up %#p ss[0] %#p\n", up, ss[0]->np);
	r = -1;
	for(i = 0; i < n; i++){
	DBG("semalt1 %#p\n", up);
		s = ss[i];
		n = semadec(s->np);
		if(n >= 0){
			r = i;
			goto Done;
		}
		lock(s);
		if(s->nwakes > 0){
			s->nwakes--;
			unlock(s);
			r = i;
			goto Done;
		}
		s->q = realloc(s->q, (s->nq+1) * sizeof s->q[0]);
		if(s->q == nil)
			panic("semalt: not enough memory");
		s->q[s->nq++] = up;
		unlock(s);
	}

	DBG("semalt up %#p blocked\n", up);
	up->state = Semdown;
	sched();

Done:
	DBG("semalt up %#p awaken\n", up);
	for(j = 0; j < i; j++){
		assert(ss[j] != nil);
		if(ss[j] != up->waitsem)
			semdequeue(ss[j]);
		else
			r = j;
	}
	if(r < 0)
		panic("semalt");
	return r;
}

void
syssemalt(Ar0 *ar0, va_list list)
{
	int **sl;
	int i, *np, ns;
	Segment *sg;
	Sem *ksl[16];

	/*
	 * void semalt(int*[], int);
	 */
	ar0->i = -1;
	sl = va_arg(list, int**);
	ns = va_arg(list, int);
	sl = validaddr(sl, ns * sizeof(int*), 1);
	if(ns > nelem(ksl))
		panic("syssemalt: bug: too many semaphores in alt");
	for(i = 0; i < ns; i++){
		np = sl[i];
		np = validaddr(np, sizeof(int), 1);
		evenaddr(PTR2UINT(np));
		if((sg = seg(up, PTR2UINT(np), 0)) == nil)
			error(Ebadarg);
		ksl[i] = segmksem(sg, np);
	}	
	ar0->i = semalt(ksl, ns);
}

