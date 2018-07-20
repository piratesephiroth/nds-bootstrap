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

#include <nds/fifomessages.h>
#include <nds/ipc.h>
#include <nds/interrupts.h>
#include <nds/system.h>
#include <nds/input.h>
#include <nds/arm7/audio.h>
#include "debugToFile.h"
#include "cardEngine.h"
#include "io_dldi.h"
#include "fat.h"

extern void* memcpy(const void * src0, void * dst0, int len0);	// Fixes implicit declaration @ line 126 & 136
extern int tryLockMutex(int * addr);					// Fixes implicit declaration @ line 145
extern int lockMutex(int * addr);					    // Fixes implicit declaration
extern int unlockMutex(int * addr);					// Fixes implicit declaration @ line 223

static bool initialized = false;
static bool initializedIRQ = false;
static bool calledViaIPC = false;
extern vu32* volatile cardStruct;
extern u32 fileCluster;
extern u32 saveCluster;
extern u32 saveSize;
extern u32 sdk_version;
extern u32 language;
vu32* volatile sharedAddr = (vu32*)0x027FFB08;
static aFile * romFile = (aFile *)0x237B000;
static aFile * savFile = ((aFile *)0x237B000)+1;

static int accessCounter = 0;

static int cardEgnineCommandMutex = 0;
static int saveMutex = 0;

void initialize() {
	if(!initialized) {
		_io_dldi.fn_startup();
		FAT_InitFiles(false);
		//romFile = getFileFromCluster(fileCluster);
        //buildFatTableCache(&romFile, 3);
        #ifdef DEBUG	
        if(romFile->fatTableCached) {
            nocashMessage("fat table cached");
        } else {
           nocashMessage("fat table not cached"); 
        }
        #endif
        
		/*if(saveCluster>0)
			savFile = getFileFromCluster(saveCluster);
		else
			savFile.firstCluster = CLUSTER_FREE;*/
            
		#ifdef DEBUG		
		aFile myDebugFile = getBootFileCluster ("NDSBTSRP.LOG");
		enableDebug(myDebugFile);
		dbg_printf("logging initialized\n");		
		dbg_printf("sdk version :");
		dbg_hexa(sdk_version);		
		dbg_printf("\n");	
		dbg_printf("rom file :");
		dbg_hexa(fileCluster);	
		dbg_printf("\n");	
		dbg_printf("save file :");
		dbg_hexa(saveCluster);	
		dbg_printf("\n");
		#endif
        			
		initialized=true;
	}
	
}

void log_arm9() {
	#ifdef DEBUG		
	u32 src = *(vu32*)(sharedAddr+2);
	u32 dst = *(vu32*)(sharedAddr);
	u32 len = *(vu32*)(sharedAddr+1);
	u32 marker = *(vu32*)(sharedAddr+3);

	dbg_printf("\ncard read received\n");

	if(calledViaIPC) {
		dbg_printf("\ntriggered via IPC\n");
	}
	dbg_printf("\nstr : \n");
	dbg_hexa(cardStruct);
	dbg_printf("\nsrc : \n");
	dbg_hexa(src);
	dbg_printf("\ndst : \n");
	dbg_hexa(dst);
	dbg_printf("\nlen : \n");
	dbg_hexa(len);
	dbg_printf("\nmarker : \n");
	dbg_hexa(marker);

	dbg_printf("\nlog only \n");
	#endif
}

void cardRead_arm9() {
	u32 src = *(vu32*)(sharedAddr+2);
	u32 dst = *(vu32*)(sharedAddr);
	u32 len = *(vu32*)(sharedAddr+1);
	u32 marker = *(vu32*)(sharedAddr+3);

	#ifdef DEBUG
	dbg_printf("\ncard read received v2\n");

	if(calledViaIPC) {
		dbg_printf("\ntriggered via IPC\n");
	}

	dbg_printf("\nstr : \n");
	dbg_hexa(cardStruct);
	dbg_printf("\nsrc : \n");
	dbg_hexa(src);
	dbg_printf("\ndst : \n");
	dbg_hexa(dst);
	dbg_printf("\nlen : \n");
	dbg_hexa(len);
	dbg_printf("\nmarker : \n");
	dbg_hexa(marker);	
	#endif

    #ifdef DEBUG
    nocashMessage("fileRead romFile");
    #endif
	fileRead(dst,*romFile,src,len);
	/*if(*(u32*)(0x0C9328ac) == 0x4B434148){ //Primary fix for Mario's Holiday
		*(u32*)(0x0C9328ac) = 0xA00;
	}*/

	#ifdef DEBUG
	dbg_printf("\nread \n");
	if(is_aligned(dst,4) || is_aligned(len,4)) {
		dbg_printf("\n aligned read : \n");
	} else {
		dbg_printf("\n misaligned read : \n");
	}
	#endif
}

void asyncCardRead_arm9() {
	u32 src = *(vu32*)(sharedAddr+2);
	u32 dst = *(vu32*)(sharedAddr);
	u32 len = *(vu32*)(sharedAddr+1);
	u32 marker = *(vu32*)(sharedAddr+3);

	#ifdef DEBUG
	dbg_printf("\nasync card read received\n");

	if(calledViaIPC) {
		dbg_printf("\ntriggered via IPC\n");
	}

	dbg_printf("\nstr : \n");
	dbg_hexa(cardStruct);
	dbg_printf("\nsrc : \n");
	dbg_hexa(src);
	dbg_printf("\ndst : \n");
	dbg_hexa(dst);
	dbg_printf("\nlen : \n");
	dbg_hexa(len);
	dbg_printf("\nmarker : \n");
	dbg_hexa(marker);	
	#endif

    #ifdef DEBUG
    nocashMessage("fileRead romFile");
    #endif
	fileRead(dst,*romFile,src,len);

	#ifdef DEBUG
	dbg_printf("\nread \n");
	if(is_aligned(dst,4) || is_aligned(len,4)) {
		dbg_printf("\n aligned read : \n");
	} else {
		dbg_printf("\n misaligned read : \n");
	}
	#endif
}

void runCardEngineCheck (void) {
	//dbg_printf("runCardEngineCheck\n");
	#ifdef DEBUG		
	nocashMessage("runCardEngineCheck");
	#endif	

	if(tryLockMutex(&cardEgnineCommandMutex)) {
		initialize();

		//nocashMessage("runCardEngineCheck mutex ok");
		if(*(vu32*)(0x027FFB14) == (vu32)0x026ff800)
		{
			log_arm9();
			*(vu32*)(0x027FFB14) = 0;
		}

		if(*(vu32*)(0x027FFB14) == (vu32)0x025FFB08)
		{
			cardRead_arm9();
			*(vu32*)(0x027FFB14) = 0;
		}

		if(*(vu32*)(0x027FFB14) == (vu32)0x020ff800)
		{
			asyncCardRead_arm9();
			*(vu32*)(0x027FFB14) = 0;
		}
		unlockMutex(&cardEgnineCommandMutex);
	}
}

void runCardEngineCheckHalt (void) {
	//dbg_printf("runCardEngineCheckHalt\n");
	#ifdef DEBUG		
	nocashMessage("runCardEngineCheckHalt");
	#endif	

    // lockMutex should be possible to be used here instead of tryLockMutex since the execution of irq is not blocked
    // to be checked
	if(lockMutex(&cardEgnineCommandMutex)) {
		initialize();

		//nocashMessage("runCardEngineCheck mutex ok");
		if(*(vu32*)(0x027FFB14) == (vu32)0x026ff800)
		{
			log_arm9();
			*(vu32*)(0x027FFB14) = 0;
		}

		if(*(vu32*)(0x027FFB14) == (vu32)0x025FFB08)
		{
			cardRead_arm9();
			*(vu32*)(0x027FFB14) = 0;
		}

		if(*(vu32*)(0x027FFB14) == (vu32)0x020ff800)
		{
			asyncCardRead_arm9();
			*(vu32*)(0x027FFB14) = 0;
		}
		unlockMutex(&cardEgnineCommandMutex);
	}
}


//---------------------------------------------------------------------------------
void myIrqHandlerFIFO(void) {
//---------------------------------------------------------------------------------
	#ifdef DEBUG		
	nocashMessage("myIrqHandlerFIFO");
	#endif	
	
	calledViaIPC = true;
    
    runCardEngineCheck();
}

//---------------------------------------------------------------------------------
void mySwiHalt(void) {
//---------------------------------------------------------------------------------
	#ifdef DEBUG		
	nocashMessage("mySwiHalt");
	#endif	
	
	calledViaIPC = false;

	/*if (!runViaIRQ)*/ runCardEngineCheckHalt();
}


void myIrqHandlerVBlank(void) {
	#ifdef DEBUG		
	nocashMessage("myIrqHandlerVBlank");
	#endif	

	calledViaIPC = false;

	if (language >= 0 && language < 6) {
		*(u8*)(0x027FFCE4) = language;	// Change language
	}

	runCardEngineCheck();

    //#ifdef DEBUG
    //nocashMessage("cheat_engine_start\n");
    //#endif	
	
    //cheat_engine_start();
}

u32 myIrqEnable(u32 irq) {	
	int oldIME = enterCriticalSection();	
	
	#ifdef DEBUG		
	nocashMessage("myIrqEnable\n");
	#endif	
	
	u32 irq_before = REG_IE | IRQ_IPC_SYNC;		
	irq |= IRQ_IPC_SYNC;
	REG_IPC_SYNC |= IPC_SYNC_IRQ_ENABLE;

	REG_IE |= irq;
	leaveCriticalSection(oldIME);
	return irq_before;
}

void irqIPCSYNCEnable() {	
	if(!initializedIRQ) {
		int oldIME = enterCriticalSection();	
		initialize();	
		#ifdef DEBUG		
		dbg_printf("\nirqIPCSYNCEnable\n");	
		#endif	
		REG_IE |= IRQ_IPC_SYNC;
		REG_IPC_SYNC |= IPC_SYNC_IRQ_ENABLE;
		#ifdef DEBUG		
		dbg_printf("IRQ_IPC_SYNC enabled\n");
		#endif	
		leaveCriticalSection(oldIME);
		initializedIRQ = true;
	}
}

// ARM7 Redirected function

bool eepromProtect (void) {
	#ifdef DEBUG		
	dbg_printf("\narm7 eepromProtect\n");
	#endif	
	
	return true;
}

bool eepromRead (u32 src, void *dst, u32 len) {
	#ifdef DEBUG	
	dbg_printf("\narm7 eepromRead\n");	
	
	dbg_printf("\nsrc : \n");
	dbg_hexa(src);		
	dbg_printf("\ndst : \n");
	dbg_hexa((u32)dst);
	dbg_printf("\nlen : \n");
	dbg_hexa(len);
	#endif	

	if (lockMutex(&saveMutex)) {
		initialize();
		fileRead(dst,*savFile,src,len);
        unlockMutex(&saveMutex);
	}
	return true;
}

bool eepromPageWrite (u32 dst, const void *src, u32 len) {
	#ifdef DEBUG	
	dbg_printf("\narm7 eepromPageWrite\n");	
	
	dbg_printf("\nsrc : \n");
	dbg_hexa((u32)src);		
	dbg_printf("\ndst : \n");
	dbg_hexa(dst);
	dbg_printf("\nlen : \n");
	dbg_hexa(len);
	#endif	
    
    if (lockMutex(&saveMutex)) {
		initialize();
    	fileWrite(src,*savFile,dst,len);
        unlockMutex(&saveMutex);
	}
	
	return true;
}

bool eepromPageProg (u32 dst, const void *src, u32 len) {
	#ifdef DEBUG	
	dbg_printf("\narm7 eepromPageProg\n");	
	
	dbg_printf("\nsrc : \n");
	dbg_hexa((u32)src);		
	dbg_printf("\ndst : \n");
	dbg_hexa(dst);
	dbg_printf("\nlen : \n");
	dbg_hexa(len);
	#endif	

    if (lockMutex(&saveMutex)) {
		initialize();
    	fileWrite(src,*savFile,dst,len);
        unlockMutex(&saveMutex);
	}
	
	return true;
}

bool eepromPageVerify (u32 dst, const void *src, u32 len) {
	#ifdef DEBUG	
	dbg_printf("\narm7 eepromPageVerify\n");	
	
	dbg_printf("\nsrc : \n");
	dbg_hexa((u32)src);		
	dbg_printf("\ndst : \n");
	dbg_hexa(dst);
	dbg_printf("\nlen : \n");
	dbg_hexa(len);
	#endif	

	//fileWrite(src,savFile,dst,len);
	return true;
}

bool eepromPageErase (u32 dst) {
	#ifdef DEBUG	
	dbg_printf("\narm7 eepromPageErase\n");	
	#endif	
	
	return true;
}

u32 cardId (void) {
	#ifdef DEBUG	
	dbg_printf("\cardId\n");
	#endif	

	return	1;
}

bool cardRead (u32 dma,  u32 src, void *dst, u32 len) {
	#ifdef DEBUG	
	dbg_printf("\narm7 cardRead\n");	
	
	dbg_printf("\ndma : \n");
	dbg_hexa(dma);		
	dbg_printf("\nsrc : \n");
	dbg_hexa(src);		
	dbg_printf("\ndst : \n");
	dbg_hexa((u32)dst);
	dbg_printf("\nlen : \n");
	dbg_hexa(len);
	#endif	
	
	if (lockMutex(&saveMutex)) {
		initialize();
		#ifdef DEBUG	
		nocashMessage("fileRead romFile");
		#endif	
		fileRead(dst,*romFile,src,len);
	}
	
	return true;
}




