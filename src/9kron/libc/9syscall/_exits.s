TEXT _exits(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $8, RARG
SYSCALL
RET
