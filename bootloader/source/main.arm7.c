/*-----------------------------------------------------------------
 boot.c
 
 BootLoader
 Loads a file into memory and runs it

 All resetMemory and startBinary functions are based 
 on the MultiNDS loader by Darkain.
 Original source available at:
 http://cvs.sourceforge.net/viewcvs.py/ndslib/ndslib/examples/loader/boot/main.cpp

License:
 Copyright (C) 2005  Michael "Chishm" Chisholm

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

 If you use this code, please give due credit and email me about your
 project at chishm@hotmail.com
 
Helpful information:
 This code runs from VRAM bank C on ARM7
------------------------------------------------------------------*/

#ifndef ARM7
# define ARM7
#endif
#include <nds/ndstypes.h>
#include <nds/dma.h>
#include <nds/system.h>
#include <nds/interrupts.h>
#include <nds/timers.h>
#include <nds/arm7/audio.h>

#include "fat.h"
#include "i2c.h"
//#include "dldi_patcher.h"
#include "card.h"
#include "card_patcher.h"
#include "cardengine_arm7_bin.h"
#include "cardengine_arm9_bin.h"
#include "hook.h"
#include "common.h"

void arm7clearRAM();
int sdmmc_sdcard_readsectors(u32 sector_no, u32 numsectors, void *out);
int sdmmc_sdcard_init();
void sdmmc_controller_init();

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Important things
#define TEMP_MEM 0x02FFE000
#define NDS_HEAD 0x027FFE00
#define TEMP_ARM9_START_ADDRESS (*(vu32*)0x027FFFF4)

#define ENGINE_LOCATION_ARM7  	0x0237C000
#define ENGINE_LOCATION_ARM9  	0x0237B800

const char* bootName = "BOOT.NDS";

extern unsigned long _start;
extern unsigned long storedFileCluster;
extern unsigned long initDisc;
extern unsigned long wantToPatchDLDI;
extern unsigned long argStart;
extern unsigned long argSize;
extern unsigned long dsiSD;
extern unsigned long saveFileCluster;
extern unsigned long saveSize;
extern unsigned long language;
extern unsigned long donorSdkVer;
extern unsigned long patchMpuRegion;
extern unsigned long patchMpuSize;
extern unsigned long loadingScreen;

static aFile * romFile = (aFile *)0x237B000;
static aFile * savFile = ((aFile *)0x237B000)+1;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Used for debugging purposes
static void errorOutput (void) {
	if(loadingScreen > 0) {
		// Wait until the ARM9 is ready
		while (arm9_stateFlag != ARM9_READY);
		// Set the error code, then tell ARM9 to display it
		arm9_errorColor = true;
	}
	// Stop
	while(1);
}

static void debugOutput (void) {
	if(loadingScreen > 0) {
		// Wait until the ARM9 is ready
		while (arm9_stateFlag != ARM9_READY);
		// Set the error code, then tell ARM9 to display it
		arm9_screenMode = loadingScreen-1;
		arm9_stateFlag = ARM9_DISPERR;
		// Wait for completion
		while (arm9_stateFlag != ARM9_READY);
	}
}

static void increaseLoadBarLength (void) {
	arm9_loadBarLength++;
	if(loadingScreen == 1) debugOutput();	// Let the loading bar finish before ROM starts
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Firmware stuff

#define FW_READ        0x03

void boot_readFirmware (uint32 address, uint8 * buffer, uint32 size) {
  uint32 index;

  // Read command
  while (REG_SPICNT & SPI_BUSY);
  REG_SPICNT = SPI_ENABLE | SPI_CONTINUOUS | SPI_DEVICE_NVRAM;
  REG_SPIDATA = FW_READ;
  while (REG_SPICNT & SPI_BUSY);

  // Set the address
  REG_SPIDATA =  (address>>16) & 0xFF;
  while (REG_SPICNT & SPI_BUSY);
  REG_SPIDATA =  (address>>8) & 0xFF;
  while (REG_SPICNT & SPI_BUSY);
  REG_SPIDATA =  (address) & 0xFF;
  while (REG_SPICNT & SPI_BUSY);

  for (index = 0; index < size; index++) {
    REG_SPIDATA = 0;
    while (REG_SPICNT & SPI_BUSY);
    buffer[index] = REG_SPIDATA & 0xFF;
  }
  REG_SPICNT = 0;
}

//#define resetCpu() __asm volatile("\tswi 0x000000\n");




/*-------------------------------------------------------------------------
resetMemory_ARM7
Clears all of the NDS's RAM that is visible to the ARM7
Written by Darkain.
Modified by Chishm:
 * Added STMIA clear mem loop
--------------------------------------------------------------------------*/
void resetMemory_ARM7 (void)
{
	int i;
	u8 settings1, settings2;
	u32 settingsOffset = 0;

	REG_IME = 0;

	for (i=0; i<16; i++) {
		SCHANNEL_CR(i) = 0;
		SCHANNEL_TIMER(i) = 0;
		SCHANNEL_SOURCE(i) = 0;
		SCHANNEL_LENGTH(i) = 0;
	}

	REG_SOUNDCNT = 0;

	//clear out ARM7 DMA channels and timers
	for (i=0; i<4; i++) {
		DMA_CR(i) = 0;
		DMA_SRC(i) = 0;
		DMA_DEST(i) = 0;
		TIMER_CR(i) = 0;
		TIMER_DATA(i) = 0;
	}

	arm7clearRAM();

	REG_IE = 0;
	REG_IF = ~0;
	(*(vu32*)(0x04000000-4)) = 0;  //IRQ_HANDLER ARM7 version
	(*(vu32*)(0x04000000-8)) = ~0; //VBLANK_INTR_WAIT_FLAGS, ARM7 version
	REG_POWERCNT = 1;  //turn off power to stuff

	// Get settings location
	boot_readFirmware((u32)0x00020, (u8*)&settingsOffset, 0x2);
	settingsOffset *= 8;

	// Reload DS Firmware settings
	boot_readFirmware(settingsOffset + 0x070, &settings1, 0x1);
	boot_readFirmware(settingsOffset + 0x170, &settings2, 0x1);

	if ((settings1 & 0x7F) == ((settings2+1) & 0x7F)) {
		boot_readFirmware(settingsOffset + 0x000, (u8*)0x027FFC80, 0x70);
	} else {
		boot_readFirmware(settingsOffset + 0x100, (u8*)0x027FFC80, 0x70);
	}
	if (language >= 0 && language < 6) {
		*(u8*)(0x027FFCE4) = language;	// Change language
	}
}


u32 ROMinRAM = false;
u32 ROM_TID;
u32 ROM_HEADERCRC;
u32 ARM9_LEN;
u32 fatSize;
u32 romSize;
u32 romSizeNoArm9;

void loadBinary_ARM7 (aFile file)
{
	u32 ndsHeader[0x170>>2];

	nocashMessage("loadBinary_ARM7");

	// read NDS header
	fileRead ((char*)ndsHeader, file, 0, 0x170);
	// read ARM9 info from NDS header
	u32 ARM9_SRC = ndsHeader[0x020>>2];
	char* ARM9_DST = (char*)ndsHeader[0x028>>2];
	ARM9_LEN = ndsHeader[0x02C>>2];
	// read ARM7 info from NDS header
	u32 ARM7_SRC = ndsHeader[0x030>>2];
	char* ARM7_DST = (char*)ndsHeader[0x038>>2];
	u32 ARM7_LEN = ndsHeader[0x03C>>2];

	ROM_TID = ndsHeader[0x00C>>2];
	fatSize = ndsHeader[0x04C>>2];
	romSize = ndsHeader[0x080>>2];
	romSizeNoArm9 = romSize-0x4000-ARM9_LEN;
	ROM_HEADERCRC = ndsHeader[0x15C>>2];

	//Fix Pokemon games needing header data.
	fileRead ((char*)0x027FF000, file, 0, 0x170);

	if((*(u32*)(0x27FF00C) & 0x00FFFFFF) == 0x414441	// Diamond
	|| (*(u32*)(0x27FF00C) & 0x00FFFFFF) == 0x415041	// Pearl
	|| (*(u32*)(0x27FF00C) & 0x00FFFFFF) == 0x555043	// Platinum
	|| (*(u32*)(0x27FF00C) & 0x00FFFFFF) == 0x4B5049	// HG
	|| (*(u32*)(0x27FF00C) & 0x00FFFFFF) == 0x475049)	// SS
	{
		*(u32*)(0x27FF00C) = 0x4A414441;//Make the Pokemon game code ADAJ.
	}
	
	// Load binaries into memory
	fileRead(ARM9_DST, file, ARM9_SRC, ARM9_LEN);
	fileRead(ARM7_DST, file, ARM7_SRC, ARM7_LEN);
	
	// The World Ends With You (USA) (Europe)
	if(ROM_TID == 0x454C5741 || ROM_TID == 0x504C5741){
		*(u32*)(0x203E7B0) = 0;
	}

	// Subarashiki Kono Sekai - It's a Wonderful World (Japan)
	if(ROM_TID == 0x4A4C5741){
		*(u32*)(0x203F114) = 0;
	}

	// Miami Nights - Singles in the City (USA)
	if(ROM_TID == 0x45575641){
		//fixes not enough memory error
		*(u32*)(0x0204cccc) = 0xe1a00000; //nop
	}

	// Miami Nights - Singles in the City (Europe)
	if(ROM_TID == 0x50575641){
		//fixes not enough memory error
		*(u32*)(0x0204cdbc) = 0xe1a00000; //nop
	}
	
	// 0735 - Castlevania - Portrait of Ruin (USA)
	if(ROM_TID == 0x45424341){
		*(u32*)(0x02007910) = 0xeb02508e;
		*(u32*)(0x02007918) = 0xea000004;
		*(u32*)(0x02007a00) = 0xeb025052;
		*(u32*)(0x02007a08) = 0xe59f1030;
		*(u32*)(0x02007a0c) = 0xe59f0028;
		*(u32*)(0x02007a10) = 0xe0281097;
		*(u32*)(0x02007a14) = 0xea000003;	
	}
	
	// 0676 - Akumajou Dracula - Gallery of Labyrinth (Japan)
	if(ROM_TID == 0x4a424341){
		*(u32*)(0x02007910) = 0xeb0250b0;
		*(u32*)(0x02007918) = 0xea000004;
		*(u32*)(0x02007a00) = 0xeb025074;
		*(u32*)(0x02007a08) = 0xe59f1030;
		*(u32*)(0x02007a0c) = 0xe59f0028;
		*(u32*)(0x02007a10) = 0xe0281097;
		*(u32*)(0x02007a14) = 0xea000003;	
	}
	
	// 0881 - Castlevania - Portrait of Ruin (Europe) (En,Fr,De,Es,It)
	if(ROM_TID == 0x50424341){
		*(u32*)(0x02007b00) = 0xeb025370;
		*(u32*)(0x02007b08) = 0xea000004;
		*(u32*)(0x02007bf0) = 0xeb025334;
		*(u32*)(0x02007bf8) = 0xe59f1030;
		*(u32*)(0x02007bfc) = 0xe59f0028;
		*(u32*)(0x02007c00) = 0xe0281097;
		*(u32*)(0x02007c04) = 0xea000003;	
	}

	// "Chrono Trigger (Japan)"
	if(ROM_TID == 0x4a555159){
		decompressLZ77Backwards((uint8_t*)ARM9_DST, ARM9_LEN);
		*(u32*)(0x0204e364) = 0xe3a00000; //mov r0, #0
		*(u32*)(0x0204e368) = 0xe12fff1e; //bx lr
		*(u32*)(0x0204e6c4) = 0xe3a00000; //mov r0, #0
		*(u32*)(0x0204e6c8) = 0xe12fff1e; //bx lr
	}

	// "Chrono Trigger (USA/Europe)"
	if(ROM_TID == 0x45555159 || ROM_TID == 0x50555159){
		decompressLZ77Backwards((uint8_t*)ARM9_DST, ARM9_LEN);
		*(u32*)(0x0204e334) = 0xe3a00000; //mov r0, #0
		*(u32*)(0x0204e338) = 0xe12fff1e; //bx lr
		*(u32*)(0x0204e694) = 0xe3a00000; //mov r0, #0
		*(u32*)(0x0204e698) = 0xe12fff1e; //bx lr
	}
	
	// "Dementium II (USA/EUR)"
	if(ROM_TID == 0x45454442 || ROM_TID == 0x50454442){
		*(u32*)(0x020e9120) = 0xe3a00002;
		*(u32*)(0x020e9124) = 0xea000029;
	}
	
	// "Dementium II: Tozasareta Byoutou (JPN)"
	if(ROM_TID == 0x4a454442){
		*(u32*)(0x020d9f60) = 0xe3a00005;
		*(u32*)(0x020d9f68) = 0xea000029;
	}

	// "Grand Theft Auto - Chinatown Wars (USA) (En,Fr,De,Es,It)"
	// "Grand Theft Auto - Chinatown Wars (Europe) (En,Fr,De,Es,It)"
	if(ROM_TID == 0x45584759 || ROM_TID == 0x50584759){
		*(u16*)(0x02037a34) = 0x46c0;
		*(u32*)(0x0216ac0c) = 0x0001fffb;
	}

	// "WarioWare: DIY (USA)"
	if(ROM_TID == 0x45524F55){
		*(u32*)(0x02003114) = 0xE12FFF1E; //mov r0, #0
	}

	// first copy the header to its proper location, excluding
	// the ARM9 start address, so as not to start it
	TEMP_ARM9_START_ADDRESS = ndsHeader[0x024>>2];		// Store for later
	ndsHeader[0x024>>2] = 0;
	dmaCopyWords(3, (void*)ndsHeader, (void*)NDS_HEAD, 0x170);

	// Switch to NTR mode BIOS (no effect with locked arm7 SCFG)
	//nocashMessage("Switch to NTR mode BIOS");
	//REG_SCFG_ROM = 0x703;
}

u32 enableExceptionHandler = true;

void setArm9Stuff(aFile file) {
	// ExceptionHandler2 (red screen) blacklist
	if((ROM_TID & 0x00FFFFFF) == 0x4D5341	// SM64DS
	|| (ROM_TID & 0x00FFFFFF) == 0x534D53	// SMSW
	|| (ROM_TID & 0x00FFFFFF) == 0x443241	// NSMB
	|| (ROM_TID & 0x00FFFFFF) == 0x4D4441)	// AC:WW
	{
		enableExceptionHandler = false;
	}

	hookNdsRetail9((u32*)ENGINE_LOCATION_ARM9);
}

/*-------------------------------------------------------------------------
startBinary_ARM7
Jumps to the ARM7 NDS binary in sync with the display and ARM9
Written by Darkain.
Modified by Chishm:
 * Removed MultiNDS specific stuff
--------------------------------------------------------------------------*/
void startBinary_ARM7 (void) {
	REG_IME=0;
	while(REG_VCOUNT!=191);
	while(REG_VCOUNT==191);
	// copy NDS ARM9 start address into the header, starting ARM9
	*((vu32*)0x027FFE24) = TEMP_ARM9_START_ADDRESS;
	// Get the ARM9 to boot
	arm9_stateFlag = ARM9_BOOTBIN;

	while(REG_VCOUNT!=191);
	while(REG_VCOUNT==191);
	// Start ARM7
	VoidFn arm7code = *(VoidFn*)(0x27FFE34);
	arm7code();
}

int sdmmc_sd_readsectors(u32 sector_no, u32 numsectors, void *out);
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Main function
bool sdmmc_inserted() {
	return true;
}

bool sdmmc_startup() {
	sdmmc_controller_init();
	return sdmmc_sdcard_init() == 0;
}

bool sdmmc_readsectors(u32 sector_no, u32 numsectors, void *out) {
	return sdmmc_sdcard_readsectors(sector_no, numsectors, out) == 0;
}

static u32 quickFind (const unsigned char* data, const unsigned char* search, u32 dataLen, u32 searchLen) {
	const int* dataChunk = (const int*) data;
	int searchChunk = ((const int*)search)[0];
	u32 i;
	u32 dataChunkEnd = (u32)(dataLen / sizeof(int));

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

static const unsigned char dldiMagicString[] = "\xED\xA5\x8D\xBF Chishm";	// Normal DLDI file

void arm7_main (void) {
	nocashMessage("bootloader");

    // Wait for ARM9 to at least start
	while (arm9_stateFlag < ARM9_START);

	// Get ARM7 to clear RAM
	nocashMessage("Get ARM7 to clear RAM");
	debugOutput();	// 1 dot
	resetMemory_ARM7();


	if (dsiSD) {
		_io_dldi.fn_readSectors = sdmmc_readsectors;
		_io_dldi.fn_isInserted = sdmmc_inserted;
		_io_dldi.fn_startup = sdmmc_startup;
	}

	// Init card
	if(!FAT_InitFiles(initDisc))
	{
		nocashMessage("!FAT_InitFiles");
		return -1;
	}

	*romFile = getFileFromCluster (storedFileCluster);

	if ((romFile->firstCluster < CLUSTER_FIRST) || (romFile->firstCluster >= CLUSTER_EOF)) 	/* Invalid file cluster specified */
	{
		*romFile = getBootFileCluster(bootName);
	}
	if (romFile->firstCluster == CLUSTER_FREE)
	{
		nocashMessage("fileCluster == CLUSTER_FREE");
		return -1;
	}
    
    //buildFatTableCache(romFile, 3);
    
    *savFile = getFileFromCluster(saveFileCluster);
    
    if (savFile->firstCluster != CLUSTER_FREE)
    {
		//buildFatTableCache(savFile, 3);
	}

	int errorCode;

	*(u16*)(0x4000500) = 0x807F;

	// Load the NDS file
	nocashMessage("Load the NDS file");
	loadBinary_ARM7(*romFile);
	increaseLoadBarLength();	// 2 dots

	nocashMessage("try to patch card");
	copyLoop (ENGINE_LOCATION_ARM7, (u32*)cardengine_arm7_bin, cardengine_arm7_bin_size);
	increaseLoadBarLength();	// 3 dots
	copyLoop (ENGINE_LOCATION_ARM9, (u32*)cardengine_arm9_bin, cardengine_arm9_bin_size);
	increaseLoadBarLength();	// 4 dots

	module_params_t* params = findModuleParams(NDS_HEAD, donorSdkVer);
	if(params)
	{
		ensureArm9Decompressed(NDS_HEAD, params);
	}
	increaseLoadBarLength();	// 5 dots

	errorCode = patchCardNds(NDS_HEAD, ENGINE_LOCATION_ARM7, ENGINE_LOCATION_ARM9, params, saveFileCluster, saveSize, patchMpuRegion, patchMpuSize);
	if(errorCode == ERR_NONE) {
		nocashMessage("patch card Sucessfull");
	} else {
		nocashMessage("game uses thumb");
		errorOutput();
	}
	increaseLoadBarLength();	// 6 dots

	errorCode = hookNdsRetail(NDS_HEAD, *romFile, (u32*)ENGINE_LOCATION_ARM7);
	if(errorCode == ERR_NONE) {
		nocashMessage("card hook Sucessfull");
	} else {
		nocashMessage("error during card hook");
		errorOutput();
	}
	increaseLoadBarLength();	// 7 dots
 


	setArm9Stuff(*romFile);

	increaseLoadBarLength();	// and finally, 8 dots
	fadeType = false;
	while (screenBrightness != 31);	// Wait for screen to fade-out

    // lock SCFG
    REG_SCFG_EXT &= ~(1UL << 31);

	nocashMessage("Start the NDS file");
	startBinary_ARM7();

	return 0;
}

