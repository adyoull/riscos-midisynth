/* A stand-in for RISC OS's <kernel.h>, so the output backends can be
   built and tested on a PC. The SWIs go to tests/fake_swi.c. */
#ifndef FAKE_KERNEL_H
#define FAKE_KERNEL_H

typedef struct { int r[10]; } _kernel_swi_regs;
typedef struct { int errnum; char errmess[252]; } _kernel_oserror;

_kernel_oserror *_kernel_swi(int no, _kernel_swi_regs *in, _kernel_swi_regs *out);

#endif
