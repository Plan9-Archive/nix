TEXT fd2path(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $23, RARG
SYSCALL
RET
