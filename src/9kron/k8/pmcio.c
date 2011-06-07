/*
 *  Performance counters non port part
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"amd64.h"
#include	"../port/pmc.h"


static QLock pmclck;

/* non portable, for intel will be CPUID.0AH.EDX */

enum {
	PeNreg		= 4,	/* Number of Pe/Pct regs */
};

int
pmcnregs(void)
{
	return PeNreg;
}

//PeHo|PeGo
#define PeAll	(PeOS|PeUsr)	
#define SetEvMsk(v, e) ((v)|(((e)&PeEvMskL)|(((e)<<(PeEvMsksh-8))&PeEvMskH)))
#define SetUMsk(v, u) ((v)|(((u)<<8ull)&PeUnMsk))

#define GetEvMsk(e) (((e)&PeEvMskL)|(((e)&PeEvMskH)>>(PeEvMsksh-8)))
#define GetUMsk(u) (((u)&PeUnMsk)>>8ull)

static int
pmcuserenab(int enable)
{
	u64int cr4;

	cr4 = cr4get();
	if (enable){
		cr4 |= Pce;
	} else
		cr4 &=  ~Pce;
	cr4put(cr4);
	return cr4&Pce;
}

PmcCtrId pmcids[] = {
	{"locked instr", "0x024 0x0"},
	{"SMI intr", "0x02b 0"},
	{"data access", "0x040 0x0"},
	{"data miss", "0x041 0x0"},
	{"L1 DTLB miss", "0x045 0x7"},	//L2 hit
	{"L2 DTLB miss", "0x046 0x7"},
	{"L1 DTLB hit", "0x04d 0x3"},
	{"L2 hit", "0x07d 0x3f"},
	{"L2 miss", "0x07e 0xf"},
	{"instr miss", "0x081 0x0"},
	{"L1 ITLB miss", "0x084 0"},	//L2 hit
	{"L2 ITLB miss", "0x085 0x3"},
	{"DRAM access", "0x0e0 0x3f"},
	{"L3 miss", "0x4e1 0x3"},	//one core, can be set to more
	{"", ""},
};

int
pmctrans(Pmc *p)
{
	PmcCtrId *pi;

	for (pi = &pmcids[0]; pi->portdesc[0] != '\0'; pi++){
		if ( strncmp(p->descstr, pi->portdesc, strlen(pi->portdesc)) == 0){
			strncpy(p->descstr, pi->archdesc, strlen(pi->archdesc) + 1);
			return 0;
		}
	}
	return 1;
}

int
pmcgetctl(Pmc *p, u32int regno)
{
	u64int r, e, u;

	r = rdmsr(regno + PerfEvtbase);
	p->regno = regno;
	p->enab = (r&PeCtEna) != 0;
	p->user = (r&PeUsr) != 0;
	p->os = (r&PeOS) != 0;
	e = GetEvMsk(r);
	u = GetUMsk(r);
	//TODO inverse translation
	snprint(p->descstr, KNAMELEN, "%#ullx %#ullx", e, u);
	return 0;
}

extern int pmcdebug;

int
pmcsetctl(Pmc *p)
{
	u64int v, e, u;
	char *toks[2];

	if (p->regno >= pmcnregs())
		error("invalid reg");

	qlock(&pmclck);
	v = rdmsr(p->regno + PerfEvtbase);
	v &= PeEvMskH|PeEvMskL|PeCtEna|PeOS|PeUsr|PeUnMsk;
	if (p->enab != PmcNullval)
		if (p->enab)
			v |= PeCtEna;
		else
			v &= ~PeCtEna;
	if (p->user != PmcNullval)
		if (p->user)
			v |= PeUsr;
		else
			v &= ~PeUsr;
	if (p->os != PmcNullval)
		if (p->os)
			v |= PeOS;
		else
			v &= ~PeOS;
	if (pmctrans(p) < 0){
		qunlock(&pmclck);
		return -1;
	}

	if (!p->nodesc) {
		if (tokenize(p->descstr, toks, 2) != 2)
			return -1;
		e = atoi(toks[0]);
		u = atoi(toks[1]);
		v &= ~(PeEvMskL|PeEvMskH|PeUnMsk);
		v |= SetEvMsk(v, e);
		v |= SetUMsk(v, u);
	}
	if (p->reset == 1){
		v = 0;
		wrmsr(p->regno+ PerfCtrbase, 0);
	}
	wrmsr(p->regno+ PerfEvtbase, v);
	pmcuserenab(pmcanyenab());
	if (pmcdebug) {
		v = rdmsr(p->regno+ PerfEvtbase);
		print("conf pmc[%#ux]: %#llux\n", p->regno, v);
	}
	qunlock(&pmclck);
	return 0;
}

int
pmcctlstr(char *str, int nstr, Pmc *p)
{
	int ns;

	ns = 0;
	if (p->enab)
		ns += snprint(str + ns, nstr - ns, "enable\n");
	else
		ns += snprint(str + ns, nstr - ns, "disable\n");
		
	if (p->user)
		ns += snprint(str + ns, nstr - ns, "user\n");
	if (p->os)
		ns += snprint(str + ns, nstr - ns, "os\n");
	
	//TODO, inverse pmctrans?
	ns += snprint(str + ns, nstr - ns, "%s\n", p->descstr);
	return ns;
}

int
pmcdescstr(char *str, int nstr)
{
	PmcCtrId *pi;
	int ns;

	ns = 0;

	for (pi = &pmcids[0]; pi->portdesc[0] != '\0'; pi++)
		ns += snprint(str + ns, nstr - ns, "%s\n",pi->portdesc);
	return ns;
}

u64int
pmcgetctr(u32int regno)
{
	return rdmsr(regno + PerfCtrbase);
}

int
pmcsetctr(u64int v, u32int regno)
{
	wrmsr(regno + PerfCtrbase, v);
	return 0;
}










