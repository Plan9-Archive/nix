TEXT _fstat(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $11, RARG
SYSCALL
RET