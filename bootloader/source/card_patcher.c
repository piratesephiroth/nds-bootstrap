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

#include <nds/system.h>
#include "card_patcher.h"
#include "common.h"
#include "cardengine_arm9_bin.h"
#include "cardengine_arm7_bin.h"
#include "debugToFile.h"

// Subroutine function signatures arm7
u32 relocateStartSignature[1]  = {0x02FFFFFA};
u32 a7cardReadSignature[2]     = {0x04100010,0x040001A4};
u32 a7something1Signature[2]   = {0xE350000C,0x908FF100};
u32 a7something2Signature[2]   = {0x0000A040,0x040001A0};

u32 a7JumpTableSignatureUniversal[3] = {0xE592000C,0xE5921010,0xE5922014};
u32 a7JumpTableSignatureUniversal_2[3] = {0xE593000C,0xE5931010,0xE5932014};

u32 j_HaltSignature5[3] = {0xE59FC000, 0xE12FFF1C, 0x037FB2DB};
u32 j_HaltSignature5Alt1[3] = {0xE59FC000, 0xE12FFF1C, 0x037FB51F};
u32 j_HaltSignature5Alt2[3] = {0xE59FC000, 0xE12FFF1C, 0x037FB5E3};
u32 j_HaltSignature5Alt3[3] = {0xE59FC000, 0xE12FFF1C, 0x037FB6FB};
u32 j_HaltSignatureThumb5[2] = {0x4718004B, 0x037FB463};

u32 swi12Signature[1] = {0x4770DF12};	// LZ77UnCompReadByCallbackWrite16bit
u32 swiGetPitchTableSignature5[4] = {0x781A4B06, 0xD3030791, 0xD20106D1, 0x1A404904};

// Subroutine function signatures arm9
u32 moduleParamsSignature[2]   = {0xDEC00621, 0x2106C0DE};

// sdk 5 version
u32 a9cardReadSignature5[2]    = {0x04100010, 0x040001A4};
u32 a9cardReadSignatureThumb5[2]    = {0x040001A4, 0x04100010};
u32 cardReadStartSignature5[1] = {0xE92D4FF8};
u32 cardReadStartSignatureThumb5[1] = {0x0A0AB4F0};

u32 a9cardIdSignature[2]      = {0x04100010,0xE92D4038};
u32 cardIdStartSignature[2]   = {0xE92D4010,0xE3A050B8};
u32 cardIdStartSignatureAlt[1]   = {0xE92D4038};
u32 cardIdStartSignatureAlt2[1]   = {0xE92D4010};
u32 cardIdStartSignatureThumb[1]   = {0xB081B500};
u32 cardIdStartSignatureThumbAlt[1]   = {0x202EB508};
u32 cardIdStartSignatureThumbAlt2[1]   = {0x20B8B508};
u32 cardIdStartSignatureThumbAlt3[1]   = {0x24B8B510};
  
//u32 a9instructionBHI[1]       = {0x8A000001};
u32 cardPullOutSignature5[4]   = {0xE92D4038,0xE201003F,0xE3500011,0x1A000011};
//u32 a9cardSendSignature[7]    = {0xE92D40F0,0xE24DD004,0xE1A07000,0xE1A06001,0xE1A01007,0xE3A0000E,0xE3A02000};
u32 cardCheckPullOutSignature1[4]   = {0xE92D4018,0xE24DD004,0xE59F204C,0xE1D210B0};

u32 cardReadCachedStartSignature4[2]   = {0xE92D4038,0xE59F407C};
u32 cardReadCachedEndSignature4[4]   = {0xE5940024,0xE3500000,0x13A00001,0x03A00000};
   
u32 cardReadDmaStartSignature[1]   = {0xE92D43F8};
u32 cardReadDmaStartSignatureAlt[1]   = {0xE92D47F0};
u32 cardReadDmaStartSignatureAlt2[1]   = {0xE92D4FF0};
u32 cardReadDmaEndSignature[2]   = {0x01FF8000,0x000001FF};     

u32 aRandomPatch[4] = {0xE92D43F8, 0xE3A04000, 0xE1A09001, 0xE1A08002};
u32 aRandomPatch2[3] = {0xE59F003C,0xE590001C,0xE3500000};

 

     
// irqEnable
u32 irqEnableStartSignature1[4] = {0xE59FC028,0xE1DC30B0,0xE3A01000,0xE1CC10B0};
u32 irqEnableStartSignature4[4] = {0xE92D4010, 0xE1A04000, 0xEBFFFFF6, 0xE59FC020};

//u32 arenaLowSignature[4] = {0xE1A00100,0xE2800627,0xE2800AFF,0xE5801DA0};  

bool cardReadFound = false;

//
// Look in @data for @find and return the position of it.
//
u32 getOffset(u32* addr, size_t size, u32* find, size_t sizeofFind, int direction)
{
	u32* end = addr + size/sizeof(u32);
	u32* debug = (u32*)0x037D0000;
	debug[3] = end;

    u32 result = 0;
	bool found = false;

	do {
		for(int i=0;i<sizeofFind;i++) {
			if (addr[i] != find[i]) 
			{
				break;
			} else if(i==sizeofFind-1) {
				found = true;
			}
		}
		if(!found) addr+=direction;
	} while (addr != end && !found);

	if (addr == end) {
		return NULL;
	}

	return addr;
}

u32 generateA7Instr(int arg1, int arg2) {
    return (((u32)(arg2 - arg1 - 8) >> 2) & 0xFFFFFF) | 0xEB000000;
}

module_params_t* findModuleParams(const tNDSHeader* ndsHeader)
{
	dbg_printf("Looking for moduleparams\n");
	uint32_t moduleparams = getOffset((u32*)ndsHeader->arm9destination, ndsHeader->arm9binarySize, (u32*)moduleParamsSignature, 2, 1);
	if(!moduleparams)
	{
		dbg_printf("No moduleparams?\n");
		moduleparams = malloc(0x100);
		memset(moduleparams,0,0x100);
		((module_params_t*)(moduleparams - 0x1C))->compressed_static_end = 0;
		((module_params_t*)(moduleparams - 0x1C))->sdk_version = 0x5003001;
	}
	return (module_params_t*)(moduleparams - 0x1C);
}

void decompressLZ77Backwards(uint8_t* addr, size_t size)
{
	uint32_t leng = *((uint32_t*)(addr + size - 4)) + size;
	//byte[] Result = new byte[leng];
	//Array.Copy(Data, Result, Data.Length);
	uint32_t end = (*((uint32_t*)(addr + size - 8))) & 0xFFFFFF;
	uint8_t* result = addr;
	int Offs = (int)(size - ((*((uint32_t*)(addr + size - 8))) >> 24));
	int dstoffs = (int)leng;
	while (true)
	{
		uint8_t header = result[--Offs];
		for (int i = 0; i < 8; i++)
		{
			if ((header & 0x80) == 0) result[--dstoffs] = result[--Offs];
			else
			{
				uint8_t a = result[--Offs];
				uint8_t b = result[--Offs];
				int offs = (((a & 0xF) << 8) | b) + 2;//+ 1;
				int length = (a >> 4) + 2;
				do
				{
					result[dstoffs - 1] = result[dstoffs + offs];
					dstoffs--;
					length--;
				}
				while (length >= 0);
			}
			if (Offs <= size - end)
				return;
			header <<= 1;
		}
	}
}

void ensureArm9Decompressed(const tNDSHeader* ndsHeader, module_params_t* moduleParams)
{
	if(!moduleParams->compressed_static_end)
	{
		dbg_printf("This rom is not compressed\n");
		return; //not compressed
	}
	dbg_printf("This rom is compressed ;)\n");
	decompressLZ77Backwards((uint8_t*)ndsHeader->arm9destination, ndsHeader->arm9binarySize);
	moduleParams->compressed_static_end = 0;
}

u32 patchCardNdsArm9 (const tNDSHeader* ndsHeader, u32* cardEngineLocation, module_params_t* moduleParams, u32 patchMpuRegion, u32 patchMpuSize) {

	u32* debug = (u32*)0x037C6000;
	debug[4] = ndsHeader->arm9destination;
	debug[8] = moduleParams->sdk_version;

	u32* a9cardReadSignature = a9cardReadSignature5;
	u32* a9cardReadSignatureThumb = a9cardReadSignatureThumb5;
	u32* cardReadStartSignature = cardReadStartSignature5;
	u32* cardReadStartSignatureThumb = cardReadStartSignatureThumb5;
	u32* cardPullOutSignature = cardPullOutSignature5;
	u32* cardReadCachedStartSignature = cardReadCachedStartSignature4;
	u32* cardReadCachedEndSignature = cardReadCachedEndSignature4;

	u32 needFlushCache = 0;

	bool usesThumb = false;

	// Find the card read
    u32 cardReadEndOffset =  
        getOffset((u32*)ndsHeader->arm9destination, 0x00300000,//ndsHeader->arm9binarySize,
              (u32*)a9cardReadSignature, 2, 1);
    if (!cardReadEndOffset) {
        dbg_printf("Card read end not found\n");
		//cardReadEndOffset =  
		//	getOffset((u32*)ndsHeader->arm9destination, 0x00300000,//ndsHeader->arm9binarySize,
		//		(u32*)a9cardReadSignatureThumb, 2, 1);
		//if (!cardReadEndOffset) {
		//	dbg_printf("Thumb card read end not found\n");
			return 0;
		//} else {
		//	usesThumb = true;
		//}
    }
	debug[1] = cardReadEndOffset;
    u32 cardReadStartOffset = 0;
	if (usesThumb) {
		cardReadStartOffset =   
			getOffset((u32*)cardReadEndOffset, -0xF0,
				  (u32*)cardReadStartSignatureThumb, 1, -1);
		if (!cardReadStartOffset) {
			dbg_printf("Card read start not found\n");
			return 0;
		}
	} else {
		cardReadStartOffset =   
			getOffset((u32*)cardReadEndOffset, -0x120,
				  (u32*)cardReadStartSignature, 1, -1);
		if (!cardReadStartOffset) {
			dbg_printf("Card read start not found\n");
			return 0;
		}
	}
	cardReadFound = true;
	dbg_printf("Arm9 Card read:\t");
	dbg_hexa(cardReadStartOffset);
	dbg_printf("\n");

	u32 cardPullOutOffset =   
		getOffset((u32*)ndsHeader->arm9destination, 0x00300000,//, ndsHeader->arm9binarySize,
			(u32*)cardPullOutSignature, 4, 1);
    if (!cardPullOutOffset) {
        dbg_printf("Card pull out handler not found\n");
        //return 0;
    } else {
		dbg_printf("Card pull out handler:\t");
		dbg_hexa(cardPullOutOffset);
		dbg_printf("\n");
	}


    u32 cardReadCachedEndOffset =  
        getOffset((u32*)ndsHeader->arm9destination, 0x00300000,//ndsHeader->arm9binarySize,
              (u32*)cardReadCachedEndSignature, 4, 1);
    if (!cardReadCachedEndOffset) {
        dbg_printf("Card read cached end not found\n");
        //return 0;
    }
    u32 cardReadCachedOffset =   
        getOffset((u32*)cardReadCachedEndOffset, -0xFF,
              (u32*)cardReadCachedStartSignature, 2, -1);
    if (!cardReadStartOffset) {
        dbg_printf("Card read cached start not found\n");
        //return 0;
    }
	dbg_printf("Card read cached :\t");
	dbg_hexa(cardReadCachedOffset);
	dbg_printf("\n");

	u32 cardReadDmaOffset = 0;
	u32 cardReadDmaEndOffset =  
        getOffset((u32*)ndsHeader->arm9destination, 0x00300000,//ndsHeader->arm9binarySize,
              (u32*)cardReadDmaEndSignature, 2, 1);
    if (!cardReadDmaEndOffset) {
        dbg_printf("Card read dma end not found\n");
    } else {
		dbg_printf("Card read dma end :\t");
		dbg_hexa(cardReadDmaEndOffset);
		dbg_printf("\n");
		cardReadDmaOffset =   
			getOffset((u32*)cardReadDmaEndOffset, -0x200,
				  (u32*)cardReadDmaStartSignature, 1, -1);
		if (!cardReadDmaOffset) {
			dbg_printf("Card read dma start not found\n");
			cardReadDmaOffset =   
				getOffset((u32*)cardReadDmaEndOffset, -0x200,
					  (u32*)cardReadDmaStartSignatureAlt, 1, -1);
			if (!cardReadDmaOffset) {
				dbg_printf("Card read dma start alt not found\n");
			}
		}
		if (!cardReadDmaOffset) {
			//dbg_printf("Card read dma start not found\n");
			cardReadDmaOffset =   
				getOffset((u32*)cardReadDmaEndOffset, -0x200,
					  (u32*)cardReadDmaStartSignatureAlt2, 1, -1);
			if (!cardReadDmaOffset) {
				dbg_printf("Card read dma start alt2 not found\n");
			}
		}
	}    

	// Find the card id
	u32 cardIdStartOffset = 0;
    u32 cardIdEndOffset =  
        getOffset((u32*)cardReadEndOffset+0x10, ndsHeader->arm9binarySize,
              (u32*)a9cardIdSignature, 2, 1);
			  
	if(!cardIdEndOffset){
		cardIdEndOffset =  
        getOffset((u32*)ndsHeader->arm9destination, ndsHeader->arm9binarySize,
              (u32*)a9cardIdSignature, 2, 1);
	}
    if (!cardIdEndOffset) {
        dbg_printf("Card id end not found\n");
    } else {
		debug[1] = cardIdEndOffset;
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignature, 2, -1);
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureAlt, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureAlt2, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureThumb, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureThumbAlt, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureThumbAlt2, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureThumbAlt3, 1, -1);
		}
		if (!cardIdStartOffset) {
			dbg_printf("Card id start not found\n");
		} else {
			dbg_printf("Card id :\t");
			dbg_hexa(cardIdStartOffset);
			dbg_printf("\n");
		}
	}

		u32 randomPatchOffset =  
				getOffset((u32*)ndsHeader->arm9destination, 0x00300000,//ndsHeader->arm9binarySize,
					  (u32*)aRandomPatch, 4, 1);
			if(randomPatchOffset){
				*(u32*)(randomPatchOffset) = 0xE3A00000;
				*(u32*)(randomPatchOffset+4) = 0xE12FFF1E;
			}
				if (!randomPatchOffset) {
					//dbg_printf("Random patch not found\n"); Don't bother logging it.
				}

		u32 randomPatch2Offset =
                                getOffset((u32*)ndsHeader->arm9destination, 0x00300000,//ndsHeader->arm9binarySize,
                                          (u32*)aRandomPatch2, 3, 1);
                        if(randomPatch2Offset){
                                *(u32*)(randomPatch2Offset) = 0xE3A00000;
                                *(u32*)(randomPatch2Offset+4) = 0xE12FFF1E;
                        }
                                if (!randomPatchOffset) {
                                        //dbg_printf("Random patch not found\n"); Don't bother logging it.
                                }




	debug[2] = cardEngineLocation;

	u32* patches = 0;
	if (usesThumb) {
		patches = (u32*) cardEngineLocation[1];
	} else {
		patches = (u32*) cardEngineLocation[0];
	}

	cardEngineLocation[3] = moduleParams->sdk_version;

	u32* cardReadPatch = (u32*) patches[0];

	u32* cardPullOutPatch = patches[6];

	u32* cardIdPatch = patches[3];

	u32* cardDmaPatch = patches[4];

	debug[5] = patches;

	u32* card_struct = 0;
	if (usesThumb) {
		card_struct = ((u32*)cardReadEndOffset) - 2;
	} else {
		card_struct = ((u32*)cardReadEndOffset) - 1;
	}
	//u32* cache_struct = ((u32*)cardIdStartOffset) - 1;

	debug[6] = *card_struct;
	//debug[7] = *cache_struct;

	cardEngineLocation[5] = ((u32*)*card_struct)+7;
	//cardEngineLocation[6] = *cache_struct;

	// cache management alternative
	*((u32*)patches[5]) = ((u32*)*card_struct)+7;

	*((u32*)patches[7]) = cardPullOutOffset+4;
	*((u32*)patches[8]) = cardReadCachedOffset;
	patches[10] = needFlushCache;

	//copyLoop (oldArenaLow, cardReadPatch, 0xF0);

	if (usesThumb) {
		copyLoop ((u32*)cardReadStartOffset, cardReadPatch, 0x60);
	} else {
		copyLoop ((u32*)cardReadStartOffset, cardReadPatch, 0xF0);
	}

	copyLoop ((u32*)(cardPullOutOffset), cardPullOutPatch, 0x4);

	if (cardIdStartOffset) {
		if (usesThumb) {
			copyLoop ((u32*)cardIdStartOffset, cardIdPatch, 0x4);
		} else {
			copyLoop ((u32*)cardIdStartOffset, cardIdPatch, 0x8);
		}
	}

	if (cardReadDmaOffset) {
		dbg_printf("Card read dma :\t");
		dbg_hexa(cardReadDmaOffset);
		dbg_printf("\n");

		if (usesThumb) {
			copyLoop ((u32*)cardReadDmaOffset, cardDmaPatch, 0x4);
		} else {
			copyLoop ((u32*)cardReadDmaOffset, cardDmaPatch, 0x8);
		}
	}

	dbg_printf("ERR_NONE");
	return 0;
}

/*u32 patchCardNdsArm9Overlay (u32* overlayLocation, u32 overlayLocationSize, u32* cardEngineLocation, module_params_t* moduleParams, u32 patchMpuRegion, u32 patchMpuSize) {

	u32* debug = (u32*)0x037C6000;
	debug[4] = overlayLocation;
	debug[8] = moduleParams->sdk_version;

	u32* a9cardReadSignature = a9cardReadSignature5;
	u32* cardReadStartSignature = cardReadStartSignature5;
	u32* cardPullOutSignature = cardPullOutSignature5;
	u32* cardReadCachedStartSignature = cardReadCachedStartSignature4;
	u32* cardReadCachedEndSignature = cardReadCachedEndSignature4;

	u32 needFlushCache = 0;

	// Find the card read
    u32 cardReadEndOffset =  
        getOffset((u32*)overlayLocation, 0x00300000,//ndsHeader->arm9binarySize,
              (u32*)a9cardReadSignature, 2, 1);
    if (!cardReadEndOffset) {
        dbg_printf("Card read end not found\n");
        return 0;
    }
	debug[1] = cardReadEndOffset;
    u32 cardReadStartOffset =   
        getOffset((u32*)cardReadEndOffset, -0x120,
              (u32*)cardReadStartSignature, 1, -1);
    if (!cardReadStartOffset) {
        dbg_printf("Card read start not found\n");
        return 0;
    }
	dbg_printf("Arm9 Card read:\t");
	dbg_hexa(cardReadStartOffset);
	dbg_printf("\n");

	u32 cardPullOutOffset =   
        getOffset((u32*)overlayLocation, 0x00300000,//, ndsHeader->arm9binarySize,
              (u32*)cardPullOutSignature, 4, 1);
    if (!cardPullOutOffset) {
        dbg_printf("Card pull out handler not found\n");
        //return 0;
    } else {
		dbg_printf("Card pull out handler:\t");
		dbg_hexa(cardPullOutOffset);
		dbg_printf("\n");
	}


    u32 cardReadCachedEndOffset =  
        getOffset((u32*)overlayLocation, 0x00300000,//ndsHeader->arm9binarySize,
              (u32*)cardReadCachedEndSignature, 4, 1);
    if (!cardReadCachedEndOffset) {
        dbg_printf("Card read cached end not found\n");
        //return 0;
    }
    u32 cardReadCachedOffset =   
        getOffset((u32*)cardReadCachedEndOffset, -0xFF,
              (u32*)cardReadCachedStartSignature, 2, -1);
    if (!cardReadStartOffset) {
        dbg_printf("Card read cached start not found\n");
        //return 0;
    }
	dbg_printf("Card read cached :\t");
	dbg_hexa(cardReadCachedOffset);
	dbg_printf("\n");

	u32 cardReadDmaOffset = 0;
	u32 cardReadDmaEndOffset =  
        getOffset((u32*)overlayLocation, 0x00300000,//ndsHeader->arm9binarySize,
              (u32*)cardReadDmaEndSignature, 2, 1);
    if (!cardReadDmaEndOffset) {
        dbg_printf("Card read dma end not found\n");
    } else {
		dbg_printf("Card read dma end :\t");
		dbg_hexa(cardReadDmaEndOffset);
		dbg_printf("\n");
		cardReadDmaOffset =   
			getOffset((u32*)cardReadDmaEndOffset, -0x200,
				  (u32*)cardReadDmaStartSignature, 1, -1);
		if (!cardReadDmaOffset) {
			dbg_printf("Card read dma start not found\n");
			cardReadDmaOffset =   
				getOffset((u32*)cardReadDmaEndOffset, -0x200,
					  (u32*)cardReadDmaStartSignatureAlt, 1, -1);
			if (!cardReadDmaOffset) {
				dbg_printf("Card read dma start alt not found\n");
			}
		}
		if (!cardReadDmaOffset) {
			//dbg_printf("Card read dma start not found\n");
			cardReadDmaOffset =   
				getOffset((u32*)cardReadDmaEndOffset, -0x200,
					  (u32*)cardReadDmaStartSignatureAlt2, 1, -1);
			if (!cardReadDmaOffset) {
				dbg_printf("Card read dma start alt2 not found\n");
			}
		}
	}    

	// Find the card id
	u32 cardIdStartOffset = 0;
    u32 cardIdEndOffset =  
        getOffset((u32*)cardReadEndOffset+0x10, overlayLocationSize,
              (u32*)a9cardIdSignature, 2, 1);
			  
	if(!cardIdEndOffset){
		cardIdEndOffset =  
        getOffset((u32*)overlayLocation, overlayLocationSize,
              (u32*)a9cardIdSignature, 2, 1);
	}
    if (!cardIdEndOffset) {
        dbg_printf("Card id end not found\n");
    } else {
		debug[1] = cardIdEndOffset;
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignature, 2, -1);
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureAlt, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureAlt2, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureThumb, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureThumbAlt, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureThumbAlt2, 1, -1);
		}
		if (!cardIdStartOffset) {
		cardIdStartOffset =   
			getOffset((u32*)cardIdEndOffset, -0x100,
				  (u32*)cardIdStartSignatureThumbAlt3, 1, -1);
		}
		if (!cardIdStartOffset) {
			dbg_printf("Card id start not found\n");
		} else {
			dbg_printf("Card id :\t");
			dbg_hexa(cardIdStartOffset);
			dbg_printf("\n");
		}
	}

		u32 randomPatchOffset =  
				getOffset((u32*)overlayLocation, 0x00300000,//ndsHeader->arm9binarySize,
					  (u32*)aRandomPatch, 4, 1);
			if(randomPatchOffset){
				*(u32*)(randomPatchOffset) = 0xE3A00000;
				*(u32*)(randomPatchOffset+4) = 0xE12FFF1E;
			}
				if (!randomPatchOffset) {
					//dbg_printf("Random patch not found\n"); Don't bother logging it.
				}

		u32 randomPatch2Offset =
                                getOffset((u32*)overlayLocation, 0x00300000,//ndsHeader->arm9binarySize,
                                          (u32*)aRandomPatch2, 3, 1);
                        if(randomPatch2Offset){
                                *(u32*)(randomPatch2Offset) = 0xE3A00000;
                                *(u32*)(randomPatch2Offset+4) = 0xE12FFF1E;
                        }
                                if (!randomPatchOffset) {
                                        //dbg_printf("Random patch not found\n"); Don't bother logging it.
                                }




	debug[2] = cardEngineLocation;

	u32* patches =  (u32*) cardEngineLocation[0];

	//cardEngineLocation[3] = moduleParams->sdk_version;

	u32* cardReadPatch = (u32*) patches[0];

	u32* cardPullOutPatch = patches[6];

	u32* cardIdPatch = patches[3];

	u32* cardDmaPatch = patches[4];

	debug[5] = patches;

	u32* card_struct = ((u32*)cardReadEndOffset) - 1;
	//u32* cache_struct = ((u32*)cardIdStartOffset) - 1;

	debug[6] = *card_struct;
	//debug[7] = *cache_struct;

	//cardEngineLocation[5] = ((u32*)*card_struct)+7;

	// cache management alternative
	*((u32*)patches[5]) = ((u32*)*card_struct)+7;

	*((u32*)patches[7]) = cardPullOutOffset+4;
	*((u32*)patches[8]) = cardReadCachedOffset;
	patches[10] = needFlushCache;

	//copyLoop (oldArenaLow, cardReadPatch, 0xF0);

	copyLoop ((u32*)cardReadStartOffset, cardReadPatch, 0xF0);

	copyLoop ((u32*)(cardPullOutOffset), cardPullOutPatch, 0x5C);

	if (cardIdStartOffset) {
		copyLoop ((u32*)cardIdStartOffset, cardIdPatch, 0x8);
	}

	if (cardReadDmaOffset) {
		dbg_printf("Card read dma :\t");
		dbg_hexa(cardReadDmaOffset);
		dbg_printf("\n");

		copyLoop ((u32*)cardReadDmaOffset, cardDmaPatch, 0x8);
	}

	dbg_printf("ERR_NONE");
	return 0;
}*/

u32 savePatchV5 (const tNDSHeader* ndsHeader, u32* cardEngineLocation, module_params_t* moduleParams, u32 saveFileCluster) {

    dbg_printf("\nArm7 (patch vAll)\n");

	// Find the relocation signature
    u32 relocationStart = getOffset((u32*)ndsHeader->arm7destination, ndsHeader->arm7binarySize,
        relocateStartSignature, 1, 1);
    if (!relocationStart) {
        dbg_printf("Relocation start not found\n");
		return 0;
    }
    
    // Find the second relocation signature
    relocationStart = getOffset((u32*)relocationStart, ndsHeader->arm7binarySize-relocationStart,
        relocateStartSignature, 1, 1);
    if (!relocationStart) {
        dbg_printf("Relocation start 2nd signature not found\n");
		return 0;
    }

	// Validate the relocation signature
    u32 forwardedRelocStartAddr = relocationStart + 0x20;
    if (!*(u32*)forwardedRelocStartAddr)
        forwardedRelocStartAddr += 4;
    u32 vAddrOfRelocSrc =
        *(u32*)(forwardedRelocStartAddr + 8);
    // sanity checks
    u32 relocationCheck1 =
        *(u32*)(forwardedRelocStartAddr + 0xC);
    u32 relocationCheck2 =
        *(u32*)(forwardedRelocStartAddr + 0x10);
    if ( vAddrOfRelocSrc != relocationCheck1
      || vAddrOfRelocSrc != relocationCheck2) {
        dbg_printf("Error in relocation checking\n");
		return 0;
    }


    // Get the remaining details regarding relocation
    u32 valueAtRelocStart =
        *(u32*)forwardedRelocStartAddr;
    u32 relocDestAtSharedMem =
        *(u32*)valueAtRelocStart;
    if (relocDestAtSharedMem != 0x37F8000) { // shared memory in RAM
        // Try again
        vAddrOfRelocSrc +=
            *(u32*)(valueAtRelocStart + 4);
        relocDestAtSharedMem =
            *(u32*)(valueAtRelocStart + 0xC);
        if (relocDestAtSharedMem != 0x37F8000) {
            dbg_printf("Error in finding shared memory relocation area\n");
			return 0;
        }
    }

    dbg_printf("Relocation src: ");
	dbg_hexa(vAddrOfRelocSrc);
	dbg_printf("\n");
	dbg_printf("Relocation dst: ");
	dbg_hexa(relocDestAtSharedMem);
	dbg_printf("\n");

    u32 JumpTableFunc = getOffset((u32*)ndsHeader->arm7destination, ndsHeader->arm7binarySize,
        a7JumpTableSignatureUniversal, 3, 1);

	if(!JumpTableFunc){
		JumpTableFunc = getOffset((u32*)ndsHeader->arm7destination, ndsHeader->arm7binarySize,
        a7JumpTableSignatureUniversal_2, 3, 1);
	}
		
	dbg_printf("JumpTableFunc: ");
	dbg_hexa(JumpTableFunc);
	dbg_printf("\n");


	u32* patches =  (u32*) cardEngineLocation[0];
	u32* arm7Function =  (u32*) patches[9];
	u32 srcAddr;

	if(true){

		u32* eepromRead = (u32*) (JumpTableFunc + 0xC);
		dbg_printf("Eeprom read:\t");
		dbg_hexa((u32)eepromRead);
		dbg_printf("\n");
		srcAddr = JumpTableFunc + 0xC  - vAddrOfRelocSrc + relocDestAtSharedMem ;
		u32 patchRead = generateA7Instr(srcAddr,
			arm7Function[5]);
		*eepromRead=patchRead;

		u32* eepromPageWrite = (u32*) (JumpTableFunc + 0x24);
		dbg_printf("Eeprom page write:\t");
		dbg_hexa((u32)eepromPageWrite);
		dbg_printf("\n");
		srcAddr = JumpTableFunc + 0x24 - vAddrOfRelocSrc + relocDestAtSharedMem ;
		u32 patchWrite = generateA7Instr(srcAddr,
			arm7Function[3]);
		*eepromPageWrite=patchWrite;

		u32* eepromPageProg = (u32*) (JumpTableFunc + 0x3C);
		dbg_printf("Eeprom page prog:\t");
		dbg_hexa((u32)eepromPageProg);
		dbg_printf("\n");
		srcAddr = JumpTableFunc + 0x3C - vAddrOfRelocSrc + relocDestAtSharedMem ;
		u32 patchProg = generateA7Instr(srcAddr,
			arm7Function[4]);
		*eepromPageProg=patchProg;

		u32* eepromPageVerify = (u32*) (JumpTableFunc + 0x54);
		dbg_printf("Eeprom verify:\t");
		dbg_hexa((u32)eepromPageVerify);
		dbg_printf("\n");
		srcAddr =  JumpTableFunc + 0x54 - vAddrOfRelocSrc + relocDestAtSharedMem ;
		u32 patchVerify = generateA7Instr(srcAddr,
			arm7Function[2]);
		*eepromPageVerify=patchVerify;


		u32* eepromPageErase = (u32*) (JumpTableFunc + 0x68);
		dbg_printf("Eeprom page erase:\t");
		dbg_hexa((u32)eepromPageErase);
		dbg_printf("\n");
		srcAddr = JumpTableFunc + 0x68 - vAddrOfRelocSrc + relocDestAtSharedMem ;
		u32 patchErase = generateA7Instr(srcAddr,
			arm7Function[1]);
		*eepromPageErase=patchErase; 

		arm7Function[8] = saveFileCluster;
	}

	return 1;
}

void patchSwiHalt (const tNDSHeader* ndsHeader, u32* cardEngineLocation) {
	u32* patches =  (u32*) cardEngineLocation[0];
	bool isThumb = false;
	u32 swiHaltOffset =   
		getOffset((u32*)ndsHeader->arm7destination, 0x00010000,//, ndsHeader->arm7binarySize,
			  (u32*)j_HaltSignature5, 3, 1);
	if (!swiHaltOffset) {
		dbg_printf("swiHalt SDK5 call not found\n");
		swiHaltOffset =   
			getOffset((u32*)ndsHeader->arm7destination, 0x00010000,//, ndsHeader->arm7binarySize,
				  (u32*)j_HaltSignature5Alt1, 3, 1);
	}
	if (!swiHaltOffset) {
		dbg_printf("swiHalt SDK5 call alt 1 not found\n");
		swiHaltOffset =   
			getOffset((u32*)ndsHeader->arm7destination, 0x00010000,//, ndsHeader->arm7binarySize,
				  (u32*)j_HaltSignature5Alt2, 3, 1);
	}
	if (!swiHaltOffset) {
		dbg_printf("swiHalt SDK5 call alt 2 not found\n");
		swiHaltOffset =   
			getOffset((u32*)ndsHeader->arm7destination, 0x00010000,//, ndsHeader->arm7binarySize,
				  (u32*)j_HaltSignature5Alt3, 3, 1);
	}
	if (!swiHaltOffset) {
		dbg_printf("swiHalt SDK5 call alt 3 not found\n");
		isThumb = true;
		swiHaltOffset =   
			getOffset((u32*)ndsHeader->arm7destination, 0x00010000,//, ndsHeader->arm7binarySize,
				  (u32*)j_HaltSignatureThumb5, 2, 1);
	}
	if (!swiHaltOffset) {
		dbg_printf("swiHalt SDK5 thumb call not found\n");
	}
	if (swiHaltOffset>0) {
		dbg_printf("swiHalt call found\n");
		u32* swiHaltPatch = (u32*) patches[12-isThumb];
		if (isThumb) {
			copyLoop ((u32*)swiHaltOffset, swiHaltPatch, 0x8);
		} else {
			copyLoop ((u32*)swiHaltOffset, swiHaltPatch, 0xC);
		}
	}
}

u32 patchCardNdsArm7 (const tNDSHeader* ndsHeader, u32* cardEngineLocation, module_params_t* moduleParams, u32 saveFileCluster) {
	u32* debug = (u32*)0x037C6000;

	u32* patches =  (u32*) cardEngineLocation[0];

	if(REG_SCFG_ROM != 0x703) {
		u32 swi12Offset =   
			getOffset((u32*)ndsHeader->arm7destination, 0x00010000,//, ndsHeader->arm7binarySize,
				  (u32*)swi12Signature, 1, 1);
		if (!swi12Offset) {
			dbg_printf("swi 0x12 call not found\n");
		} else {
			// Patch to call swi 0x02 instead of 0x12
			dbg_printf("swi 0x12 call found\n");
			u32* swi12Patch = (u32*) patches[10];
			copyLoop ((u32*)swi12Offset, swi12Patch, 0x4);
		}

		u32 swiGetPitchTableOffset =   
			getOffset((u32*)ndsHeader->arm7destination, 0x00010000,//, ndsHeader->arm9binarySize,
				  (u32*)swiGetPitchTableSignature5, 4, 1);
		if (!swiGetPitchTableOffset) {
			dbg_printf("swiGetPitchTable call not found\n");
		} else {
			u32* swiGetPitchTablePatch = (u32*) patches[13];
			copyLoop ((u32*)swiGetPitchTableOffset, swiGetPitchTablePatch, 0xC);
			dbg_printf("swiGetPitchTable call found\n");
		}
	}
	patchSwiHalt(ndsHeader, cardEngineLocation);
       
    u32 saveResult = savePatchV5(ndsHeader, cardEngineLocation, moduleParams, saveFileCluster);	

	u32* irqEnableStartSignature = irqEnableStartSignature1;
	u32* cardCheckPullOutSignature = cardCheckPullOutSignature1;

	if(moduleParams->sdk_version > 0x4000000) {
		irqEnableStartSignature = irqEnableStartSignature4;
	}

	u32 cardCheckPullOutOffset =   
        getOffset((u32*)ndsHeader->arm7destination, 0x00400000,//, ndsHeader->arm9binarySize,
              (u32*)cardCheckPullOutSignature, 4, 1);
    if (!cardCheckPullOutOffset) {
        dbg_printf("Card check pull out not found\n");
        //return 0;
    } else {
		debug[0] = cardCheckPullOutOffset;
		dbg_printf("Card check pull out found\n");
	}

	u32 cardIrqEnableOffset =   
        getOffset((u32*)ndsHeader->arm7destination, 0x00400000,//, ndsHeader->arm9binarySize,
              (u32*)irqEnableStartSignature, 4, 1);
    if (!cardIrqEnableOffset) {
        dbg_printf("irq enable not found\n");
        return 0;
    }
	debug[0] = cardIrqEnableOffset;
    dbg_printf("irq enable found\n");


	cardEngineLocation[3] = moduleParams->sdk_version;

	u32* cardIrqEnablePatch = (u32*) patches[2];
	u32* cardCheckPullOutPatch = (u32*) patches[1];

	if(cardCheckPullOutOffset>0)
		copyLoop ((u32*)cardCheckPullOutOffset, cardCheckPullOutPatch, 0x4);

	copyLoop ((u32*)cardIrqEnableOffset, cardIrqEnablePatch, 0x30);


	dbg_printf("ERR_NONE");
	return 0;
}

u32 patchCardNds (const tNDSHeader* ndsHeader, u32* cardEngineLocationArm7, u32* cardEngineLocationArm9, module_params_t* moduleParams, 
		u32 saveFileCluster, u32 patchMpuRegion, u32 patchMpuSize) {

	//Debug stuff.

	/*aFile myDebugFile = getBootFileCluster ("NDSBTSR2.LOG");
	enableDebug(myDebugFile);*/

	dbg_printf("patchCardNds");

	patchCardNdsArm9(ndsHeader, cardEngineLocationArm9, moduleParams, patchMpuRegion, patchMpuSize);
	if (cardReadFound) {
		patchCardNdsArm7(ndsHeader, cardEngineLocationArm7, moduleParams, saveFileCluster);

		dbg_printf("ERR_NONE");
		return ERR_NONE;
	} else {
		dbg_printf("ERR_LOAD_OTHR");
		return ERR_LOAD_OTHR;
	}
}
