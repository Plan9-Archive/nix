TEXT segflush(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $33, RARG
SYSCALL
RET
