TEXT fauth(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $10, RARG
SYSCALL
RET
