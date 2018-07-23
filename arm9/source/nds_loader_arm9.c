/*-----------------------------------------------------------------
 Copyright (C) 2005 - 2010
	Michael "Chishm" Chisholm
	Dave "WinterMute" Murphy

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

------------------------------------------------------------------*/
#include <string.h>
#include <nds.h>
#include <sys/stat.h>
#include <limits.h>

#include <unistd.h>
#include <fat.h>

#include "load_bin.h"

#include "nds_loader_arm9.h"
#define LCDC_BANK_C (u16*)0x06840000
#define STORED_FILE_CLUSTER (*(((u32*)LCDC_BANK_C) + 1))
#define INIT_DISC (*(((u32*)LCDC_BANK_C) + 2))

#define STORED_FILE_CLUSTER_OFFSET 4 
#define INIT_DISC_OFFSET 8
#define WANT_TO_PATCH_DLDI_OFFSET 12
#define ARG_START_OFFSET 16
#define ARG_SIZE_OFFSET 20
#define HAVE_DSISD_OFFSET 28
#define SAV_OFFSET 32
#define SAVSIZE_OFFSET 36
#define LANGUAGE_OFFSET 40
#define DSIMODE_OFFSET 44
#define PUR_OFFSET 48
#define PUS_OFFSET 52
#define CONSOLEMODEL_OFFSET 56
#define LOADSCR_OFFSET 60
#define ROMREADLED_OFFSET 64
#define GAMESOFTRESET_OFFSET 68
#define ASYNC_OFFSET 72
#define CARDENGINE_ARM7_OFFSET 76
#define CARDENGINE_ARM9_OFFSET 80

typedef signed int addr_t;
typedef unsigned char data_t;

#define FIX_ALL	0x01
#define FIX_GLUE	0x02
#define FIX_GOT	0x04
#define FIX_BSS	0x08

static char hexbuffer [9];
char* tohex(u32 n)
{
    unsigned size = 9;
    char *buffer = hexbuffer;
    unsigned index = size - 2;

	for (int i=0; i<size; i++) {
		buffer[i] = '0';
	}

    while (n > 0)
    {
        unsigned mod = n % 16;

        if (mod >= 10)
            buffer[index--] = (mod - 10) + 'A';
        else
            buffer[index--] = mod + '0';

        n /= 16;
    }
    buffer[size - 1] = '\0';
    return buffer;
}

static addr_t readAddr (data_t *mem, addr_t offset) {
	return ((addr_t*)mem)[offset/sizeof(addr_t)];
}

static void writeAddr (data_t *mem, addr_t offset, addr_t value) {
	((addr_t*)mem)[offset/sizeof(addr_t)] = value;
}

static void vramcpy (void* dst, const void* src, int len)
{
	u16* dst16 = (u16*)dst;
	u16* src16 = (u16*)src;
	
	//dmaCopy(src, dst, len);

	for ( ; len > 0; len -= 2) {
		*dst16++ = *src16++;
	}
}	

static addr_t quickFind (const data_t* data, const data_t* search, size_t dataLen, size_t searchLen) {
	const int* dataChunk = (const int*) data;
	int searchChunk = ((const int*)search)[0];
	addr_t i;
	addr_t dataChunkEnd = (addr_t)(dataLen / sizeof(int));

	for ( i = 0; i < dataChunkEnd; i++) {
		if (dataChunk[i] == searchChunk) {
			if ((i*sizeof(int) + searchLen) > dataLen) {
				return -1;
			}
			if (memcmp (&data[i*sizeof(int)], search, searchLen) == 0) {
				return i*sizeof(int);
			}
		}
	}

	return -1;
}

int runNds (const void* loader, u32 loaderSize, u32 cluster, u32 saveCluster, u32 saveSize, u32 language, u32 dsiMode, u32 patchMpuRegion, u32 patchMpuSize, u32 consoleModel, u32 loadingScreen, u32 romread_LED, u32 gameSoftReset, u32 asyncPrefetch, bool initDisc, bool dldiPatchNds, int argc, const char** argv, u32* cheat_data)
{
	char* argStart;
	u16* argData;
	u16 argTempVal = 0;
	int argSize;
	const char* argChar;
	
	nocashMessage("runNds");

	irqDisable(IRQ_ALL);

	// Direct CPU access to VRAM bank C
	VRAM_C_CR = VRAM_ENABLE | VRAM_C_LCD;
	VRAM_D_CR = VRAM_ENABLE | VRAM_D_LCD;	
	// Load the loader/patcher into the correct address
	vramcpy (LCDC_BANK_C, loader, loaderSize);

	// Set the parameters for the loader
	// STORED_FILE_CLUSTER = cluster;
	writeAddr ((data_t*) LCDC_BANK_C, STORED_FILE_CLUSTER_OFFSET, cluster);
	// INIT_DISC = initDisc;
	writeAddr ((data_t*) LCDC_BANK_C, INIT_DISC_OFFSET, initDisc);

	// Give arguments to loader
	argStart = (char*)LCDC_BANK_C + readAddr((data_t*)LCDC_BANK_C, ARG_START_OFFSET);
	argStart = (char*)(((int)argStart + 3) & ~3);	// Align to word
	argData = (u16*)argStart;
	argSize = 0;
	
	for (; argc > 0 && *argv; ++argv, --argc) 
	{
		for (argChar = *argv; *argChar != 0; ++argChar, ++argSize) 
		{
			if (argSize & 1) 
			{
				argTempVal |= (*argChar) << 8;
				*argData = argTempVal;
				++argData;
			} 
			else 
			{
				argTempVal = *argChar;
			}
		}
		if (argSize & 1)
		{
			*argData = argTempVal;
			++argData;
		}
		argTempVal = 0;
		++argSize;
	}
	*argData = argTempVal;
	
	writeAddr ((data_t*) LCDC_BANK_C, ARG_START_OFFSET, (addr_t)argStart - (addr_t)LCDC_BANK_C);
	writeAddr ((data_t*) LCDC_BANK_C, ARG_SIZE_OFFSET, argSize);
	
	writeAddr ((data_t*) LCDC_BANK_C, SAV_OFFSET, saveCluster);
	writeAddr ((data_t*) LCDC_BANK_C, SAVSIZE_OFFSET, saveSize);
	writeAddr ((data_t*) LCDC_BANK_C, LANGUAGE_OFFSET, language);
	writeAddr ((data_t*) LCDC_BANK_C, DSIMODE_OFFSET, dsiMode);
	writeAddr ((data_t*) LCDC_BANK_C, PUR_OFFSET, patchMpuRegion);
	writeAddr ((data_t*) LCDC_BANK_C, PUS_OFFSET, patchMpuSize);
	writeAddr ((data_t*) LCDC_BANK_C, CONSOLEMODEL_OFFSET, consoleModel);
	writeAddr ((data_t*) LCDC_BANK_C, LOADSCR_OFFSET, loadingScreen);
	writeAddr ((data_t*) LCDC_BANK_C, ROMREADLED_OFFSET, romread_LED);
	writeAddr ((data_t*) LCDC_BANK_C, GAMESOFTRESET_OFFSET, gameSoftReset);
	writeAddr ((data_t*) LCDC_BANK_C, ASYNC_OFFSET, asyncPrefetch);
    
    loadCheatData(cheat_data);

	nocashMessage("irqDisable(IRQ_ALL);");

	irqDisable(IRQ_ALL);

	nocashMessage("Give the VRAM to the ARM7");
	// Give the VRAM to the ARM7
	VRAM_C_CR = VRAM_ENABLE | VRAM_C_ARM7_0x06000000;	
	VRAM_D_CR = VRAM_ENABLE | VRAM_D_ARM7_0x06020000;		
	
	nocashMessage("Reset into a passme loop");
	// Reset into a passme loop
	REG_EXMEMCNT |= ARM7_OWNS_ROM | ARM7_OWNS_CARD;
	
	*((vu32*)0x02FFFFFC) = 0;
	*((vu32*)0x02FFFE04) = (u32)0xE59FF018;
	*((vu32*)0x02FFFE24) = (u32)0x02FFFE04;
	
	nocashMessage("resetARM7");

	resetARM7(0x06000000);	

	nocashMessage("swiSoftReset");

	swiSoftReset(); 
	return true;
}

int runNdsFile (const char* filename, const char* savename, int saveSize, int language, int dsiMode, int patchMpuRegion, int patchMpuSize, int consoleModel, int loadingScreen, int romread_LED, int gameSoftReset, int asyncPrefetch, int argc, const char** argv, u32* cheat_data) {
	struct stat st;
	struct stat stSav;
	u32 clusterSav = 0;
	char filePath[PATH_MAX];
	int pathLen;
	const char* args[1];

	
	if (stat (filename, &st) < 0) {
		return 1;
	}
	
	if (stat (savename, &stSav) >= 0) {
		clusterSav = stSav.st_ino;
	}

	if (argc <= 0 || !argv) {
		// Construct a command line if we weren't supplied with one
		if (!getcwd (filePath, PATH_MAX)) {
			return 2;
		}
		pathLen = strlen (filePath);
		strcpy (filePath + pathLen, filename);
		args[0] = filePath;
		argv = args;
	}

	bool havedsiSD = false;

	if(argv[0][0]=='s' && argv[0][1]=='d') havedsiSD = true;

	return runNds (load_bin, load_bin_size, st.st_ino, clusterSav, saveSize, language, dsiMode, patchMpuRegion, patchMpuSize, consoleModel, loadingScreen, romread_LED, gameSoftReset, asyncPrefetch, true, true, argc, argv, cheat_data);
}

static inline void copyLoop (u32* dest, const u32* src, u32 size) {
	size = (size +3) & ~3;
	do {
        writeAddr ((data_t*) dest, 0, *src);
		dest++;
        src++;
	} while (size -= 4);
}

int loadCheatData (u32* cheat_data) {
    nocashMessage("loadCheatData");
            
    u32 cardengineArm7Offset = ((u32*)load_bin)[CARDENGINE_ARM7_OFFSET/4];
    nocashMessage("cardengineArm7Offset");
    nocashMessage(tohex(cardengineArm7Offset));
    
    u32* cardengineArm7 = (u32*) (load_bin + cardengineArm7Offset);
    nocashMessage("cardengineArm7");
    nocashMessage(tohex(cardengineArm7));
    
    u32 cheatDataOffset = cardengineArm7[13];
    nocashMessage("cheatDataOffset");
    nocashMessage(tohex(cheatDataOffset));
    
    u32* cheatDataDest = (u32*) (((u32)LCDC_BANK_C) + cardengineArm7Offset + cheatDataOffset);
    nocashMessage("cheatDataDest");
    nocashMessage(tohex(cheatDataDest));
    
    copyLoop (cheatDataDest, (u32*)cheat_data, 1024);
    
    return true;
}

