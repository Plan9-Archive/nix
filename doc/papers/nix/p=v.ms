.HP
NIX has a changed memory model from the old UNIX model used for the
last forty years.  The reader should recall that the early machines
UNIX ran on had very tight memory constraints: 16 4096-byte pages.
Further, the 16-bit memory address on those machines could not address
all of memory once the 11/45 and later models were introduced.
Finally, it made sense to provide a virtual address space that was the
same for all binaries, since it made for more efficient code.  Hence,
we have the memory address scheme show below.
.PS
.ps 8
.vs 10
boxht=0.1
boxwid=1.0

P0: box invis wid 1 "\f(CWProcess Virtual Address Map\fP"
move right 2
M0: box invis wid 1 "\f(CWPhysical Pages\fP"
for i = 1 to 32 do {M: box ht .1 wid 1 "Page" with .n at last box.s}

define boxproc {
	box invis wid 2 ht 1 with .n at $1.s
	box wid 2 "Stack at 0xffffff....." with .n at last box.s 
	line ->from last box.e to M0.sw - (0,$2)
	box wid 2 "Gap between heap and stack" with .n at last box.s
	box wid 2 "Heap page" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$3)
	box wid 2 "Heap page" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$3)
	box wid 2 "Heap page" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$5)
	box wid 2 "..." with .n at last box.s
	box wid 2 "Heap at next page after end of data" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$6)
	box wid 2 "Data at last page after end of text" with .n at last box.s
	line -> from last box.e to M0.sw-(0,$7)
	box wid 2 "Text at 4096" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$8)
}

P1:boxproc(P0, .85, .65, .95, 2.25, 1.15, 1.75, 1.65, 2.05)
boxproc(P1, .25, 1.05, 2.35, 2.55, .15, .35, 1.95, 2.45)
.PE
.PP
As the figure shows, with a standard virtual address range, even
though processes have a near-identical address layout, the same
virtual address in different processes point to different pages
(except in the special case of the code for shared libraries and
binaries).  This setup works well for code and data.  Code and data
can be linked to always start at the same virtual address, greatly
simplifying the task of starting a process and managing the use of
multiple processes sharing one image.³
.FS
³ On some older systems, the virtual address space of the code can
change each time it is run, requiring support from the runtime,
architecture, and even the operating system.
.FE
For dynamic addresses, such as the stack and the heap, such
simplification is supported but not required.  The stack address can
always be derived from the stack pointer register, and the heap is
determined from a system call
.I brk ). (
Most systems that use threads
already allow for a variable stack location, specified when the thread
is started.  Unlike the older model shown above, most newer Unix
systems also use disjoint heaps; libraries use
.I mmap
to allocate data,
not
.I brk ,
and the mapped data areas need not be virtually contiguous
with any other area.  For example, /bin/cat on Linux has at least five
different sets of heap pages, none of them adjacent to each other.
.PP
The simplification offered by a common virtual address range is not
without problems.  Processes can not share memory easily, because a
heap address in one process does not point to the same data when
passed to another process.  This one limitation has felled more trees
and spilled more ink at more conferences in the last 40 years than one
might expect; it even led researchers at Berkeley to modify their
PDP-11 hardware to support data transfer between user mode processes
with the move from previous address space instruction⁴.
.FS
⁴ Fortunately, it was a one-wire change. 
.FE
Process  addresses are not the addresses used by the kernel, which lives
in its own address space. Still worse, interrupt code, which does not run in any 
process context, can not do direct I/O to process memory without a great 
deal of additional complexity. Pages must be "pinned", to make sure they don't move
around, and data structures must be created and maintained to ensure that 
all the correct cleanup and maintenance is done. 
There is the further problem of I/O. Process heap virtual 
addresses have no meaning to I/O devices, most of which do not handle 
virtual addressing, and even further work must be maintained
in, not only kernel structures, but structures managed by the operating system 
on the I/O cards. 
Quite a bit of work has been done to maintain the unique heap address space 
that is virtually contiguous;
which, as we have seen, is not even needed any more. 
.PP
In our work on Blue Gene we found a way to get rid of this limitation: we got
rid of the fiction. Our kernel on Blue Gene
sets up processes so that heap virtual addresses are not unique, and in fact are 
directly mapped from virtual to physical addresses. For heap, physical 
and virtual addresses are the same. The result is that 
a process heap address has the same meaning to the process, all other processes, 
the kernel, the 
interrupt drivers, and the I/O devices. A process can initiate I/O to an address without 
having to do any computations: the address has universal meaning. 
Processes can share memory without worrying about whether a given memory 
range in one process can fit in another. 
.PP
A condensed version of the  
address space structure we are using on Blue Gene is shown below. 
We will be using some variant of this on NIX. One question: should heap
be above or below the stack? For now, we leave the relative locations unchanged, 
although threaded programs intermix heap and stack pages without much trouble. 
As can be seen, heap pages are mapped to a corresponding physical page. 
Sharing between processes is easy given this design; a process need merely indicate
which other process should be granted access to which part of its address space, 
and the kernel can install it in the other process should that other process 
attempt to access it. 
.PS
.ps 8
.vs 10
boxht=0.1
boxwid=1.0

P0: box invis wid 1 "\f(CWProcess Virtual Address Map\fP"
move right 2
M0: box invis wid 1 "\f(CWPhysical Pages\fP"
for i = 1 to 18 do {M: box ht .1 wid 1 "Page" with .n at last box.s}
for i = 1 to 13 do {M: box ht .1 wid 1 "Heap Page" with .n at last box.s}

define nboxproc {
	box invis wid 2 ht 1 with .n at $1.s
	box wid 2 "Stack at 0xffffff....." with .n at last box.s 
	line ->from last box.e to M0.sw - (0,$2)
	box wid 2 "Gap between heap and stack" with .n at last box.s
	box wid 2 "Heap page" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$3)
	box wid 2 "Hole" with .n at last box.s
	box wid 2 "Heap page" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$4)
	box wid 2 "Hole" with .n at last box.s
	box wid 2 "Heap base at (e.g.) 16 GB" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$6)
	box wid 2 "Data at last page after end of text" with .n at last box.s
	box wid 2 "Text at 4096" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$7)
}

define nboxproc2 {
	box invis wid 2 ht 1 with .n at $1.s
	box wid 2 "Stack at 0xffffff....." with .n at last box.s 
	line ->from last box.e to M0.sw - (0,$2)
	box wid 2 "Gap between heap and stack" with .n at last box.s
	box wid 2 "Hole" with .n at last box.s
	box wid 2 "Heap page" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$3)
	box wid 2 "Hole" with .n at last box.s
	box wid 2 "Heap page" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$4)
	box wid 2 "..." with .n at last box.s
	box wid 2 "Heap base at (e.g.) 16 GB" with .n at last box.s
	box wid 2 "Data at last page after end of text" with .n at last box.s
	line -> from last box.e to M0.sw-(0,$5)
	box wid 2 "Text at 4096" with .n at last box.s
	line -> from last box.e to M0.sw- (0,$6)
}

P2:nboxproc(P0, .85, 2.05, 2.25, 2.45, 1.15, 1.75, 1.65, 2.05)
nboxproc2(P2, .25, 2.15, 2.35, 1.25, .35)
.PE
.LP
Applications can now transparently share heap addresses as needed with each 
other. Heap addresses are now valid in all processor modes. 
One other change is that heaps are no longer virtually contiguous. Heaps have 
holes, which, as mentioned above, is acceptable.
Some runtimes, e.g. Go in 32-bit mode, will not function correctly
with discontiguous heaps. At the same time, Go only needs about 1 Gbyte of contiguous 
memory, so it is possible that this will not be a problem if the kernel can preserve 
reasonably large chunks of phsyically contiguous memory. 
