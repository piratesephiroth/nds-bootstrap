/*
    NitroHax -- Cheat tool for the Nintendo DS
    Copyright (C) 2008  Michael "Chishm" Chisholm

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <nds/ndstypes.h>
#include <nds/arm9/exceptions.h>
#include <nds/arm9/cache.h>
#include <nds/system.h>
#include <nds/interrupts.h>
#include <nds/fifomessages.h>
#include <nds/memory.h> // tNDSHeader
#include "hex.h"
#include "nds_header.h"
#include "module_params.h"
#include "cardengine.h"
#include "locations.h"

#define _32KB_READ_SIZE  0x8000
#define _64KB_READ_SIZE  0x10000
#define _128KB_READ_SIZE 0x20000
#define _192KB_READ_SIZE 0x30000
#define _256KB_READ_SIZE 0x40000
#define _512KB_READ_SIZE 0x80000
#define _768KB_READ_SIZE 0xC0000
#define _1MB_READ_SIZE   0x100000

extern void user_exception(void);

extern vu32* volatile cardStruct0;
//extern vu32* volatile cacheStruct;

extern module_params_t* moduleParams;
extern u32 ROMinRAM;
extern u32 dsiMode; // SDK 5
extern u32 enableExceptionHandler;
extern u32 consoleModel;

extern u32 needFlushDCCache;

extern volatile int (*readCachedRef)(u32*); // This pointer is not at the end of the table but at the handler pointer corresponding to the current irq
vu32* volatile sharedAddr = (vu32*)CARDENGINE_SHARED_ADDRESS;

static u32 cacheDescriptor[dev_EXTENDED_CACHE_SLOTS] = {0xFFFFFFFF};
static u32 cacheCounter[dev_EXTENDED_CACHE_SLOTS];
static u32 accessCounter = 0;

static tNDSHeader* ndsHeader = (tNDSHeader*)NDS_HEADER_4MB;
static u32 romLocation = EXTENDED_ROM_LOCATION;
static u32 cacheAddress = EXTENDED_CACHE_ADRESS_START;
static u16 cacheSlots = retail_EXTENDED_CACHE_SLOTS;

static u32 cacheReadSizeSubtract = 0;

/*static u32 readNum = 0;
static bool alreadySetMpu = false;*/

static bool flagsSet = false;
static bool hgssFix = false;

//---------------------------------------------------------------------------------
void setExceptionHandler2(void) {
//---------------------------------------------------------------------------------
	exceptionStack = (u32)0x23EFFFC;
	EXCEPTION_VECTOR = enterException;
	*exceptionC = user_exception;
}

static int allocateCacheSlot(void) {
	int slot = 0;
	u32 lowerCounter = accessCounter;
	for (int i = 0; i < cacheSlots; i++) {
		if (cacheCounter[i] <= lowerCounter) {
			lowerCounter = cacheCounter[i];
			slot = i;
			if (!lowerCounter) {
				break;
			}
		}
	}
	return slot;
}

static int getSlotForSector(u32 sector) {
	for (int i = 0; i < cacheSlots; i++) {
		if (cacheDescriptor[i] == sector) {
			return i;
		}
	}
	return -1;
}

static vu8* getCacheAddress(int slot) {
	//return (vu32*)(cacheAddress + slot*_128KB_READ_SIZE);
	return (vu8*)(cacheAddress + slot*_128KB_READ_SIZE);
}

static void updateDescriptor(int slot, u32 sector) {
	cacheDescriptor[slot] = sector;
	cacheCounter[slot] = accessCounter;
}

static void waitForArm7(void) {
	while (sharedAddr[3] != (vu32)0);
}

static void accessExtRam(bool yes) {
	if (yes) {
		REG_IME = 0;	// Disable all IRQs to prevent crashing when accessing extra RAM
		REG_SCFG_EXT = (dsiMode ? 0x8307F100 : 0x8300C000);
	} else {
		REG_IME = 1;	// Re-enable all IRQs when done accessing extra RAM
		REG_SCFG_EXT = (dsiMode ? 0x8307B100 : 0x83000000);
	}
}

static inline bool isHGSS(const tNDSHeader* ndsHeader) {
	const char* romTid = getRomTid(ndsHeader);
	return (strncmp(romTid, "IPK", 3) == 0  // Pokemon HeartGold
		|| strncmp(romTid, "IPG", 3) == 0); // Pokemon SoulSilver
}

int cardRead(u32* cacheStruct, u8* dst0, u32 src0, u32 len0) {
	//nocashMessage("\narm9 cardRead\n");

	bool sdk5 = isSdk5(moduleParams);

	vu32* volatile cardStruct = (sdk5 ? (vu32* volatile)(CARDENGINE_ARM9_WRAM_LOCATION + 0x3BC0) : cardStruct0);

	u8* cacheBuffer = (u8*)(cacheStruct + 8);
	u32* cachePage = cacheStruct + 2;
	u32 commandRead;
	u32 src = (sdk5 ? src0 : cardStruct[0]);
	if (sdk5) {
		cardStruct[0] = src;
	}

	if(src <= 0x8000){
		src = 0x8000+(src & 0x1FF);	// Fix reads below 0x8000
	}
	if (src == 0) {
		// If ROM read location is 0, do not proceed.
		return 0;
	}
	u8* dst = (sdk5 ? dst0 : (u8*)(cardStruct[1]));
	u32 len = (sdk5 ? len0 : cardStruct[2]);

	if (sdk5) {
		cardStruct[1] = (vu32)dst;
		cardStruct[2] = len;
	}

	u32 page = (src / 512) * 512;

	// SDK 5 --> White screen
	/*if (*(vu32*)0x2800010 != 1) {
		if (readNum >= 0x100){ // Don't set too early or some games will crash
			*(vu32*)(*(vu32*)(0x2800000)) = *(vu32*)0x2800004;
			*(vu32*)(*(vu32*)(0x2800008)) = *(vu32*)0x280000C;
			alreadySetMpu = true;
		} else {
			readNum += 1;
		}
	}*/

	if (!flagsSet) {
		if (isHGSS(ndsHeader)) {
			cacheSlots = HGSS_CACHE_SLOTS;	// Use smaller cache size to avoid timing issues
			hgssFix = true;
		} else if (consoleModel > 0) {
			cacheSlots = dev_EXTENDED_CACHE_SLOTS;
		}

		ndsHeader->romSize += 0x1000;

		if (enableExceptionHandler) {
			setExceptionHandler2();
		}
		flagsSet = true;
	}

	#ifdef DEBUG
	// send a log command for debug purpose
	// -------------------------------------
	commandRead = 0x026ff800;

	sharedAddr[0] = dst;
	sharedAddr[1] = len;
	sharedAddr[2] = src;
	sharedAddr[3] = commandRead;

	//IPC_SendSync(0xEE24);

	waitForArm7();
	// -------------------------------------*/
	#endif
	

	if (!ROMinRAM) {
		u32 sector = (src/_128KB_READ_SIZE)*_128KB_READ_SIZE;
		cacheReadSizeSubtract = 0;
		if ((ndsHeader->romSize > 0) && ((sector+_128KB_READ_SIZE) > ndsHeader->romSize)) {
			for (u32 i = 0; i < _128KB_READ_SIZE; i++) {
				cacheReadSizeSubtract++;
				if (((sector+_128KB_READ_SIZE)-cacheReadSizeSubtract) == ndsHeader->romSize) break;
			}
		}

		accessCounter++;

		if (page == src && len > _128KB_READ_SIZE && (u32)dst < 0x02700000 && (u32)dst > 0x02000000 && (u32)dst % 4 == 0) {
			// Read directly at ARM7 level
			commandRead = 0x025FFB08;

			cacheFlush();

			sharedAddr[0] = (vu32)dst;
			sharedAddr[1] = len;
			sharedAddr[2] = src;
			sharedAddr[3] = commandRead;

			//IPC_SendSync(0xEE24);

			waitForArm7();

		} else {
			// Read via the main RAM cache
			while(len > 0) {
				int slot = getSlotForSector(sector);
				vu8* buffer = getCacheAddress(slot);
				// Read max CACHE_READ_SIZE via the main RAM cache
				if (slot == -1) {
					// Send a command to the ARM7 to fill the RAM cache
					commandRead = 0x025FFB08;

					slot = allocateCacheSlot();

					buffer = getCacheAddress(slot);

					accessExtRam(true);

					if (needFlushDCCache) {
						DC_FlushRange((void*)buffer, _128KB_READ_SIZE);
					}

					// Write the command
					sharedAddr[0] = (vu32)buffer;
					sharedAddr[1] = _128KB_READ_SIZE - cacheReadSizeSubtract;
					sharedAddr[2] = sector;
					sharedAddr[3] = commandRead;

					//IPC_SendSync(0xEE24);

					waitForArm7();
		
					accessExtRam(false);
				}

				updateDescriptor(slot, sector);	

				u32 len2 = len;
				if ((src - sector) + len2 > _128KB_READ_SIZE) {
					len2 = sector - src + _128KB_READ_SIZE;
				}

				if (len2 > 512) {
					len2 -= src % 4;
					len2 -= len2 % 32;
				}

				if (sdk5 || readCachedRef == 0 || (len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0)) {
					#ifdef DEBUG
					// Send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = dst;
					sharedAddr[1] = len2;
					sharedAddr[2] = buffer+src-sector;
					sharedAddr[3] = commandRead;

					//IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------*/
					#endif

					// Copy directly
					accessExtRam(true);
					memcpy(dst, (void*)(buffer+(src-sector)), len2);
					accessExtRam(false);

					// Update cardi common
					cardStruct[0] = src + len2;
					cardStruct[1] = (vu32)(dst + len2);
					cardStruct[2] = len - len2;
				} else {
					#ifdef DEBUG
					// Send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = page;
					sharedAddr[1] = len2;
					sharedAddr[2] = buffer+page-sector;
					sharedAddr[3] = commandRead;

					//IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------
					#endif

					// Read via the 512b ram cache
					//copy8(buffer+(page-sector)+(src%512), dst, len2);
					//cardStruct[0] = src + len2;
					//cardStruct[1] = dst + len2;
					//cardStruct[2] = len - len2;
					//(*readCachedRef)(cacheStruct);
					accessExtRam(true);
					memcpy(cacheBuffer, (void*)(buffer+(page-sector)), 512);
					accessExtRam(false);
					*cachePage = page;
					(*readCachedRef)(cacheStruct);
				}
				len = cardStruct[2];
				if (len > 0) {
					src = cardStruct[0];
					dst = (u8*)cardStruct[1];
					page = (src / 512) * 512;
					sector = (src / _128KB_READ_SIZE) * _128KB_READ_SIZE;
					cacheReadSizeSubtract = 0;
					if (ndsHeader->romSize > 0 && (sector+_128KB_READ_SIZE) > ndsHeader->romSize) {
						for (u32 i = 0; i < _128KB_READ_SIZE; i++) {
							cacheReadSizeSubtract++;
							if ((sector+_128KB_READ_SIZE) - cacheReadSizeSubtract == ndsHeader->romSize) {
								break;
							}
						}
					}
					accessCounter++;
				}
			}
		}
	} else {
		while (len > 0) {
			u32 len2=len;
			if (len2 > 512) {
				len2 -= src % 4;
				len2 -= len2 % 32;
			}

			if (sdk5 || readCachedRef == 0 || (len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0)) {
				#ifdef DEBUG
				// Send a log command for debug purpose
				// -------------------------------------
				commandRead = 0x026ff800;

				sharedAddr[0] = dst;
				sharedAddr[1] = len;
				sharedAddr[2] = ((romLocation)-0x4000-ndsHeader->arm9binarySize)+src;
				sharedAddr[3] = commandRead;

				//IPC_SendSync(0xEE24);

				waitForArm7();
				// -------------------------------------
				#endif

				// Copy directly
				accessExtRam(true);
				memcpy(dst, (void*)((romLocation - 0x4000 - ndsHeader->arm9binarySize) + src),len);
				accessExtRam(false);

				// Update cardi common
				cardStruct[0] = src + len;
				cardStruct[1] = (vu32)(dst + len);
				cardStruct[2] = len - len;
			} else {
				#ifdef DEBUG
				// Send a log command for debug purpose
				// -------------------------------------
				commandRead = 0x026ff800;

				sharedAddr[0] = page;
				sharedAddr[1] = len2;
				sharedAddr[2] = (romLocation-0x4000-ndsHeader->arm9binarySize)+page;
				sharedAddr[3] = commandRead;

				//IPC_SendSync(0xEE24);

				waitForArm7();
				// -------------------------------------
				#endif

				// Read via the 512b ram cache
				accessExtRam(true);
				memcpy(cacheBuffer, (void*)((romLocation - 0x4000 - ndsHeader->arm9binarySize) + page), 512);
				accessExtRam(false);
				*cachePage = page;
				(*readCachedRef)(cacheStruct);
			}
			len = cardStruct[2];
			if (len > 0) {
				src = cardStruct[0];
				dst = (u8*)cardStruct[1];
				page = (src / 512) * 512;
			}
		}
	}
	return 0;
}
