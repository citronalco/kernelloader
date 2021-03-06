/* Copyright (c) 2007 - 2010 Mega Man */
#include "stdio.h"
#include "kernel.h"
#include "intc.h"

uint32_t unknownSyscall(int nr)
{
	uint32_t epc;

	__asm__ __volatile__(
		"sync.p\n"
		"mfc0 %0,$14\n":"=r" (epc):);
	printf("Unknown syscall nr. %d called at 0x%x.\n", nr, epc);
	return 0;
}

void *syscallTable[130] = {
	/* 0 */
	0, 0, syscallSetCrtc, 0, syscallExit, 0, 0, 0, 0, 0,
	/* 10 */
	0, 0, 0, 0, 0, 0, 0, 0, syscallAddDmacHandler, 0,
	/* 20 */
	intc_enable_irq, intc_disable_irq, syscallEnableDmac, syscallDisableDmac, 0, 0, 0, 0, 0, 0,
	/* 30 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 40 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 50 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 60 */
	syscallRFU60, syscallRFU61, 0, 0, syscallCreateSema, iDeleteSema, iSignalSema, iSignalSema, WaitSema, 0,
	/* 70 */
	0, 0, 0, iDeleteSema, 0, 0, 0, 0, 0, 0,
	/* 80 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 90 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 100 */
	syscallRFU100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 110 */
	0, 0, 0, syscallSetGsIMR, 0, 0, 0, 0, 0, syscallSifSetDma,
	/* 120 */
	syscallSifSetDChain, syscallSifSetReg, syscallSifGetReg, 0, 0, 0, 0, 0, 0, 0,
};
