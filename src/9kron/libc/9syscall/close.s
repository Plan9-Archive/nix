TEXT close(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $4, RARG
SYSCALL
RET
