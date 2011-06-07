
enum{
	PmcNullval = 0xdead,
};

typedef struct PmcCtrId PmcCtrId;
struct PmcCtrId {
	char portdesc[KNAMELEN];
	char archdesc[KNAMELEN];
};


typedef struct Pmc Pmc;
struct Pmc{
	u32int regno;
	int enab;
	int user;
	int os;
	int nodesc;
	char descstr[KNAMELEN];
	int reset;
};

int		pmcnregs(void);
int		pmcsetctl(Pmc *p);
int		pmctrans(Pmc *p);
int		pmcgetctl(Pmc *p, u32int regno);
int		pmcdescstr(char *str, int nstr);
int		pmcctlstr(char *str, int nstr, Pmc *p);
u64int	pmcgetctr(u32int regno);
int		pmcsetctr(u64int v, u32int regno);

int		pmcanyenab(void);

