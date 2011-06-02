/*
 *  Performance counters
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"amd64.h"
#include	"pmc.h"

enum{
	Qdir		= 0,
	Qdata,
	Qctl,
	Qgctl,

	PmcRdStr = 4*1024,
};

#define PMCTYPE(x)	(((unsigned)x)&0xffful)
#define PMCID(x)	(((unsigned)x)>>12)
#define PMCQID(i, t)	((((unsigned)i)<<12)|(t))

Dirtab *pmctab;
static int npmctab;
int pmcdebug;

static void
pmcinit(void)
{
	int nr, i;
	Dirtab *d;

	nr = pmcnregs();
		
	npmctab = 2 + 2*nr;
	pmctab = malloc(npmctab * sizeof(Dirtab));
	if (pmctab == nil)
		return;
	
	d = pmctab;
	strncpy(d->name, ".", KNAMELEN);
	mkqid(&d->qid, Qdir, 0, QTDIR);
	d->perm = DMDIR|0555;
	d++;
	strncpy(d->name, "ctrdesc", KNAMELEN);
	mkqid(&d->qid, Qgctl, 0, 0);
	d->perm = 0444;
	for (i = 2; i < nr + 2; i++) {
		d = &pmctab[i];
		snprint(d->name, KNAMELEN, "ctr%2.2ud", i - 2);
		mkqid(&d->qid, PMCQID(i - 2, Qdata), 0, 0);
		d->perm = 0600;

		d = &pmctab[nr + i];
		snprint(d->name, KNAMELEN, "ctr%2.2udctl", i - 2);
		mkqid(&d->qid, PMCQID(i - 2, Qctl), 0, 0);
		d->perm = 0600;
	}	
	
}

static Chan *
pmcattach(char *spec)
{
	if (pmctab == nil)
		error(Enomem);
	return devattach(L'ε', spec);
}

static Walkqid*
pmcwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, pmctab, npmctab, devgen);
}

static long
pmcstat(Chan *c, uchar *dp, long n)
{
	return devstat(c, dp, n, pmctab, npmctab, devgen);
}

static Chan*
pmcopen(Chan *c, int omode)
{
	if (!iseve())
		error(Eperm);
	return devopen(c, omode, pmctab, npmctab, devgen);
}

static void
pmcclose(Chan *c)
{
	if (c->aux) {
		free(c->aux);
		c->aux = nil;
	}
}


int
pmcanyenab(void)
{
	int i;
	Pmc p;

	for (i = 0; i < pmcnregs(); i++) {
		if (pmcgetctl(&p, i) < 0)
			return -1;
		if (p.enab)
			return 1;
	}

	return 0;
	
}

static long
pmcread(Chan *c, void *a, long n, vlong offset)
{
	ulong type, id;
	Pmc p;
	char *s;
	u64int v;

	if (c->qid.type == QTDIR)
		return devdirread(c, a, n, pmctab, npmctab, devgen);

	s = malloc(PmcRdStr);
	if(waserror()){
		free(s);
		nexterror();
	}
	type = PMCTYPE(c->qid.path);
	id = PMCID(c->qid.path);
	switch(type){
	case Qdata:
		if (up->ac != nil)
			error("unimplemented yet");
		v = pmcgetctr(id);
		snprint(s, PmcRdStr, "%#ullx", v);
		break;
	case Qctl:
		if (pmcgetctl(&p, id) < 0)
			error("bad ctr");
		if (pmcctlstr(s, PmcRdStr, &p) < 0)
			error("bad pmc");
		break;
	case Qgctl:
		if (pmcdescstr(s, PmcRdStr) < 0)
			error("bad pmc");
		break;
	default:
		error(Eperm);
	}
	n = readstr(offset, a, n, s);
	free(s);
	poperror();
	return n;
}

enum{
	Enable,
	Disable,
	User,
	Os,
	Reset,
	Debug,
};

static Cmdtab pmcctlmsg[] =
{
	Enable,		"enable",	0,
	Disable,	"disable",	0,
	User,		"user",		0,
	Os,		"os",		0,
	Reset,		"reset",	0,
	Debug, 		"debug",	0,
};

static void
pmcnull(Pmc *p)
{
	memset(p, 0xff, sizeof(Pmc));
	p->enab = PmcNullval;
	p->user = PmcNullval;
	p->os = PmcNullval;
}

typedef void (*APfunc)(void);

void
acpmcsetctl(void)
{
	Pmc p;
	Mach *mp;

	mp = up->ac;
	memmove(&p, mp->icc->data, sizeof(Pmc));
	
	mp->icc->rc = pmcsetctl(&p);
	return;
}
static long
pmcwrite(Chan *c, void *a, long n, vlong)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	ulong type;
	char str[64];	/* 0x0000000000000000\0 */
	Pmc p;

	if (c->qid.type == QTDIR)
		error(Eperm);
	if (c->qid.path == Qgctl)
		error(Eperm);
	if (n >= sizeof(str))
		error(Ebadctl);

	pmcnull(&p);
	type = PMCTYPE(c->qid.path);
	p.regno = PMCID(c->qid.path);
	memmove(str, a, n);
	str[n] = '\0';
	if (type == Qdata) {
		pmcsetctr(atoi(str), p.regno);
		return n;
	}


	/* TODO: should iterate through multiple lines */
	if (strncmp(str, "set ", 4) == 0){
		memmove(p.descstr, (char *)str + 4, n - 4);
		p.nodesc = 0;
	} else {
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, pmcctlmsg, nelem(pmcctlmsg));
		switch(ct->index){
		case Enable:
			p.enab = 1;
			break;
		case Disable:
			p.enab = 0;
			break;
		case User:
			p.user = 1;
			break;
		case Os:
			p.os = 1;
			break;
		case Reset:
			p.reset = 1;
			break;
		case Debug:
			pmcdebug = ~pmcdebug;
			break;
		default:
			cmderror(cb, "invalid ctl");
		break;
		}
		free(cb);
		poperror();
	}
	if (up->ac != nil) {
		if (runac(up->ac, acpmcsetctl, 0, &p, sizeof(Pmc)) < 0)
			n = -1;
	} else {
		if (pmcsetctl(&p) < 0)
			n = -1;
	}
	return n;
}


Dev pmcdevtab = {
	L'ε',
	"pmc",

	pmcinit,
	devinit,
	devshutdown,
	pmcattach,
	pmcwalk,
	pmcstat,
	pmcopen,
	devcreate,
	pmcclose,
	pmcread,
	devbread,
	pmcwrite,
	devbwrite,
	devremove,
	devwstat,
};
