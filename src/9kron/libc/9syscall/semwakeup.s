TEXT semwakeup(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $53, RARG
SYSCALL
RET
