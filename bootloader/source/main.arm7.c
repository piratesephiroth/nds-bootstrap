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
#include "card.h"
#include "card_patcher.h"
#include "cardengine_arm7_bin.h"
#include "cardengine_arm9_bin.h"
#include "hook.h"
#include "common.h"

void arm7clearRAM();

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Important things
#define NDS_HEAD 0x02FFFE00
#define TEMP_ARM9_START_ADDRESS (*(vu32*)0x02FFFFF4)

#define ENGINE_LOCATION_ARM7  	0x037C0000
#define ENGINE_LOCATION_ARM9  	0x0C800000
#define ROM_LOCATION			0x0D000000

const char* bootName = "BOOT.NDS";

extern unsigned long _start;
extern unsigned long storedFileCluster;
extern unsigned long initDisc;
extern unsigned long argStart;
extern unsigned long argSize;
extern unsigned long dsiSD;
extern unsigned long saveFileCluster;
extern unsigned long saveSize;
extern unsigned long language;
extern unsigned long dsiMode;
extern unsigned long patchMpuRegion;
extern unsigned long patchMpuSize;
extern unsigned long consoleModel;
extern unsigned long loadingScreen;
extern unsigned long romread_LED;
extern unsigned long gameSoftReset;
extern unsigned long asyncPrefetch;

static aFile * romFile = (aFile *)0x37D5000;
static aFile * savFile = ((aFile *)0x37D5000)+1;

bool dsiModeConfirmed = false;

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
		boot_readFirmware(settingsOffset + 0x000, (u8*)0x02FFFC80, 0x70);
	} else {
		boot_readFirmware(settingsOffset + 0x100, (u8*)0x02FFFC80, 0x70);
	}
	if (language >= 0 && language < 6) {
		*(u8*)(0x02FFFCE4) = language;	// Change language
	}
}

// The following 3 functions are not in devkitARM r47
//---------------------------------------------------------------------------------
u32 readTSCReg(u32 reg) {
//---------------------------------------------------------------------------------
 
	REG_SPICNT = SPI_ENABLE | SPI_BAUD_4MHz | SPI_DEVICE_TOUCH | SPI_CONTINUOUS;
	REG_SPIDATA = ((reg<<1) | 1) & 0xFF;
 
	while(REG_SPICNT & 0x80);
 
	REG_SPIDATA = 0;
 
	while(REG_SPICNT & 0x80);

	REG_SPICNT = 0;

	return REG_SPIDATA;
}

//---------------------------------------------------------------------------------
void readTSCRegArray(u32 reg, void *buffer, int size) {
//---------------------------------------------------------------------------------
 
	REG_SPICNT = SPI_ENABLE | SPI_BAUD_4MHz | SPI_DEVICE_TOUCH | SPI_CONTINUOUS;
	REG_SPIDATA = ((reg<<1) | 1) & 0xFF;

	char *buf = (char*)buffer;
	while(REG_SPICNT & 0x80);
	int count = 0;
	while(count<size) {
		REG_SPIDATA = 0;
 
		while(REG_SPICNT & 0x80);


		buf[count++] = REG_SPIDATA;
		
	}
	REG_SPICNT = 0;

}


//---------------------------------------------------------------------------------
u32 writeTSCReg(u32 reg, u32 value) {
//---------------------------------------------------------------------------------
 
	REG_SPICNT = SPI_ENABLE | SPI_BAUD_4MHz | SPI_DEVICE_TOUCH | SPI_CONTINUOUS;
	REG_SPIDATA = ((reg<<1)) & 0xFF;
 
	while(REG_SPICNT & 0x80);
 
	REG_SPIDATA = value;
 
	while(REG_SPICNT & 0x80);

	REG_SPICNT = 0;

	return REG_SPIDATA;
}


//---------------------------------------------------------------------------------
void NDSTouchscreenMode() {
//---------------------------------------------------------------------------------
	//unsigned char * *(unsigned char*)0x40001C0=		(unsigned char*)0x40001C0;
	//unsigned char * *(unsigned char*)0x40001C0byte2=(unsigned char*)0x40001C1;
	//unsigned char * *(unsigned char*)0x40001C2=	(unsigned char*)0x40001C2;
	//unsigned char * I2C_DATA=	(unsigned char*)0x4004500;
	//unsigned char * I2C_CNT=	(unsigned char*)0x4004501;


	u8 volLevel;
	
	//if(fifoCheckValue32(FIFO_MAXMOD)) {
	//	// special setting (when found special gamecode)
	//	volLevel = 0xAC;
	//} else {
		// normal setting (for any other gamecodes)
		volLevel = 0xA7;
	//}

	volLevel += 0x13;

	// Touchscr
	readTSCReg(0);
	writeTSCReg(0,0);
	writeTSCReg(0x3a,0);
	readTSCReg(0x51);
	writeTSCReg(3,0);
	readTSCReg(2);
	writeTSCReg(0,0);
	readTSCReg(0x3f);
	writeTSCReg(0,1);
	readTSCReg(0x38);
	readTSCReg(0x2a);
	readTSCReg(0x2E);
	writeTSCReg(0,0);
	writeTSCReg(0x52,0x80);
	writeTSCReg(0x40,0xC);
	writeTSCReg(0,1);
	writeTSCReg(0x24,0xff);
	writeTSCReg(0x25,0xff);
	writeTSCReg(0x26,0x7f);
	writeTSCReg(0x27,0x7f);
	writeTSCReg(0x28,0x4a);
	writeTSCReg(0x29,0x4a);
	writeTSCReg(0x2a,0x10);
	writeTSCReg(0x2b,0x10);
	writeTSCReg(0,0);
	writeTSCReg(0x51,0);
	writeTSCReg(0,3);
	readTSCReg(2);
	writeTSCReg(2,0x98);
	writeTSCReg(0,1);
	writeTSCReg(0x23,0);
	writeTSCReg(0x1f,0x14);
	writeTSCReg(0x20,0x14);
	writeTSCReg(0,0);
	writeTSCReg(0x3f,0);
	readTSCReg(0x0b);
	writeTSCReg(0x5,0);
	writeTSCReg(0xb,0x1);
	writeTSCReg(0xc,0x2);
	writeTSCReg(0x12,0x1);
	writeTSCReg(0x13,0x2);
	writeTSCReg(0,1);
  writeTSCReg(0x2E,0x00);
  writeTSCReg(0,0);
  writeTSCReg(0x3A,0x60);
  writeTSCReg(0x01,01);
  writeTSCReg(0x9,0x66);
  writeTSCReg(0,1);
  readTSCReg(0x20);
  writeTSCReg(0x20,0x10);
  writeTSCReg(0,0);
  writeTSCReg( 04,00);
  writeTSCReg( 0x12,0x81);
  writeTSCReg( 0x13,0x82);
  writeTSCReg( 0x51,0x82);
  writeTSCReg( 0x51,0x00);
  writeTSCReg( 0x04,0x03);
  writeTSCReg( 0x05,0xA1);
  writeTSCReg( 0x06,0x15);
  writeTSCReg( 0x0B,0x87);
  writeTSCReg( 0x0C,0x83);
  writeTSCReg( 0x12,0x87);
  writeTSCReg( 0x13,0x83);
  writeTSCReg(0,3);
  readTSCReg(0x10);
  writeTSCReg(0x10,0x08);
  writeTSCReg(0,4);
  writeTSCReg(0x08,0x7F);
  writeTSCReg(0x09,0xE1);
  writeTSCReg(0xa,0x80);
  writeTSCReg(0xb,0x1F);
  writeTSCReg(0xc,0x7F);
  writeTSCReg(0xd,0xC1);
  writeTSCReg(0,0);
  writeTSCReg( 0x41, 0x08);
  writeTSCReg( 0x42, 0x08);
  writeTSCReg( 0x3A, 0x00);
  writeTSCReg(0,4);
  writeTSCReg(0x08,0x7F);
  writeTSCReg(0x09,0xE1);
  writeTSCReg(0xa,0x80);
  writeTSCReg(0xb,0x1F);
  writeTSCReg(0xc,0x7F);
  writeTSCReg(0xd,0xC1);
  writeTSCReg(0,1);
  writeTSCReg(0x2F, 0x2B);
  writeTSCReg(0x30, 0x40);
  writeTSCReg(0x31, 0x40);
  writeTSCReg(0x32, 0x60);
  writeTSCReg(0,0);
  readTSCReg( 0x74);
  writeTSCReg( 0x74, 0x02);
  readTSCReg( 0x74);
  writeTSCReg( 0x74, 0x10);
  readTSCReg( 0x74);
  writeTSCReg( 0x74, 0x40);
  writeTSCReg(0,1);
  writeTSCReg( 0x21, 0x20);
  writeTSCReg( 0x22, 0xF0);
  writeTSCReg(0,0);
  readTSCReg( 0x51);
  readTSCReg( 0x3f);
  writeTSCReg( 0x3f, 0xd4);
  writeTSCReg(0,1);
  writeTSCReg(0x23,0x44);
  writeTSCReg(0x1F,0xD4);
  writeTSCReg(0x28,0x4e);
  writeTSCReg(0x29,0x4e);
  writeTSCReg(0x24,0x9e);
  writeTSCReg(0x24,0x9e);
  writeTSCReg(0x20,0xD4);
  writeTSCReg(0x2a,0x14);
  writeTSCReg(0x2b,0x14);
  writeTSCReg(0x26,volLevel);
  writeTSCReg(0x27,volLevel);
  writeTSCReg(0,0);
  writeTSCReg(0x40,0);
  writeTSCReg(0x3a,0x60);
  writeTSCReg(0,1);
  writeTSCReg(0x26,volLevel);
  writeTSCReg(0x27,volLevel);
  writeTSCReg(0x2e,0x03);
  writeTSCReg(0,3);
  writeTSCReg(3,0);
  writeTSCReg(0,1);
  writeTSCReg(0x21,0x20);
  writeTSCReg(0x22,0xF0);
  readTSCReg(0x22);
  writeTSCReg(0x22,0xF0);
  writeTSCReg(0,0);
  writeTSCReg(0x52,0x80);
  writeTSCReg(0x51,0x00);
  writeTSCReg(0,3);
  readTSCReg(0x02);
  writeTSCReg(2,0x98);
  writeTSCReg(0,0xff);
  writeTSCReg(5,0);
	
	
	
	
	
	
	
	// Powerman
	writePowerManagement(0x00,0x0D);
	//*(unsigned char*)0x40001C2 = 0x80, 0x00;		// read PWR[0]   ;<-- also part of TSC !
	//*(unsigned char*)0x40001C2 = 0x00, 0x0D;		// PWR[0]=0Dh    ;<-- also part of TSC !
	

}


u32 ROMinRAM = false;
u32 ROM_TID;
u32 ROM_HEADERCRC;
u32 ARM9_LEN;
u32 ARM7_LEN;
u32 fatSize;
u32 romSize;
u32 romSizeNoArm9;

void loadBinary_ARM7 (aFile file)
{
	u32 ndsHeader[0x2F0>>2];

	nocashMessage("loadBinary_ARM7");

	// read NDS header
	fileRead ((char*)ndsHeader, file, 0, 0x2F0, 2);
	// read ARM9 info from NDS header
	u32 ARM9_SRC = ndsHeader[0x020>>2];
	char* ARM9_DST = (char*)ndsHeader[0x028>>2];
	ARM9_LEN = ndsHeader[0x02C>>2];
	// read ARM7 info from NDS header
	u32 ARM7_SRC = ndsHeader[0x030>>2];
	char* ARM7_DST = (char*)ndsHeader[0x038>>2];
	ARM7_LEN = ndsHeader[0x03C>>2];

	ROM_TID = ndsHeader[0x00C>>2];
	fatSize = ndsHeader[0x04C>>2];
	romSize = ndsHeader[0x080>>2];
	romSizeNoArm9 = romSize-0x4000-ARM9_LEN;
	ROM_HEADERCRC = ndsHeader[0x15C>>2];

	if ((consoleModel > 0) && (romSizeNoArm9 <= 0x01000000)) {
		// Set to load ROM into RAM
		ROMinRAM = true;
	}
	
	// Load binaries into memory
	fileRead(ARM9_DST, file, ARM9_SRC, ARM9_LEN, 3);
	fileRead(ARM7_DST, file, ARM7_SRC, ARM7_LEN, 3);

	// first copy the header to its proper location, excluding
	// the ARM9 start address, so as not to start it
	TEMP_ARM9_START_ADDRESS = ndsHeader[0x024>>2];		// Store for later
	ndsHeader[0x024>>2] = 0;
	dmaCopyWords(3, (void*)ndsHeader, (void*)NDS_HEAD, 0x170);

	if (dsiMode && (ndsHeader[0x10>>2]&BIT(16+1)))
	{
		dsiModeConfirmed = true;

		u32 ARM9i_SRC = ndsHeader[0x1C0>>2];
		char* ARM9i_DST = (char*)ndsHeader[0x1C8>>2];
		u32 ARM9i_LEN = ndsHeader[0x1CC>>2];
		u32 ARM7i_SRC = ndsHeader[0x1D0>>2];
		char* ARM7i_DST = (char*)ndsHeader[0x1D8>>2];
		u32 ARM7i_LEN = ndsHeader[0x1DC>>2];

		if (ARM9i_LEN)
			fileRead(ARM9i_DST, file, ARM9i_SRC, ARM9i_LEN, 3);
		if (ARM7i_LEN)
			fileRead(ARM7i_DST, file, ARM7i_SRC, ARM7i_LEN, 3);
	} else {
		// Switch to NTR Mode
		REG_SCFG_ROM = 0x703;
	}
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
	*((vu32*)0x02FFFE24) = TEMP_ARM9_START_ADDRESS;
	// Get the ARM9 to boot
	arm9_stateFlag = ARM9_BOOTBIN;

	while(REG_VCOUNT!=191);
	while(REG_VCOUNT==191);
	// Start ARM7
	VoidFn arm7code = *(VoidFn*)(0x2FFFE34);
	arm7code();
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Main function

void initMBK() {
	// give all DSI WRAM to arm7 at boot
	// this function have no effect on DSI with ARM7 SCFG locked
	
	// arm7 is master of WRAM-A, arm9 of WRAM-B & C
	REG_MBK9=0x3000000F;
	
	// WRAM-A fully mapped to arm7
	*((vu32*)REG_MBK1)=0x8185898D; // same as dsiware
	
	// WRAM-B fully mapped to arm7 // inverted order
	*((vu32*)REG_MBK2)=0x9195999D;
	*((vu32*)REG_MBK3)=0x8185898D;
	
	// WRAM-C fully mapped to arm7 // inverted order
	*((vu32*)REG_MBK4)=0x9195999D;
	*((vu32*)REG_MBK5)=0x8185898D;
	
	// WRAM mapped to the 0x3700000 - 0x37FFFFF area 
	// WRAM-A mapped to the 0x37C0000 - 0x37FFFFF area : 256k
	REG_MBK6=0x080037C0; // same as dsiware
	// WRAM-B mapped to the 0x3740000 - 0x37BFFFF area : 512k // why? only 256k real memory is there
	REG_MBK7=0x07C03740; // same as dsiware
	// WRAM-C mapped to the 0x3700000 - 0x373FFFF area : 256k
	REG_MBK8=0x07403700; // same as dsiware
}

void arm7_main (void) {
	nocashMessage("bootloader");

	initMBK();
    
    // Wait for ARM9 to at least start
	while (arm9_stateFlag < ARM9_START);

	// Get ARM7 to clear RAM
	nocashMessage("Get ARM7 to clear RAM");
	debugOutput();	// 1 dot
	resetMemory_ARM7();

	// Init card
	if(!FAT_InitFiles(initDisc, 3))
	{
		nocashMessage("!FAT_InitFiles");
		return -1;
	}

	*romFile = getFileFromCluster(storedFileCluster);

	if ((romFile->firstCluster < CLUSTER_FIRST) || (romFile->firstCluster >= CLUSTER_EOF)) 	/* Invalid file cluster specified */
	{
		*romFile = getBootFileCluster(bootName, 3);
	}
	if (romFile->firstCluster == CLUSTER_FREE)
	{
		nocashMessage("fileCluster == CLUSTER_FREE");
		return -1;
	}

    buildFatTableCache(romFile, 3);
    
    *savFile = getFileFromCluster(saveFileCluster);
    
    if (romFile->firstCluster != CLUSTER_FREE)
	{
         buildFatTableCache(savFile, 3);
	}

	int errorCode;

	if (REG_SCFG_EXT == 0) {
		NDSTouchscreenMode();
		*(u16*)(0x4000500) = 0x807F;
	}

	// Load the NDS file
	nocashMessage("Load the NDS file");
	loadBinary_ARM7(*romFile);
	increaseLoadBarLength();	// 2 dots

	nocashMessage("try to patch card");
	copyLoop (ENGINE_LOCATION_ARM7, (u32*)cardengine_arm7_bin, cardengine_arm7_bin_size);
	increaseLoadBarLength();	// 3 dots
	copyLoop (ENGINE_LOCATION_ARM9, (u32*)cardengine_arm9_bin, cardengine_arm9_bin_size);
	increaseLoadBarLength();	// 4 dots

	module_params_t* params = findModuleParams(NDS_HEAD);
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
 


	if (ROMinRAM == true) {
		// Load ROM into RAM
		fileRead (ROM_LOCATION, *romFile, 0x4000+ARM9_LEN, romSizeNoArm9, 0);
	}

	hookNdsRetail9((u32*)ENGINE_LOCATION_ARM9);

	if(ROMinRAM == false) {
		if (romread_LED == 1 || (romread_LED > 0 && asyncPrefetch == 1)) {
			i2cWriteRegister(0x4A, 0x30, 0x12);    // Turn WiFi LED off
		}
	}

	increaseLoadBarLength();	// and finally, 8 dots
	fadeType = false;
	while (screenBrightness != 31);	// Wait for screen to fade-out
    
    // lock SCFG
    REG_SCFG_EXT &= ~(1UL << 31);

	nocashMessage("Start the NDS file");
	startBinary_ARM7();

	return 0;
}

