TEXT await(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $47, RARG
SYSCALL
RET