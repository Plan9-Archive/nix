TEXT fversion(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $40, RARG
SYSCALL
RET
