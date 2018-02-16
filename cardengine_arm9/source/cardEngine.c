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

//static u32 ROM_LOCATION = 0x0C4A0000;
static u32 ROM_LOCATION = 0x0D000000;
extern u32 ROM_TID;
extern u32 ROM_HEADERCRC;
extern u32 ARM9_LEN;
extern u32 romSize;
static u32 SCFG_EXT_CACHEACCESS = 0x8300C000;
static u32 SCFG_EXT_NORM = 0x83008000;

#define _32KB_READ_SIZE 0x8000
#define _64KB_READ_SIZE 0x10000
#define _128KB_READ_SIZE 0x20000
#define _192KB_READ_SIZE 0x30000
#define _256KB_READ_SIZE 0x40000
#define _512KB_READ_SIZE 0x80000
#define _768KB_READ_SIZE 0xC0000
#define _1MB_READ_SIZE 0x100000

#define REG_MBK_WRAM_CACHE_START	0x4004044
#define WRAM_CACHE_ADRESS_START 0x03708000
#define WRAM_CACHE_ADRESS_END 0x03778000
#define WRAM_CACHE_ADRESS_SIZE 0x78000
#define WRAM_CACHE_SLOTS 15

//#define MobiClip_CACHE_ADRESS_START 0x0C880000
//#define MobiClip_CACHE_ADRESS_SIZE 0x580000

#define dsMode_CACHE_ADRESS_START 0x0C4A0000
#define dsMode_CACHE_ADRESS_SIZE 0x1B40000
#define dsMode_128KB_CACHE_SLOTS 0xDA
#define dsMode_128KB_CACHE_SLOTS_part1 0x5A
#define dsiMode_CACHE_ADRESS_START 0x0D000000
#define dsiMode_CACHE_ADRESS_SIZE 0x1000000
#define dsiMode_128KB_CACHE_SLOTS 0x80
//#define dsiMode_192KB_CACHE_SLOTS 0x55
//#define dsiMode_256KB_CACHE_SLOTS 0x40
//#define dsiMode_512KB_CACHE_SLOTS 0x20
//#define dsiMode_768KB_CACHE_SLOTS 0x15
//#define dsiMode_1MB_CACHE_SLOTS 0x10

vu32* volatile cardStruct = 0x0C497BC0;
vu32* volatile fat = 0x0C488000;
//extern vu32* volatile cacheStruct;
extern u32 sdk_version;
extern u32 needFlushDCCache;
vu32* volatile sharedAddr = (vu32*)0x027FFB08;
extern volatile int (*readCachedRef)(u32*); // this pointer is not at the end of the table but at the handler pointer corresponding to the current irq

//static u32 MobiClip_cacheDescriptor [1];
//static u32 MobiClip_cacheCounter [1];
static u32 cacheDescriptor [dsMode_128KB_CACHE_SLOTS];
static u32 cacheCounter [dsMode_128KB_CACHE_SLOTS];
//static u32 MobiClip_accessCounter = 0;
static u32 accessCounter = 0;

static u32 CACHE_READ_SIZE = _128KB_READ_SIZE;

static bool flagsSet = false;
extern u32 ROMinRAM;
static int use16MB = 0;
extern u32 dsiMode;
//static bool mobiClip = false;

extern u32 setDataMobicliplist[3];
extern u32 setDataBWlist[7];
extern u32 setDataBWlist_1[3];
extern u32 setDataBWlist_2[3];
extern u32 setDataBWlist_3[3];
extern u32 setDataBWlist_4[3];
int dataAmount = 0;

void user_exception(void);

//---------------------------------------------------------------------------------
void setExceptionHandler2() {
//---------------------------------------------------------------------------------
	exceptionStack = (u32)0x23EFFFC ;
	EXCEPTION_VECTOR = enterException ;
	*exceptionC = user_exception;
}

/*int MobiClip_allocateCacheSlot() {
	int slot = 0;
	int lowerCounter = MobiClip_accessCounter;
	for(int i=0; i<1; i++) {
		if(MobiClip_cacheCounter[i]<=lowerCounter) {
			lowerCounter = MobiClip_cacheCounter[i];
			slot = i;
			if(!lowerCounter) break;
		}
	}
	return slot;
}*/

int allocateCacheSlot() {
	int slot = 0;
	int lowerCounter = accessCounter;
	//if(dsiMode) {
		for(int i=0; i<dsiMode_128KB_CACHE_SLOTS; i++) {
			if(cacheCounter[i]<=lowerCounter) {
				lowerCounter = cacheCounter[i];
				slot = i;
				if(!lowerCounter) return slot;
			}
		}
	/*} else {
		for(int i=0; i<dsMode_128KB_CACHE_SLOTS; i++) {
			if(cacheCounter[i]<=lowerCounter) {
				lowerCounter = cacheCounter[i];
				slot = i;
				if(!lowerCounter) return slot;
			}
		}
	}*/
	return slot;
}

int GAME_allocateCacheSlot() {
	int slot = 0;
	int lowerCounter = accessCounter;
	for(int i=0; i<setDataBWlist[5]; i++) {
		if(cacheCounter[i]<=lowerCounter) {
			lowerCounter = cacheCounter[i];
			slot = i;
			if(!lowerCounter) break;
		}
	}
	return slot;
}

/*int MobiClip_getSlotForSector(u32 sector) {
	for(int i=0; i<1; i++) {
		if(MobiClip_cacheDescriptor[i]==sector) {
			return i;
		}
	}
	return -1;
}*/

int getSlotForSector(u32 sector) {
	//if(dsiMode) {
		for(int i=0; i<dsiMode_128KB_CACHE_SLOTS; i++) {
			if(cacheDescriptor[i]==sector) {
				return i;
			}
		}
	/*} else {
		for(int i=0; i<dsMode_128KB_CACHE_SLOTS; i++) {
			if(cacheDescriptor[i]==sector) {
				return i;
			}
		}
	}*/
	return -1;
}

int GAME_getSlotForSector(u32 sector) {
	for(int i=0; i<setDataBWlist[5]; i++) {
		if(cacheDescriptor[i]==sector) {
			return i;
		}
	}
	return -1;
}


/*vu8* MobiClip_getCacheAddress() {
	return (vu32*)(MobiClip_CACHE_ADRESS_START);
}*/

vu8* getCacheAddress(int slot) {
	//if(dsiMode) {
		return (vu32*)(dsiMode_CACHE_ADRESS_START+slot*CACHE_READ_SIZE);
	/*} else {
		if(slot >= dsMode_128KB_CACHE_SLOTS_part1) {
			slot -= dsMode_128KB_CACHE_SLOTS_part1;
			return (vu32*)(dsiMode_CACHE_ADRESS_START+slot*CACHE_READ_SIZE);
		} else {
			return (vu32*)(dsMode_CACHE_ADRESS_START+slot*CACHE_READ_SIZE);
		}
	}*/
}

vu8* GAME_getCacheAddress(int slot) {
	return (vu32*)(setDataBWlist[4]+slot*setDataBWlist[6]);
}

/*void MobiClip_updateDescriptor(int slot, u32 sector) {
	MobiClip_cacheDescriptor[slot] = sector;
	MobiClip_cacheCounter[slot] = MobiClip_accessCounter;
}*/

void updateDescriptor(int slot, u32 sector) {
	cacheDescriptor[slot] = sector;
	cacheCounter[slot] = accessCounter;
}

void accessCounterIncrease() {
	accessCounter++;
}

void loadMobiclipVideo(u32 src, int slot, u32 buffer) {
	bool isMobiclip = false;
	
	// Search for "MODSN3.."
	for(u32 i = 0; i < CACHE_READ_SIZE; i++) {
		if(*(u32*)buffer+i == 0x4D4F4453 && *(u32*)buffer+i+4 == 0x4E330A00) {
			isMobiclip = true;	// It's a MobiClip video, so cache the full file
			break;
		}
	}
	
	if(isMobiclip) {
		u32 endOfFile;

		// Search for address of end of file
		int i2 = 0;
		for(u32 i = 0; i < 0x8000; i += 4) {
			if(fat[i2] >= src) {
				endOfFile = fat[i2+1];
				break;
			}
			i2 += 2;
		}

		u32 sector = (src/CACHE_READ_SIZE)*CACHE_READ_SIZE;

		u32 fileSize;

		// Generate file size
		for(u32 i = src; i < endOfFile; i += CACHE_READ_SIZE) {
			fileSize = i-src;
		}

		// Cache full MobiClip video into RAM
		if(buffer+fileSize < 0x0E000000) {
			i2 = 0;
			for(u32 i = src; i < endOfFile; i += CACHE_READ_SIZE) {
				i2++;
				accessCounterIncrease();
				updateDescriptor(slot+i2, sector+CACHE_READ_SIZE*i2);
			}

			u32 commandRead;

			commandRead = 0x025FFB08;

			sharedAddr[0] = buffer+CACHE_READ_SIZE;
			sharedAddr[1] = fileSize;
			sharedAddr[2] = src;
			sharedAddr[3] = commandRead;

			while(sharedAddr[3] != (vu32)0);
		}
	}
}

int cardRead (u32* cacheStruct, u8* dst0, u32 src0, u32 len0) {
	//nocashMessage("\narm9 cardRead\n");

	setExceptionHandler2();

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
		// If ROM size is 0x01000000 or below, then the ROM is in RAM.
		if(dsiMode) {
			SCFG_EXT_CACHEACCESS = 0x8307F100;
			SCFG_EXT_NORM = 0x8307B100;
			if((romSize > 0) && (romSize <= dsiMode_CACHE_ADRESS_SIZE) || setDataBWlist[3]==false) {
				ROM_LOCATION = 0x0D000000;
				ROM_LOCATION -= 0x4000;
				ROM_LOCATION -= ARM9_LEN;
			}
		} else if((romSize > 0) && (romSize <= dsiMode_CACHE_ADRESS_SIZE) || setDataBWlist[3]==false) {
			ROM_LOCATION -= 0x4000;
			ROM_LOCATION -= ARM9_LEN;
		}
		/*if(setDataMobicliplist[0] != 0 && setDataMobicliplist[1] != 0 && setDataMobicliplist[2] != 0) {
			mobiClip = true;
		}*/
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

	IPC_SendSync(0xEE24);

	while(sharedAddr[3] != (vu32)0);
	// -------------------------------------*/
	#endif
	
	
	CACHE_READ_SIZE = _128KB_READ_SIZE;

	if(ROMinRAM==0) {
		accessCounterIncrease();
	}

	u32 sector = (src/CACHE_READ_SIZE)*CACHE_READ_SIZE;

	if(ROMinRAM==0 && page == src && len > CACHE_READ_SIZE && dst < 0x02700000 && dst > 0x02000000 && ((u32)dst)%4==0) {
		// read directly at arm7 level
		commandRead = 0x025FFB08;

		cacheFlush();

		sharedAddr[0] = dst;
		sharedAddr[1] = len;
		sharedAddr[2] = src;
		sharedAddr[3] = commandRead;

		IPC_SendSync(0xEE24);

		while(sharedAddr[3] != (vu32)0);

	} else {
		// read via the main RAM/DSi WRAM cache
		while(len > 0) {
			if(ROMinRAM==0) {
				int slot = 0;
				vu8* buffer = 0;
				/*if(mobiClip && src >= setDataMobicliplist[0] && src < setDataMobicliplist[1]){
					CACHE_READ_SIZE = setDataMobicliplist[2];
					sector = (src/CACHE_READ_SIZE)*CACHE_READ_SIZE;

					slot = MobiClip_getSlotForSector(sector);
					buffer = MobiClip_getCacheAddress();
					// read max CACHE_READ_SIZE via the main RAM cache
					if(slot==-1) {
						// send a command to the arm7 to fill the RAM cache
						commandRead = 0x025FFB08;

						slot = MobiClip_allocateCacheSlot();

						buffer = MobiClip_getCacheAddress();

						// write the command
						sharedAddr[0] = buffer;
						sharedAddr[1] = CACHE_READ_SIZE;
						sharedAddr[2] = sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						while(sharedAddr[3] != (vu32)0);
					}

					MobiClip_updateDescriptor(slot, sector);
				} else {*/
					slot = getSlotForSector(sector);
					buffer = getCacheAddress(slot);
					// read max CACHE_READ_SIZE via the main RAM cache
					if(slot==-1) {
						// send a command to the arm7 to fill the RAM cache
						commandRead = 0x025FFB08;

						slot = allocateCacheSlot();

						buffer = getCacheAddress(slot);

						REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;

						if(needFlushDCCache) DC_FlushRange(buffer, CACHE_READ_SIZE);

						// write the command
						sharedAddr[0] = buffer;
						sharedAddr[1] = CACHE_READ_SIZE;
						sharedAddr[2] = sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						while(sharedAddr[3] != (vu32)0);

						REG_SCFG_EXT = SCFG_EXT_NORM;
					}

					updateDescriptor(slot, sector);

					loadMobiclipVideo(src0, slot, buffer);
				//}

				u32 len2=len;
				if((src - sector) + len2 > CACHE_READ_SIZE){
					len2 = sector - src + CACHE_READ_SIZE;
				}

				if(len2 > 512) {
					len2 -= src%4;
					len2 -= len2 % 32;
				}

				if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = dst;
					sharedAddr[1] = len2;
					sharedAddr[2] = buffer+src-sector;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					while(sharedAddr[3] != (vu32)0);
					// -------------------------------------*/
					#endif

					// copy directly
					REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
					copy8(buffer+(src-sector),dst,len2);
					REG_SCFG_EXT = SCFG_EXT_NORM;

					// update cardi common
					cardStruct[0] = src + len2;
					cardStruct[1] = dst + len2;
					cardStruct[2] = len - len2;
				} else {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = page;
					sharedAddr[1] = len2;
					sharedAddr[2] = buffer+page-sector;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					while(sharedAddr[3] != (vu32)0);
					// -------------------------------------*/
					#endif

					// read via the 512b ram cache
					REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
					copy8(buffer+(page-sector)+(src%512), dst, len2);
					REG_SCFG_EXT = SCFG_EXT_NORM;
					cardStruct[0] = src + len2;
                                        cardStruct[1] = dst + len2;
                                        cardStruct[2] = len - len2;
					//(*readCachedRef)(cacheStruct);
				}
				len = cardStruct[2];
				if(len>0) {
					src = cardStruct[0];
					dst = cardStruct[1];
					page = (src/512)*512;
					sector = (src/CACHE_READ_SIZE)*CACHE_READ_SIZE;
					accessCounterIncrease();
				}
			} else if (ROMinRAM==1) {
				// Prevent overwriting ROM in RAM
				if(dst > 0x0D000000 && dst < 0x0E000000) {
					if(use16MB==2) {
						return 0;	// Reject data from being loaded into debug 4MB area
					} else if(use16MB==1) {
						dst -= 0x01000000;
					}
				}

				u32 len2=len;
				if(len2 > 512) {
					len2 -= src%4;
					len2 -= len2 % 32;
				}

				if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = dst;
					sharedAddr[1] = len2;
					sharedAddr[2] = ROM_LOCATION+src;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					while(sharedAddr[3] != (vu32)0);
					// -------------------------------------*/
					#endif

					// read ROM loaded into RAM
					REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
					copy8(ROM_LOCATION+src,dst,len2);
					REG_SCFG_EXT = SCFG_EXT_NORM;

					// update cardi common
					cardStruct[0] = src + len2;
					cardStruct[1] = dst + len2;
					cardStruct[2] = len - len2;
				} else {
					#ifdef DEBUG
					// send a log command for debug purpose
					// -------------------------------------
					commandRead = 0x026ff800;

					sharedAddr[0] = page;
					sharedAddr[1] = len2;
					sharedAddr[2] = ROM_LOCATION+page;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					while(sharedAddr[3] != (vu32)0);
					// -------------------------------------
					#endif

					// read via the 512b ram cache
					REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
                                        copy8(ROM_LOCATION+page+(src%512), dst, len2);
                                        REG_SCFG_EXT = SCFG_EXT_NORM;
                                        cardStruct[0] = src + len2;
                                        cardStruct[1] = dst + len2;
                                        cardStruct[2] = len - len2;
				}
				len = cardStruct[2];
				if(len>0) {
					src = cardStruct[0];
					dst = cardStruct[1];
					page = (src/512)*512;
				}
			} else if (ROMinRAM==2) {
				// Prevent overwriting ROM in RAM
				if(dst > 0x0D000000 && dst < 0x0E000000) {
					if(use16MB==2) {
						return 0;	// Reject data from being loaded into debug 4MB area
					} else if(use16MB==1) {
						dst -= 0x01000000;
					}
				}

				if(setDataBWlist[3]==true && src >= setDataBWlist[0] && src < setDataBWlist[1]) {
					// if(src >= setDataBWlist[0] && src < setDataBWlist[1]) {
						u32 ROM_LOCATION2 = ROM_LOCATION-setDataBWlist[0];

						u32 len2=len;
						if(len2 > 512) {
							len2 -= src%4;
							len2 -= len2 % 32;
						}

						if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
							#ifdef DEBUG
							// send a log command for debug purpose
							// -------------------------------------
							commandRead = 0x026ff800;

							sharedAddr[0] = dst;
							sharedAddr[1] = len2;
							sharedAddr[2] = ROM_LOCATION2+src;
							sharedAddr[3] = commandRead;

							IPC_SendSync(0xEE24);

							while(sharedAddr[3] != (vu32)0);
							// -------------------------------------
							#endif

							// read ROM loaded into RAM
							REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
							copy8(ROM_LOCATION2+src,dst,len2);
							REG_SCFG_EXT = SCFG_EXT_NORM;

							// update cardi common
							cardStruct[0] = src + len2;
							cardStruct[1] = dst + len2;
							cardStruct[2] = len - len2;
						} else {
							#ifdef DEBUG
							// send a log command for debug purpose
							// -------------------------------------
							commandRead = 0x026ff800;

							sharedAddr[0] = page;
							sharedAddr[1] = len2;
							sharedAddr[2] = ROM_LOCATION2+page;
							sharedAddr[3] = commandRead;

							IPC_SendSync(0xEE24);

							while(sharedAddr[3] != (vu32)0);
							// -------------------------------------
							#endif

							// read via the 512b ram cache
							REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
		                                        copy8(ROM_LOCATION2+page+(src%512), dst, len2);
                		                        REG_SCFG_EXT = SCFG_EXT_NORM;
                                		        cardStruct[0] = src + len2;
                                		        cardStruct[1] = dst + len2;
		                                        cardStruct[2] = len - len2;
							//(*readCachedRef)(cacheStruct);
						}
						len = cardStruct[2];
						if(len>0) {
							src = cardStruct[0];
							dst = cardStruct[1];
							page = (src/512)*512;
						}
					// }
					/* if(dataAmount >= 1 && src >= setDataBWlist_1[0] && src < setDataBWlist_1[1]) {
						u32 src2=src;
						src2 -= setDataBWlist_1[0];
						src2 += setDataBWlist[2];
						u32 page2=page;
						page2 -= setDataBWlist_1[0];
						page2 += setDataBWlist[2];

						u32 len2=len;
						if(len2 > 512) {
							len2 -= src%4;
							len2 -= len2 % 32;
						}

						if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
							#ifdef DEBUG
							// send a log command for debug purpose
							// -------------------------------------
							commandRead = 0x026ff800;

							sharedAddr[0] = dst;
							sharedAddr[1] = len2;
							sharedAddr[2] = ROM_LOCATION+src2;
							sharedAddr[3] = commandRead;

							IPC_SendSync(0xEE24);

							while(sharedAddr[3] != (vu32)0);
							// -------------------------------------
							#endif

							// read ROM loaded into RAM
							REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
							copy8(ROM_LOCATION+src2,dst,len2);
							REG_SCFG_EXT = SCFG_EXT_NORM;

							// update cardi common
							cardStruct[0] = src + len2;
							cardStruct[1] = dst + len2;
							cardStruct[2] = len - len2;
						} else {
							#ifdef DEBUG
							// send a log command for debug purpose
							// -------------------------------------
							commandRead = 0x026ff800;

							sharedAddr[0] = page2;
							sharedAddr[1] = len2;
							sharedAddr[2] = ROM_LOCATION+page2;
							sharedAddr[3] = commandRead;

							IPC_SendSync(0xEE24);

							while(sharedAddr[3] != (vu32)0);
							// -------------------------------------
							#endif

							// read via the 512b ram cache
							REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
							copy8(ROM_LOCATION+page2+(src2%512), dst, len2);
							REG_SCFG_EXT = SCFG_EXT_NORM;
        	                                cardStruct[0] = src + len2;
	                                        cardStruct[1] = dst + len2;
                                	        cardStruct[2] = len - len2;
							//(*readCachedRef)(cacheStruct);
						}
						len = cardStruct[2];
						if(len>0) {
							src = cardStruct[0];
							dst = cardStruct[1];
							page = (src/512)*512;
						}
					}
					if(dataAmount == 2 && src >= setDataBWlist_2[0] && src < setDataBWlist_2[1]) {
						u32 src2=src;
						src2 -= setDataBWlist_2[0];
						src2 += setDataBWlist[2];
						src2 += setDataBWlist_1[2];
						u32 page2=page;
						page2 -= setDataBWlist_2[0];
						page2 += setDataBWlist[2];
						page2 += setDataBWlist_1[2];

						u32 len2=len;
						if(len2 > 512) {
							len2 -= src%4;
							len2 -= len2 % 32;
						}

						if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
							#ifdef DEBUG
							// send a log command for debug purpose
							// -------------------------------------
							commandRead = 0x026ff800;

							sharedAddr[0] = dst;
							sharedAddr[1] = len2;
							sharedAddr[2] = ROM_LOCATION+src2;
							sharedAddr[3] = commandRead;

							IPC_SendSync(0xEE24);

							while(sharedAddr[3] != (vu32)0);
							// -------------------------------------
							#endif

							// read ROM loaded into RAM
							REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
							copy8(ROM_LOCATION+src2,dst,len2);
							REG_SCFG_EXT = SCFG_EXT_NORM;

							// update cardi common
							cardStruct[0] = src + len2;
							cardStruct[1] = dst + len2;
							cardStruct[2] = len - len2;
						} else {
							#ifdef DEBUG
							// send a log command for debug purpose
							// -------------------------------------
							commandRead = 0x026ff800;

							sharedAddr[0] = page2;
							sharedAddr[1] = len2;
							sharedAddr[2] = ROM_LOCATION+page2;
							sharedAddr[3] = commandRead;

							IPC_SendSync(0xEE24);

							while(sharedAddr[3] != (vu32)0);
							// -------------------------------------
							#endif

							// read via the 512b ram cache
							REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
							copy8(ROM_LOCATION+page2+(src2%512), dst, len2);
							REG_SCFG_EXT = SCFG_EXT_NORM;
        	                                cardStruct[0] = src + len2;
	                                        cardStruct[1] = dst + len2;
                                	        cardStruct[2] = len - len2;
							//(*readCachedRef)(cacheStruct);
						}
						len = cardStruct[2];
						if(len>0) {
							src = cardStruct[0];
							dst = cardStruct[1];
							page = (src/512)*512;
						}
					} */
				} else if(setDataBWlist[3]==false && src > 0 && src < setDataBWlist[0]) {
					u32 len2=len;
					if(len2 > 512) {
						len2 -= src%4;
						len2 -= len2 % 32;
					}

					if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = dst;
						sharedAddr[1] = len2;
						sharedAddr[2] = ROM_LOCATION+src;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						while(sharedAddr[3] != (vu32)0);
						// -------------------------------------*/
						#endif

						// read ROM loaded into RAM
						REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
						copy8(ROM_LOCATION+src,dst,len2);
						REG_SCFG_EXT = SCFG_EXT_NORM;

						// update cardi common
						cardStruct[0] = src + len2;
						cardStruct[1] = dst + len2;
						cardStruct[2] = len - len2;
					} else {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = page;
						sharedAddr[1] = len2;
						sharedAddr[2] = ROM_LOCATION+page;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						while(sharedAddr[3] != (vu32)0);
						// -------------------------------------
						#endif

						// read via the 512b ram cache
						REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
                        	                copy8(ROM_LOCATION+page+(src%512), dst, len2);
                	                        REG_SCFG_EXT = SCFG_EXT_NORM;
        	                                cardStruct[0] = src + len2;
	                                        cardStruct[1] = dst + len2;
                                	        cardStruct[2] = len - len2;
						//(*readCachedRef)(cacheStruct);
					}
					len = cardStruct[2];
					if(len>0) {
						src = cardStruct[0];
						dst = cardStruct[1];
						page = (src/512)*512;
					}
				} else if(setDataBWlist[3]==false && src >= setDataBWlist[1] && src < romSize) {
					u32 len2=len;
					if(len2 > 512) {
						len2 -= src%4;
						len2 -= len2 % 32;
					}

					if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = dst;
						sharedAddr[1] = len2;
						sharedAddr[2] = ROM_LOCATION-setDataBWlist[2]+src;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						while(sharedAddr[3] != (vu32)0);
						// -------------------------------------*/
						#endif

						// read ROM loaded into RAM
						REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
						copy8(ROM_LOCATION-setDataBWlist[2]+src,dst,len2);
						REG_SCFG_EXT = SCFG_EXT_NORM;

						// update cardi common
						cardStruct[0] = src + len2;
						cardStruct[1] = dst + len2;
						cardStruct[2] = len - len2;
					} else {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = page;
						sharedAddr[1] = len2;
						sharedAddr[2] = ROM_LOCATION-setDataBWlist[2]+page;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						while(sharedAddr[3] != (vu32)0);
						// -------------------------------------
						#endif

						// read via the 512b ram cache
						REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
        	                                copy8(ROM_LOCATION-setDataBWlist[2]+page+(src%512), dst, len2);
	                                        REG_SCFG_EXT = SCFG_EXT_NORM;
                	                        cardStruct[0] = src + len2;
                        	                cardStruct[1] = dst + len2;
                                	        cardStruct[2] = len - len2;
						//(*readCachedRef)(cacheStruct);
					}
					len = cardStruct[2];
					if(len>0) {
						src = cardStruct[0];
						dst = cardStruct[1];
						page = (src/512)*512;
					}
				} else if(page == src && len > setDataBWlist[6] && dst < 0x02700000 && dst > 0x02000000 && ((u32)dst)%4==0) {
					accessCounter++;

					// read directly at arm7 level
					commandRead = 0x025FFB08;

					cacheFlush();

					sharedAddr[0] = dst;
					sharedAddr[1] = len;
					sharedAddr[2] = src;
					sharedAddr[3] = commandRead;

					IPC_SendSync(0xEE24);

					while(sharedAddr[3] != (vu32)0);
				} else {
					accessCounter++;

					u32 sector = (src/setDataBWlist[6])*setDataBWlist[6];

					int slot = GAME_getSlotForSector(sector);
					vu8* buffer = GAME_getCacheAddress(slot);
					// read max CACHE_READ_SIZE via the main RAM cache
					if(slot==-1) {
						// send a command to the arm7 to fill the RAM cache
						commandRead = 0x025FFB08;

						slot = GAME_allocateCacheSlot();

						buffer = GAME_getCacheAddress(slot);

						REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;

						if(needFlushDCCache) DC_FlushRange(buffer, setDataBWlist[6]);

						// write the command
						sharedAddr[0] = buffer;
						sharedAddr[1] = setDataBWlist[6];
						sharedAddr[2] = sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						while(sharedAddr[3] != (vu32)0);

						REG_SCFG_EXT = SCFG_EXT_NORM;
					}

					updateDescriptor(slot, sector);

					u32 len2=len;
					if((src - sector) + len2 > setDataBWlist[6]){
						len2 = sector - src + setDataBWlist[6];
					}

					if(len2 > 512) {
						len2 -= src%4;
						len2 -= len2 % 32;
					}

					if(len2 >= 512 && len2 % 32 == 0 && ((u32)dst)%4 == 0 && src%4 == 0) {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = dst;
						sharedAddr[1] = len2;
						sharedAddr[2] = buffer+src-sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						while(sharedAddr[3] != (vu32)0);
						// -------------------------------------*/
						#endif

						// copy directly
						REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
						copy8(buffer+(src-sector),dst,len2);
						REG_SCFG_EXT = SCFG_EXT_NORM;

						// update cardi common
						cardStruct[0] = src + len2;
						cardStruct[1] = dst + len2;
						cardStruct[2] = len - len2;
					} else {
						#ifdef DEBUG
						// send a log command for debug purpose
						// -------------------------------------
						commandRead = 0x026ff800;

						sharedAddr[0] = page;
						sharedAddr[1] = len2;
						sharedAddr[2] = buffer+page-sector;
						sharedAddr[3] = commandRead;

						IPC_SendSync(0xEE24);

						while(sharedAddr[3] != (vu32)0);
						// -------------------------------------*/
						#endif

						// read via the 512b ram cache
						REG_SCFG_EXT = SCFG_EXT_CACHEACCESS;
                                	        copy8(buffer+(page-sector)+(src%512), dst, len2);
                	                        REG_SCFG_EXT = SCFG_EXT_NORM;
        	                                cardStruct[0] = src + len2;
	                                        cardStruct[1] = dst + len2;
                                        	cardStruct[2] = len - len2;
						//(*readCachedRef)(cacheStruct);
					}
					len = cardStruct[2];
					if(len>0) {
						src = cardStruct[0];
						dst = cardStruct[1];
						page = (src/512)*512;
						sector = (src/setDataBWlist[6])*setDataBWlist[6];
						accessCounter++;
					}
				}
			}
		}
	}
	return 0;
}




