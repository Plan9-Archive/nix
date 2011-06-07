#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#define _KADDR(pa)	UINT2PTR(kseg0+((uintptr)(pa)))
#define _PADDR(va)	PTR2UINT(((uintptr)(va)) - kseg0)

void*
KADDR(uintptr pa)
{
	void* va;

	va = _KADDR(pa);
	if(PTR2UINT(va) < kseg0)
		print("pa %#p va #%p @ %#p\n", pa, va, getcallerpc(&pa));
	return va;
}

uintptr
PADDR(void* va)
{
	return _PADDR(va);
}
