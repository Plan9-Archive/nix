TEXT rendezvous(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $34, RARG
SYSCALL
RET