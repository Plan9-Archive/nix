TEXT pread(SB), 1, $0
MOVQ RARG, a0+0(FP)
MOVQ $50, RARG
SYSCALL
RET
