/* Copyright (c) 2007 Mega Man */
#ifndef _GRAPHIC_H_
#define _GRAPHIC_H_

#define MAX_INPUT_LEN 1024

#ifdef __cplusplus
#include "menu.h"
#include "config.h"

Menu *graphic_main(void);
void incrementMode(void);
void decrementMode(void);
void setMode(int mode);
int getModeFrequenzy(void);
void graphic_paint(void);
void graphic_auto_boot_paint(int time);
int setCurrentMenu(void *arg);
Menu *getCurrentMenu(void);
GSTEXTURE *getTexFolder(void);
GSTEXTURE *getTexUp(void);
GSTEXTURE *getTexBack(void);
GSTEXTURE *getTexSelected(void);
GSTEXTURE *getTexUnselected(void);
bool isNTSCMode(void);
int getCurrentMode(void);
#endif

#ifdef __cplusplus
extern "C" {
#endif
	void graphic_setPercentage(int percentage, const char *name);
	void setErrorMessage(const char *text);
	const char *getErrorMessage(void);
	int error_printf(const char *format, ...);
	void info_prints(const char *text);
	int info_printf(const char *format, ...);
	void graphic_setStatusMessage(const char *text);
	void setEnableDisc(int v);
	void goToNextErrorMessage(void);
	void scrollUpFast(void);
	void scrollUp(void);
	void scrollDownFast(void);
	void scrollDown(void);
	int getScrollPos(void);
	int isInfoBufferEmpty(void);
	void clearInfoBuffer(void);
	void enablePad(int val);
	void setInputBuffer(char *buffer, int writeable);
	char *getInputBuffer(void);
	int isWriteable(void);
	void graphic_screenshot(void);
	void moveScreen(int dx, int dy);
	void changeMode(void);
	int getCursorPos(void);
	void incCursorPos(void);
	void decCursorPos(void);
	void homeCursorPos(void);
	void endCursorPos(void);
	void setEmulatedKey(int key);

	extern int xoffset;
	extern int yoffset;
#ifdef __cplusplus
}
#endif

#endif
