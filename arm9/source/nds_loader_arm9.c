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
#include <string.h> // memcpy
#include <limits.h>
#include <unistd.h>
#include <nds/interrupts.h>
#include <nds/arm9/video.h>
#include <nds/memory.h>
#include <nds/system.h>
#include <nds/bios.h>

#include <nds/arm9/dldi.h>
#include <nds/debug.h>
#include <sys/stat.h>
#include <fat.h>

#include "hex.h"
#include "nds_loader_arm9.h"
#include "locations.h"
#include "load_crt0.h"
#include "cardengine_header_arm7.h"

#include "load_bin.h"

//#define memcpy __builtin_memcpy

//#define LCDC_BANK_C (u16*)0x06840000

loadCrt0* lc0 = (loadCrt0*)LOAD_CRT0_LOCATION;

/*typedef signed int addr_t;  // s32
typedef unsigned char data_t; // u8*/

/*#define FIX_ALL  0x01
#define FIX_GLUE 0x02
#define FIX_GOT  0x04
#define FIX_BSS  0x08*/

/*enum DldiOffsets {
	DO_magicString      = 0x00, // "\xED\xA5\x8D\xBF Chishm"
	DO_magicToken       = 0x00, // 0xBF8DA5ED
	DO_magicShortString = 0x04, // " Chishm"
	DO_version          = 0x0C,
	DO_driverSize       = 0x0D,
	DO_fixSections      = 0x0E,
	DO_allocatedSpace   = 0x0F,

	DO_friendlyName = 0x10,

	DO_text_start = 0x40, // Data start
	DO_data_end   = 0x44, // Data end
	DO_glue_start = 0x48, // Interworking glue start	-- Needs address fixing
	DO_glue_end   = 0x4C, // Interworking glue end
	DO_got_start  = 0x50, // GOT start					-- Needs address fixing
	DO_got_end    = 0x54, // GOT end
	DO_bss_start  = 0x58, // bss start					-- Needs setting to zero
	DO_bss_end    = 0x5C, // bss end

	// IO_INTERFACE data
	DO_ioType       = 0x60,
	DO_features     = 0x64,
	DO_startup      = 0x68,	
	DO_isInserted   = 0x6C,	
	DO_readSectors  = 0x70,	
	DO_writeSectors = 0x74,
	DO_clearStatus  = 0x78,
	DO_shutdown     = 0x7C,
	DO_code         = 0x80
};*/


/*static inline addr_t readAddr(data_t *mem, addr_t offset) {
	return ((addr_t*)mem)[offset/sizeof(addr_t)];
}

static inline void writeAddr(data_t *mem, addr_t offset, addr_t value) {
	((addr_t*)mem)[offset/sizeof(addr_t)] = value;
}*/

/*static inline u32 readAddr(const u8* mem, u32 offset) {
	return ((u32*)mem)[offset/sizeof(u32)];
}

static inline void writeAddr(u8* mem, u32 offset, u32 value) {
	((u32*)mem)[offset/sizeof(u32)] = value;
}*/

/*static inline void vramcpy(void* dst, const void* src, int len) {
	u16* dst16 = (u16*)dst;
	u16* src16 = (u16*)src;
	
	//dmaCopy(src, dst, len);

	for (; len > 0; len -= 2) {
		*dst16++ = *src16++;
	}
}*/
/*static inline void vramcpy(u16* dst, const u16* src, u32 size) {
	size = (size +1) & ~1; // Bigger nearest multiple of 2
	do {
		*dst++ = *src++;
	} while (size -= 2);
}*/

// See: arm7/source/main.c
/*static u32 quickFind (const unsigned char* data, const unsigned char* search, u32 dataSize, u32 searchSize) {
	const int* dataChunk = (const int*) data;
	int searchChunk = ((const int*)search)[0];
	u32 i;
	u32 dataLen = (u32)(dataSize / sizeof(int));

	for ( i = 0; i < dataLen; i++) {
		if (dataChunk[i] == searchChunk) {
			if ((i*sizeof(int) + searchSize) > dataSize) {
				return -1;
			}
			if (memcmp (&data[i*sizeof(int)], search, searchSize) == 0) {
				return i*sizeof(int);
			}
		}
	}

	return -1;
}*/

/*static inline void copyLoop(u32* dest, const u32* src, u32 size) {
	size = (size +3) & ~3; // Bigger nearest multiple of 4
	do {
		*dest = *src; //writeAddr((u8*)dest, 0, *src);
		dest++;
		src++;
	} while (size -= 4);
}*/

int loadArgs(int argc, const char** argv) {
	// Give arguments to loader

	char* argStart = (char*)lc0 + lc0->argStart;
	argStart = (char*)(((int)argStart + 3) & ~3); // Align to word
	u16* argData = (u16*)argStart;
	int argSize = 0;
	u16 argTempVal = 0;
	
	for (; argc > 0 && *argv; ++argv, --argc) {
		for (const char* argChar = *argv; *argChar != 0; ++argChar, ++argSize) {
			if (argSize & 1) {
				argTempVal |= (*argChar) << 8;
				*argData = argTempVal;
				++argData;
			} else {
				argTempVal = *argChar;
			}
		}
		if (argSize & 1) {
			*argData = argTempVal;
			++argData;
		}
		argTempVal = 0;
		++argSize;
	}
	*argData = argTempVal;

	lc0->argStart = (u32)argStart - (u32)lc0;
	lc0->argSize  = argSize;

	return true;
}

int loadCheatData(u32* cheat_data, u32 cheat_data_len) {
	nocashMessage("loadCheatData");

	cardengineArm7* ce7 = getCardengineArm7(lc0);
	nocashMessage("ce7");
	nocashMessage(tohex((u32)ce7));
	
	u32* ce7_cheat_data = getCheatData(ce7);
	nocashMessage("ce7_cheat_data");
	nocashMessage(tohex((u32)ce7_cheat_data));
	
	//memcpy(ce7_cheat_data, cheat_data, 32768);
	memcpy(ce7_cheat_data, cheat_data, cheat_data_len*sizeof(u32));

	ce7->cheat_data_len = cheat_data_len;
	
	return true;
}

int runNds(
	const void* loader,
	u32 loaderSize,
	u32 cluster,
	u32 saveCluster,
	u32 saveSize,
	u32 language,
	bool dsiMode,
	u32 donorSdkVer,
	u32 patchMpuRegion,
	u32 patchMpuSize,
	u32 consoleModel,
	u32 loadingScreen,
	u32 romread_LED,
	bool gameSoftReset,
	bool asyncPrefetch,
	bool extendedCache,
	bool logging,
	bool initDisc,
	bool dldiPatchNds,
	int argc, const char** argv,
	u32* cheat_data, u32 cheat_data_len
) {
	nocashMessage("runNds");

	irqDisable(IRQ_ALL);

	// Direct CPU access to VRAM bank C
	VRAM_C_CR = VRAM_ENABLE | VRAM_C_LCD;
	VRAM_D_CR = VRAM_ENABLE | VRAM_D_LCD;

	// Load the loader into the correct address
	memcpy(lc0, loader, loaderSize); //vramcpy(LCDC_BANK_C, loader, loaderSize);

	// Set the parameters for the loader

	lc0->storedFileCluster = cluster;
	lc0->initDisc = initDisc;
	lc0->wantToPatchDLDI = dldiPatchNds;

	loadArgs(argc, argv);

	lc0->saveFileCluster = saveCluster;
	lc0->saveSize        = saveSize;
	lc0->language        = language;
	lc0->dsiMode         = dsiMode; // SDK 5
	lc0->donorSdkVer     = donorSdkVer;
	lc0->patchMpuRegion  = patchMpuRegion;
	lc0->patchMpuSize    = patchMpuSize;
	lc0->consoleModel    = consoleModel;
	lc0->loadingScreen   = loadingScreen;
	lc0->romread_LED     = romread_LED;
	lc0->gameSoftReset   = gameSoftReset;
	lc0->asyncPrefetch   = asyncPrefetch;
	lc0->extendedCache   = extendedCache;
	lc0->logging         = logging;

	loadCheatData(cheat_data, cheat_data_len);

	nocashMessage("irqDisable(IRQ_ALL);");
	irqDisable(IRQ_ALL);

	// Give the VRAM to the ARM7
	nocashMessage("Give the VRAM to the ARM7");
	VRAM_C_CR = VRAM_ENABLE | VRAM_C_ARM7_0x06000000;	
	VRAM_D_CR = VRAM_ENABLE | VRAM_D_ARM7_0x06020000;		
	
	// Reset into a passme loop
	nocashMessage("Reset into a passme loop");
	REG_EXMEMCNT |= ARM7_OWNS_ROM | ARM7_OWNS_CARD;
	
	*(vu32*)0x02FFFFFC = 0;
	*(vu32*)0x02FFFE04 = (u32)0xE59FF018;
	*(vu32*)0x02FFFE24 = (u32)0x02FFFE04;
	
	// Reset ARM7
	nocashMessage("resetARM7");
	resetARM7(0x06000000);	

	// swi soft reset
	nocashMessage("swiSoftReset");
	swiSoftReset();

	return true;
}

int runNdsFile(
	const char* filename,
	const char* savename,
	u32 saveSize,
	u32 language,
	bool dsiMode, // SDK 5
	u32 donorSdkVer,
	u32 patchMpuRegion,
	u32 patchMpuSize,
	u32 consoleModel,
	u32 loadingScreen,
	u32 romread_LED,
	bool gameSoftReset,
	bool asyncPrefetch,
	bool extendedCache,
	bool logging,
	int argc, const char** argv,
	u32* cheat_data, u32 cheat_data_len
) {
	struct stat st;
	struct stat stSav;
	u32 clusterSav = 0;
	char filePath[PATH_MAX];
	int pathLen;
	const char* args[1];

	if (stat(filename, &st) < 0) {
		return 1;
	}
	
	if (stat(savename, &stSav) >= 0) {
		clusterSav = stSav.st_ino;
	}
	
	if (argc <= 0 || !argv) {
		// Construct a command line if we weren't supplied with one
		if (!getcwd(filePath, PATH_MAX)) {
			return 2;
		}
		pathLen = strlen(filePath);
		strcpy(filePath + pathLen, filename);
		args[0] = filePath;
		argv = args;
	}

	//bool havedsiSD = false;
	//bool havedsiSD = (argv[0][0] == 's' && argv[0][1] == 'd');

	return runNds(
		load_bin,
		load_bin_size,
		st.st_ino,
		clusterSav,
		saveSize,
		language,
		dsiMode, // SDK 5
		donorSdkVer,
		patchMpuRegion,
		patchMpuSize,
		consoleModel,
		loadingScreen,
		romread_LED,
		gameSoftReset,
		asyncPrefetch,
		extendedCache,
		logging,
		true,
		true,
		argc, argv,
		cheat_data, cheat_data_len
	);
}
