TEXT _wstat(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $26, RARG
SYSCALL
RET
