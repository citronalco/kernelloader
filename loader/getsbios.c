/* Copyright (c) 2007 Mega Man */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include "fileXio_rpc.h"

#include "SMS_CDVD.h"
#include "SMS_CDDA.h"
#include "graphic.h"
#include "getsbios.h"
#include "configuration.h"
#include "modules.h"
#include "kprint.h"

char rteElf[MAX_INPUT_LEN] = "cdfs:pbpx_955.09";
char rteElfOffset[MAX_INPUT_LEN] = "16773120";
static char magic_string[] __attribute__((aligned(4))) = "PS2b";

static int real_copyRTESBIOS(void *arg)
{
	FILE *fin;
	const char *filename;
	uint32_t *magic = (uint32_t *) magic_string;
	uint32_t offset = 0;
	uint32_t sbiosSize = 0xf000;

	(void) arg;

	filename = rteElf;
	kprintf("Search for sbios in \"%s\".\n", filename);

	graphic_setPercentage(0, filename);

	offset = atoi(rteElfOffset);

	fin = fopen(filename, "rb");
	if (fin != NULL)
	{
		char *buffer;
		uint32_t size;

		filename = NULL;
		fseek(fin, 0, SEEK_END);
		size = ftell(fin);
		fseek(fin, 0, SEEK_SET);
		buffer = malloc(size);
		if (buffer != NULL)
		{
			FILE *fout;
			char *addr;
			char *endaddr;

			if (fread(buffer, size, 1, fin) != 1)
			{
				fclose(fin);
				free(buffer);
				error_printf("Can't read file.");
				return -3;
			}
			fclose(fin);

			endaddr = (char *) (((uint32_t) buffer) + size);
			for (addr = buffer; addr < endaddr; addr+=4)
			{
				if (*((uint32_t *)addr) == *magic)
				{
					break;
				}
			}
			if (*((uint32_t *)addr) == *magic)
			{
				uint32_t regs[32];
				void *code;
				addr -= 4;

				kprintf("Found sbios.bin at file offset 0x%08x.\n",
					((uint32_t) addr) - ((uint32_t) buffer));

				if (offset != 0) {
					uint32_t search;

					search = addr - buffer;
					search += offset;
					kprintf("Search 0x%08x\n", search);
					for (code = buffer; code < ((void *) endaddr); code += 4) {
						uint32_t value;

						value = *((uint32_t *)code);
						if ((value >> 26) == 9) {
							/* addiu instruction. */
							int rs;
							int rt;
							int immidiate;

							rs = (value >> 21) & 0x1f;
							rt = (value >> 16) & 0x1f;
							immidiate = ((int16_t) ((uint16_t) (value & 0xFFFF)));

							regs[rt] = regs[rs] + immidiate;
							//kprintf("addiu %d, %d, 0x%04x\n", rt, rs, immidiate);

							if (regs[rt] == search) {
								uint32_t pos;
								pos = (uint32_t) code - (uint32_t) buffer;
								kprintf("Loadded at 0x%08x (file position 0x%08x).\n", pos + offset, pos);
							}
						}
						if ((value >> 26) == 15) {
							int rs;
							int rt;
							int immidiate;

							/* lui instruction */
							rs = (value >> 21) & 0x1f;
							rt = (value >> 16) & 0x1f;
							immidiate = ((int16_t) ((uint16_t) (value & 0xFFFF)));

							if (rs == 0) {
								regs[rt] = immidiate << 16;
								//kprintf("lui %d, 0x%08x\n", rt, regs[rt]);
							}
						}
						if ((value >> 26) == 0) {
							/* Special */
							if ((value & 0x3f) == 35) {
								/* subu instruction */
								int rs;
								int rt;
								int rd;

								rs = (value >> 21) & 0x1f;
								rt = (value >> 16) & 0x1f;
								rd = (value >> 11) & 0x1f;

								if (regs[rt] == search) {
									kprintf("0x%08x = 0x%08x - 0x%08x\n", regs[rs] - regs[rt], regs[rs], regs[rt]);
								}
								regs[rd] = regs[rs] - regs[rt];
							}
						}
						if ((value >> 26) == 3) {
							/* jal instruction. */
							if (regs[5] == search) {
								/* memcopy gets SBIOS in register a1 and size in register a2. */
								sbiosSize = regs[6];
								kprintf("SBIOS size is 0x%08x.\n", sbiosSize);
								break;
							}
						}
					}
				}

				fout = fopen("mc0:kloader/sbios.bin", "wb");
				if (fout == NULL) {
					fileXioMkdir(CONFIG_DIR, 0777);
					fout = fopen("mc0:kloader/sbios.bin", "wb");
				}
				if (fout != NULL)
				{
					if (fwrite(addr, sbiosSize, 1, fout) != 1)
					{
						fclose(fout);
						fioRemove("mc0:kloader/sbios.bin");
						fioRmdir("mc0:kloader/sbios.bin"); /* Needed because of bug in fioRemove. */
						free(buffer);
						error_printf("Failed to write sbios.bin");
						return -5;
					}
					fclose(fout);
					kprintf("sbios.bin written.\n");
				}
				else
				{
					error_printf("Failed to open sbios.bin");
					free(buffer);
					return -4;
				}
			}
			else
			{
				error_printf("Can't find sbios.");
				free(buffer);
				return -6;
			}
			free(buffer);
			buffer = NULL;
		}
		else
		{
			error_printf("out of memory");
			return -2;
		}
	}
	else
	{
		error_printf("Failed to open file \"%s\"", filename);
	}
	
	return 0;
}

int copyRTESBIOS(void *arg)
{
	DiskType type;
	int rv;

	setEnableDisc(-1);

	if (isDVDVSupported()) {
		type = CDDA_DiskType();
	
		/* Detect disk type, so loading will work. */
		if (type == DiskType_DVDV) {
			CDVD_SetDVDV(1);
		} else {
			CDVD_SetDVDV(0);
		}
	}

	rv = real_copyRTESBIOS(arg);

	if (isDVDVSupported()) {
		/* Always stop CD/DVD when an error happened. */
		CDVD_Stop();
		CDVD_FlushCache();
	}

	setEnableDisc(0);

	if (getErrorMessage() == NULL) {
		graphic_setPercentage(0, NULL);
	}

	return 0;
}
