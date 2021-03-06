
/* Copyright (c) 2007 Mega Man */
#include <stdio.h>

#include <kernel.h>
#include <sifrpc.h>
#include <libpad.h>
#include <loadfile.h>

#include "pad.h"
#include "graphic.h"
#include "kprint.h"

#define PADCOUNT 2

// pad_dma_buf is provided by the user, one buf for each pad
// contains the pad's current state
static char padBuf[PADCOUNT][256] __attribute__ ((aligned(64)));

static char actAlign[PADCOUNT][6];

static int actuators[PADCOUNT];

static int padInitialized[PADCOUNT];

int initializePad(int port, int slot);

void initializeController(void)
{
	int ret;

	int port;					// 0 -> Connector 1, 1 -> Connector 2

	int slot = 0;				// Always zero if not using multitap

	if (padInit(0) != 0) {
		kprintf("padInit() failed.\n");
		return;
	}

	for (port = 0; port < PADCOUNT; port++) {
		if ((ret = padPortOpen(port, slot, padBuf[port])) == 0) {
			kprintf("padOpenPort failed: %d\n", ret);
			return;
		}

		if (!initializePad(port, slot)) {
			kprintf("pad %d initalization failed!\n", port);
		}
	}
}

void deinitializeController(void)
{
	int ret;

	int port;					// 0 -> Connector 1, 1 -> Connector 2

	int slot = 0;				// Always zero if not using multitap

	for (port = 0; port < PADCOUNT; port++) {
		ret = padPortClose(port, slot);
		if (ret != 1) {
			kprintf("padClosePort failed: %d\n", ret);
		}
	}
	padEnd();
}

/*
 * isPadReady()
 */
int isPadReady(int port, int slot)
{
	int state;

	int laststate = -1;

	char stateString[16];

	state = padGetState(port, slot);
	while ((state != PAD_STATE_STABLE) && (state != PAD_STATE_FINDCTP1)) {
		if ((state == PAD_STATE_DISCONN)
			|| (state == PAD_STATE_ERROR))
			return 0;
		if (state != laststate) {
			padStateInt2String(state, stateString);
			kprintf("Please wait, pad(%d,%d) is in state %s\n",
				port, slot, stateString);
			laststate = state;
		}
		state = padGetState(port, slot);
	}
	return -1;
}

/*
 * initializePad()
 */
int initializePad(int port, int slot)
{

	int ret;

	int modes;

	int i;

	padInitialized[port] = 0;

	if (!isPadReady(port, slot))
		return 0;

	// How many different modes can this device operate in?
	// i.e. get # entrys in the modetable
	modes = padInfoMode(port, slot, PAD_MODETABLE, -1);
	kprintf("The device has %d modes\n", modes);

	if (modes > 0) {
		kprintf("( ");
		for (i = 0; i < modes; i++) {
			kprintf("%d ", padInfoMode(port, slot, PAD_MODETABLE, i));
		}
		kprintf(")");
	}

	kprintf("It is currently using mode %d\n",
		padInfoMode(port, slot, PAD_MODECURID, 0));

	// If modes == 0, this is not a Dual shock controller 
	// (it has no actuator engines)
	if (modes == 0) {
		kprintf("This is a digital controller?\n");
		padInitialized[port] = -1;
		return 1;
	}
	// Verify that the controller has a DUAL SHOCK mode
	i = 0;
	do {
		if (padInfoMode(port, slot, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK)
			break;
		i++;
	} while (i < modes);
	if (i >= modes) {
		kprintf("This is no Dual Shock controller\n");
		padInitialized[port] = -1;
		return 1;
	}
	// If ExId != 0x0 => This controller has actuator engines
	// This check should always pass if the Dual Shock test above passed
	ret = padInfoMode(port, slot, PAD_MODECUREXID, 0);
	if (ret == 0) {
		kprintf("This is no Dual Shock controller??\n");
		padInitialized[port] = -1;
		return 1;
	}

	kprintf("Enabling dual shock functions of pad %d\n", port);

	// When using MMODE_LOCK, user cant change mode with Select button
	padSetMainMode(port, slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);

	if (!isPadReady(port, slot)) {
		kprintf("Failed pad %d\n", port);
		return 0;
	}
	kprintf("infoPressMode: %d\n", padInfoPressMode(port, slot));

	if (!isPadReady(port, slot))
		return 0;
	kprintf("enterPressMode: %d\n", padEnterPressMode(port, slot));

	if (!isPadReady(port, slot))
		return 0;
	actuators[port] = padInfoAct(port, slot, -1, 0);
	kprintf("# of actuators: %d\n", actuators[port]);

	if (actuators[port] != 0) {
		actAlign[port][0] = 0;	// Enable small engine
		actAlign[port][1] = 1;	// Enable big engine
		actAlign[port][2] = 0xff;
		actAlign[port][3] = 0xff;
		actAlign[port][4] = 0xff;
		actAlign[port][5] = 0xff;

		if (!isPadReady(port, slot))
			return 0;
		kprintf("padSetActAlign: %d\n",
			padSetActAlign(port, slot, actAlign[port]));
	} else {
		kprintf("Did not find any actuators.\n");
	}

	if (!isPadReady(port, slot))
		return 0;

	padInitialized[port] = -1;
	return 1;
}

int readPad(int port)
{
	struct padButtonStatus buttons;

	u32 paddata;

	int ret;

	int slot = 0;

	if (!padInitialized[port])
		initializePad(port, slot);
	if (!padInitialized[port])
		return 0;

	ret = padGetState(port, slot);
	while ((ret != PAD_STATE_STABLE) && (ret != PAD_STATE_FINDCTP1)) {
		if (ret == PAD_STATE_DISCONN) {
			//kprintf("Pad(%d, %d) is disconnected\n", port, slot);
			return 0;
		}
		ret = padGetState(port, slot);
	}

	ret = padRead(port, slot, &buttons);	// port, slot, buttons

	if (ret != 0) {
		int d;

		paddata = 0xffff ^ buttons.btns;


#ifdef PAD_MOVE_SCREEN
		if (buttons.mode & 0x20) { /* PAD_TYPE_DUALSHOCK */
		//if (padInfoMode(port, slot, PAD_MODECURID, 0) == PAD_TYPE_DUALSHOCK) {
			/* Change screen position with left analog stick. */
			if (buttons.ljoy_h >= 0xbf) {
				d = (buttons.ljoy_h - 0xbf) / 0x10;
				d++;
				moveScreen(d, 0);
			}
			if (buttons.ljoy_h < 0x40) {
				d = (buttons.ljoy_h - 0x40) / 0x10;
				d--;
				moveScreen(d, 0);
			}
			if (buttons.ljoy_v >= 0xbf) {
				d = (buttons.ljoy_v - 0xbf) / 0x40;
				d++;
				moveScreen(0, d);
			}
			if (buttons.ljoy_v < 0x40) {
				d = (buttons.ljoy_v - 0x40) / 0x40;
				d--;
				moveScreen(0, d);
			}
		}
#endif

		return paddata;
	} else
		return 0;
}
