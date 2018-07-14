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

#include <nds.h> 
#include <nds/fifomessages.h>
#include "cardEngine.h"

extern u32 ROMinRAM;
extern u32 ROM_TID;
extern u32 ROM_HEADERCRC;
extern u32 ARM9_LEN;
extern u32 romSize;
extern u32 consoleModel;
extern u32 asyncPrefetch;

#define _32KB_READ_SIZE 0x8000
#define _64KB_READ_SIZE 0x10000
#define _128KB_READ_SIZE 0x20000
#define _192KB_READ_SIZE 0x30000
#define _256KB_READ_SIZE 0x40000
#define _512KB_READ_SIZE 0x80000
#define _768KB_READ_SIZE 0xC0000
#define _1MB_READ_SIZE 0x100000

#define retail_CACHE_ADRESS_START 0x0C480000
#define retail_CACHE_ADRESS_SIZE 0x280000
#define retail_CACHE_SLOTS 0x14
#define dev_CACHE_ADRESS_START 0x0D000000
#define dev_CACHE_ADRESS_SIZE 0x1000000
#define dev_CACHE_SLOTS 0x80

vu32* volatile cardStruct = 0x0C807BC0;
//extern vu32* volatile cacheStruct;
extern u32 sdk_version;
extern u32 needFlushDCCache;
vu32* volatile sharedAddr = (vu32*)0x027FFB08;
extern volatile int (*readCachedRef)(u32*); // this pointer is not at the end of the table but at the handler pointer corresponding to the current irq

static u32 cacheDescriptor [dev_CACHE_SLOTS] = {0xffffffff};
static u32 cacheCounter [dev_CACHE_SLOTS];
static u32 accessCounter = 0;

static u32 cacheAddress = retail_CACHE_ADRESS_START;
static u16 cacheSlots = retail_CACHE_SLOTS;

static u32 cacheReadSizeSubtract = 0;
static u32 asyncReadSizeSubtract = 0;

static u32 asyncSector = 0xFFFFFFFF;
static u32 asyncQueue [10];
static int aQHead = 0;
static int aQTail = 0;
static int aQSize = 0;
static char hexbuffer [9];

static bool flagsSet = false;
extern u32 dsiMode;

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

void user_exception(void);

//---------------------------------------------------------------------------------
void setExceptionHandler2() {
//---------------------------------------------------------------------------------
	exceptionStack = (u32)0x23EFFFC ;
	EXCEPTION_VECTOR = enterException ;
	*exceptionC = user_exception;
}

int allocateCacheSlot() {
	int slot = 0;
	u32 lowerCounter = accessCounter;
	for(int i=0; i<cacheSlots; i++) {
		if(cacheCounter[i]<=lowerCounter) {
			lowerCounter = cacheCounter[i];
			slot = i;
			if(!lowerCounter) break;
		}
	}
	return slot;
}

int getSlotForSector(u32 sector) {
	for(int i=0; i<cacheSlots; i++) {
		if(cacheDescriptor[i]==sector) {
			return i;
		}
	}
	return -1;
}


vu8* getCacheAddress(int slot) {
	return (vu32*)(cacheAddress+slot*_128KB_READ_SIZE);
}

void updateDescriptor(int slot, u32 sector) {
	cacheDescriptor[slot] = sector;
	cacheCounter[slot] = accessCounter;
}

void waitForArm7() {
	while(sharedAddr[3] != (vu32)0);
}

void addToAsyncQueue(sector) {
	#ifdef DEBUG
	nocashMessage("\narm9 addToAsyncQueue\n");	
	nocashMessage("\narm9 sector\n");	
	nocashMessage(tohex(sector));
	#endif
	
	asyncQueue[aQHead] = sector;
	aQHead++;
	aQSize++;
	if(aQHead>9) {
		aQHead=0;
	}
	if(aQSize>10) {
		aQSize=10;
		aQTail++;
		if(aQTail>9) aQTail=0;
	}
}

void triggerAsyncPrefetch(sector) {	
	#ifdef DEBUG
	nocashMessage("\narm9 triggerAsyncPrefetch\n");	
	nocashMessage("\narm9 sector\n");	
	nocashMessage(tohex(sector));
	nocashMessage("\narm9 asyncSector\n");	
	nocashMessage(tohex(asyncSector));
	#endif
	
	asyncReadSizeSubtract = 0;
	if(asyncSector == 0xFFFFFFFF) {
		if (romSize > 0) {
			if (sector > romSize) {
				sector = 0;
			} else if ((sector+_128KB_READ_SIZE) > romSize) {
				for (u32 i = 0; i < _128KB_READ_SIZE; i++) {
					asyncReadSizeSubtract++;
					if (((sector+_128KB_READ_SIZE)-asyncReadSizeSubtract) == romSize) break;
				}
			}
		}
		int slot = getSlotForSector(sector);
		// read max CACHE_READ_SIZE via the main RAM cache
		// do it only if there is no async command ongoing
		if(slot==-1) {
			addToAsyncQueue(sector);
			// send a command to the arm7 to fill the RAM cache
			u32 commandRead = 0x020ff800;		

			slot = allocateCacheSlot();
			vu8* buffer = getCacheAddress(slot);

			if(needFlushDCCache) DC_FlushRange(buffer, _128KB_READ_SIZE);

			cacheDescriptor[slot] = sector;
			cacheCounter[slot] = 0x0FFFFFFF ; // async marker
			asyncSector = sector;		

			// write the command
			sharedAddr[0] = buffer;
			sharedAddr[1] = _128KB_READ_SIZE-asyncReadSizeSubtract;
			sharedAddr[2] = sector;
			sharedAddr[3] = commandRead;

			//IPC_SendSync(0xEE24);			


			// do it asynchronously
			//waitForArm7();
		}
	}
}

void processAsyncCommand() {
	#ifdef DEBUG
	nocashMessage("\narm9 processAsyncCommand\n");	
	nocashMessage("\narm9 asyncSector\n");	
	nocashMessage(tohex(asyncSector));
	#endif
	
	if(asyncSector != 0xFFFFFFFF) {
		int slot = getSlotForSector(asyncSector);
		if(slot!=-1 && cacheCounter[slot] == 0x0FFFFFFF) {
			if(sharedAddr[3] == (vu32)0) {
				updateDescriptor(slot, asyncSector);
				asyncSector = 0xFFFFFFFF;
			}			
		}	
	}
}

void getAsyncSector() {
	#ifdef DEBUG
	nocashMessage("\narm9 getAsyncSector\n");	
	nocashMessage("\narm9 asyncSector\n");	
	nocashMessage(tohex(asyncSector));
	#endif
	
	if(asyncSector != 0xFFFFFFFF) {
		int slot = getSlotForSector(asyncSector);
		if(slot!=-1 && cacheCounter[slot] == 0x0FFFFFFF) {
			waitForArm7();

			updateDescriptor(slot, asyncSector);
			asyncSector = 0xFFFFFFFF;
		}	
	}	
}

int cardRead (u32* cacheStruct, u8* dst0, u32 src0, u32 len0) {
	//nocashMessage("\narm9 cardRead\n");

	u32 commandRead;
	
	u8* dst = dst0;
	u32 src = src0;
	u32 len = len0;

	cardStruct[0] = src;
	if(src==0) {
		return 0;	// If ROM read location is 0, do not proceed.
	}
	cardStruct[1] = dst;
	cardStruct[2] = len;

	u32 page = (src/512)*512;

	if(!flagsSet) {
		setExceptionHandler2();
		
		if (consoleModel > 0) {
			cacheAddress = dev_CACHE_ADRESS_START;
			cacheSlots = dev_CACHE_SLOTS;
		}

		if(dsiMode) {
			REG_SCFG_EXT = 0x8307F100;
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
	

	if (ROMinRAM == false) {
		u32 sector = (src/_128KB_READ_SIZE)*_128KB_READ_SIZE;
		cacheReadSizeSubtract = 0;
		if ((romSize > 0) && ((sector+_128KB_READ_SIZE) > romSize)) {
			for (u32 i = 0; i < _128KB_READ_SIZE; i++) {
				cacheReadSizeSubtract++;
				if (((sector+_128KB_READ_SIZE)-cacheReadSizeSubtract) == romSize) break;
			}
		}

		accessCounter++;

		if (asyncPrefetch == 1 && consoleModel > 0) processAsyncCommand();

		if (page == src && len > _128KB_READ_SIZE && dst < 0x02700000 && dst > 0x02000000 && ((u32)dst)%4==0) {
			if (asyncPrefetch == 1 && consoleModel > 0) getAsyncSector();

			// read directly at arm7 level
			commandRead = 0x025FFB08;

			cacheFlush();

			sharedAddr[0] = dst;
			sharedAddr[1] = len;
			sharedAddr[2] = src;
			sharedAddr[3] = commandRead;

			//IPC_SendSync(0xEE24);

			waitForArm7();

		} else {
			// read via the main RAM cache
			while(len > 0) {
				int slot = getSlotForSector(sector);
				vu8* buffer = getCacheAddress(slot);
				u32 nextSector = sector+_128KB_READ_SIZE;	
				// read max CACHE_READ_SIZE via the main RAM cache
				if(slot==-1) {
					if (asyncPrefetch == 1 && consoleModel > 0) getAsyncSector();

					// send a command to the arm7 to fill the RAM cache
					commandRead = 0x025FFB08;

					slot = allocateCacheSlot();

					buffer = getCacheAddress(slot);

					if(needFlushDCCache) DC_FlushRange(buffer, _128KB_READ_SIZE);

					// write the command
					sharedAddr[0] = buffer;
					sharedAddr[1] = _128KB_READ_SIZE-cacheReadSizeSubtract;
					sharedAddr[2] = sector;
					sharedAddr[3] = commandRead;

					//IPC_SendSync(0xEE24);

					waitForArm7();

					updateDescriptor(slot, sector);	
		
					if (asyncPrefetch == 1 && consoleModel > 0) triggerAsyncPrefetch(nextSector);
				} else {
					if (asyncPrefetch == 1 && consoleModel > 0) {
						if(cacheCounter[slot] == 0x0FFFFFFF) {
							// prefetch successfull
							getAsyncSector();
							
							triggerAsyncPrefetch(nextSector);	
						} else {
							int i;
							for(i=0; i<10; i++) {
								if(asyncQueue[i]==sector) {
									// prefetch successfull
									triggerAsyncPrefetch(nextSector);	
									break;
								}
							}
						}
					}
					updateDescriptor(slot, sector);
				}

				u32 len2=len;
				if((src - sector) + len2 > _128KB_READ_SIZE){
					len2 = sector - src + _128KB_READ_SIZE;
				}

				if(len2 > 512) {
					len2 -= src%4;
					len2 -= len2 % 32;
				}

				//if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
					#ifdef DEBUG
					// send a log command for debug purpose
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

					// copy directly
					memcpy(dst,buffer+(src-sector),len2);

					// update cardi common
					cardStruct[0] = src + len2;
					cardStruct[1] = dst + len2;
					cardStruct[2] = len - len2;
				//}
				/*else {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = page;
					sharedAddr[1] = len2;
					sharedAddr[2] = buffer+page-sector;
					sharedAddr[3] = commandRead;

					//IPC_SendSync(0xEE24);

					waitForArm7();
					// -------------------------------------*
					#endif

					// read via the 512b ram cache
					copy8(buffer+(page-sector)+(src%512), dst, len2);
					cardStruct[0] = src + len2;
					cardStruct[1] = dst + len2;
					cardStruct[2] = len - len2;
					//(*readCachedRef)(cacheStruct);
				}*/
				len = cardStruct[2];
				if(len>0) {
					src = cardStruct[0];
					dst = cardStruct[1];
					page = (src/512)*512;
					sector = (src/_128KB_READ_SIZE)*_128KB_READ_SIZE;
					cacheReadSizeSubtract = 0;
					if ((romSize > 0) && ((sector+_128KB_READ_SIZE) > romSize)) {
						for (u32 i = 0; i < _128KB_READ_SIZE; i++) {
							cacheReadSizeSubtract++;
							if (((sector+_128KB_READ_SIZE)-cacheReadSizeSubtract) == romSize) break;
						}
					}
					accessCounter++;
				}
			}
		}
	} else {
		while(len > 0) {
			u32 len2=len;
			if(len2 > 512) {
				len2 -= src%4;
				len2 -= len2 % 32;
			}

			#ifdef DEBUG
			// send a log command for debug purpose
			// -------------------------------------
			commandRead = 0x026ff800;

			sharedAddr[0] = dst;
			sharedAddr[1] = len2;
			sharedAddr[2] = (dev_CACHE_ADRESS_START-0x4000-ARM9_LEN)+src;
			sharedAddr[3] = commandRead;

			//IPC_SendSync(0xEE24);

			waitForArm7();
			// -------------------------------------
			#endif

			// copy directly
			memcpy(dst,(dev_CACHE_ADRESS_START-0x4000-ARM9_LEN)+src,len2);

			// update cardi common
			cardStruct[0] = src + len2;
			cardStruct[1] = dst + len2;
			cardStruct[2] = len - len2;
			len = cardStruct[2];
			if(len>0) {
				src = cardStruct[0];
				dst = cardStruct[1];
			}
		}
	}
	return 0;
}




