/*-----------------------------------------------------------------

 Copyright (C) 2010  Dave "WinterMute" Murphy

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

#include <nds.h>
#include <fat.h>
#include <limits.h>

#include <stdio.h>
#include <stdarg.h>

#include <nds/fifocommon.h>

#include "nds_loader_arm9.h"
#include "inifile.h"

int backlightMode = 0;

using namespace std;

static bool debug = false;

static u32 cheatData [256];

static inline int dbg_printf( const char* format, ... )
{
	if(!debug) return 0;

	static FILE * debugFile;
	debugFile = fopen ("sd:/NDSBTSRP.LOG","a");

	va_list args;
    va_start( args, format );
    int ret = vprintf( format, args );
	ret = vfprintf(debugFile, format, args );
	va_end(args);

	fclose (debugFile);

    return ret;
}

//---------------------------------------------------------------------------------
void stop (void) {
//---------------------------------------------------------------------------------
	while (1) {
		swiWaitForVBlank();
	}
}

//---------------------------------------------------------------------------------
void dopause() {
//---------------------------------------------------------------------------------
	iprintf("Press start...\n");
	while(1) {
		scanKeys();
		if(keysDown() & KEY_START)
			break;
		swiWaitForVBlank();
	}
	scanKeys();
}

void runFile(string filename, string savPath, u32 saveSize, u32 language, u32 dsiMode, u32 patchMpuRegion, u32 patchMpuSize, u32 consoleModel, u32 loadingScreen, u32 romread_LED, u32 gameSoftReset, u32 asyncPrefetch, u32* cheat_data) {
	vector<char*> argarray;

	if(debug)
		for (int i=0; i<60; i++) swiWaitForVBlank();

	if ( strcasecmp (filename.c_str() + filename.size() - 5, ".argv") == 0) {
		FILE *argfile = fopen(filename.c_str(),"rb");
		char str[PATH_MAX], *pstr;
		const char seps[]= "\n\r\t ";

		while( fgets(str, PATH_MAX, argfile) ) {
			// Find comment and end string there
			if( (pstr = strchr(str, '#')) )
				*pstr= '\0';

			// Tokenize arguments
			pstr= strtok(str, seps);

			while( pstr != NULL ) {
				argarray.push_back(strdup(pstr));
				pstr= strtok(NULL, seps);
			}
		}
		fclose(argfile);
		filename = argarray.at(0);
	} else {
		argarray.push_back(strdup(filename.c_str()));
	}

	if ( strcasecmp (filename.c_str() + filename.size() - 4, ".nds") != 0 || argarray.size() == 0 ) {
		dbg_printf("no nds file specified\n");
	} else {
		dbg_printf("Running %s with %d parameters\n", argarray[0], argarray.size());
		switch(backlightMode) {
			case 0:
			default:
				powerOn(PM_BACKLIGHT_TOP);
				//powerOn(PM_BACKLIGHT_BOTTOM);
				break;
			case 1:
				powerOn(PM_BACKLIGHT_TOP);
				powerOff(PM_BACKLIGHT_BOTTOM);
				break;
			case 2:
				powerOff(PM_BACKLIGHT_TOP);
				powerOn(PM_BACKLIGHT_BOTTOM);
				break;
			case 3:
				powerOff(PM_BACKLIGHT_TOP);
				powerOff(PM_BACKLIGHT_BOTTOM);
				break;
		}
		int err = runNdsFile (argarray[0],
							strdup(savPath.c_str()),
							saveSize,
							language,
							dsiMode,
							patchMpuRegion,
							patchMpuSize,
							consoleModel,
							loadingScreen,
							romread_LED,
							gameSoftReset,
							asyncPrefetch,
							argarray.size(), (const char **)&argarray[0],
                            cheat_data);
		powerOff(PM_BACKLIGHT_TOP);
		dbg_printf("Start failed. Error %i\n", err);

	}
}

typedef struct {
	char gameTitle[12];			//!< 12 characters for the game title.
	char gameCode[4];			//!< 4 characters for the game code.
} sNDSHeadertitlecodeonly;

void getSFCG_ARM9() {
	iprintf( "SCFG_ROM ARM9 %x\n", REG_SCFG_ROM ); 
	iprintf( "SCFG_CLK ARM9 %x\n", REG_SCFG_CLK ); 
	//iprintf( "SCFG_EXT ARM9 %x\n", REG_SCFG_EXT ); 
}

void getSFCG_ARM7() {

	//iprintf( "SCFG_ROM ARM7\n" );

	//nocashMessage("fifoSendValue32(FIFO_USER_01,MSG_SCFG_ROM);\n");
	//fifoSendValue32(FIFO_USER_01,(long unsigned int)&REG_SCFG_ROM);

	//nocashMessage("dbg_printf\n");

	iprintf( "SCFG_CLK ARM7\n" );

	nocashMessage("fifoSendValue32(FIFO_USER_01,MSG_SCFG_CLK);\n");
	fifoSendValue32(FIFO_USER_01,(long unsigned int)&REG_SCFG_CLK);

	iprintf( "SCFG_EXT ARM7\n" );

	nocashMessage("fifoSendValue32(FIFO_USER_01,MSG_SCFG_EXT);\n");
	fifoSendValue32(FIFO_USER_01,(long unsigned int)&REG_SCFG_EXT);

}

void myFIFOValue32Handler(u32 value,void* data)
{
	nocashMessage("myFIFOValue32Handler\n");
	iprintf( "ARM7 data %x\n", value );
}

off_t getSaveSize(const char* path) {
    FILE* fp = fopen(path, "rb");
    off_t fsize = 0;
    if (fp) {
        fseek(fp, 0, SEEK_END);
        fsize = ftell(fp);
        if (!fsize) fsize = 0;
        fclose(fp);
    }
    return fsize;
}

int main( int argc, char **argv) {

	if (fatInitDefault()) {
		nocashMessage("fatInitDefault");

		if (access("fat:/", F_OK) == 0) {
			consoleDemoInit();
			printf("This edition of nds-bootstrap\n");
			printf("can only be used on the\n");
			printf("SD card.\n");
			stop();
		}

		CIniFile bootstrapini( "sd:/_nds/nds-bootstrap.ini" );

		if(bootstrapini.GetInt("NDS-BOOTSTRAP","DEBUG",0) == 1) {
			debug=true;

			powerOff(PM_BACKLIGHT_TOP);
			consoleDemoInit();

			dbg_printf("nds-bootstrap SDK5\n");

			fifoSetValue32Handler(FIFO_USER_02,myFIFOValue32Handler,0);

			getSFCG_ARM9();
			getSFCG_ARM7();
		}

		int romread_LED = bootstrapini.GetInt("NDS-BOOTSTRAP","ROMREAD_LED",1);
		switch(romread_LED) {
			case 0:
			default:
				break;
			case 1:
				dbg_printf("Using WiFi LED\n");
				break;
			case 2:
				dbg_printf("Using Power LED\n");
				break;
			case 3:
				dbg_printf("Using Camera LED\n");
				break;
		}

		std::string	ndsPath = bootstrapini.GetString( "NDS-BOOTSTRAP", "NDS_PATH", "");

		/*FILE *f_nds_file = fopen(ndsPath.c_str(), "rb");

		char game_TID[5];
		fseek(f_nds_file, offsetof(sNDSHeadertitlecodeonly, gameCode), SEEK_SET);
		fread(game_TID, 1, 4, f_nds_file);
		game_TID[4] = 0;
		game_TID[3] = 0;
		game_TID[2] = 0;
		game_TID[1] = 0;
		fclose(f_nds_file);
		
		if (strcmp(game_TID, "I") != 0) {
			fifoSendValue32(FIFO_USER_08, 1);	// Disable Slot-1 access for games with no built-in Infrared port
		}*/

		if(bootstrapini.GetInt("NDS-BOOTSTRAP","BOOST_CPU",0) == 1) {
			dbg_printf("CPU boosted\n");
			// libnds sets TWL clock speeds on arm7/arm9 scfg_clk at boot now. No changes needed.
		} else {
			REG_SCFG_CLK = 0x80;
			fifoSendValue32(FIFO_USER_06, 1);
		}

		fifoSendValue32(FIFO_USER_03, 1);
		fifoWaitValue32(FIFO_USER_05);

		if(bootstrapini.GetInt("NDS-BOOTSTRAP","LOGGING",0) == 1) {
			static FILE * debugFile;
			debugFile = fopen ("sd:/NDSBTSRP.LOG","w");
			fprintf(debugFile, "DEBUG MODE\n");
			fclose (debugFile);

			// create a big file (minimal sdengine libfat cannot append to a file)
			debugFile = fopen ("sd:/NDSBTSRP.LOG","a");
			for (int i=0; i<1000; i++) {
				fprintf(debugFile, "                                                                                                                                          \n");
			}
			fclose (debugFile);
		} else {
			remove ("sd:/NDSBTSRP.LOG");
		}

		std::string	savPath = bootstrapini.GetString( "NDS-BOOTSTRAP", "SAV_PATH", "");

		u32	patchMpuRegion = bootstrapini.GetInt( "NDS-BOOTSTRAP", "PATCH_MPU_REGION", 0);

		u32	patchMpuSize = bootstrapini.GetInt( "NDS-BOOTSTRAP", "PATCH_MPU_SIZE", 0);

		cheatData[0] = 0xCF000000;
        std::vector< std::string >  cheats;      
        bootstrapini.GetStringVector("NDS-BOOTSTRAP","CHEAT_DATA",cheats, ' ');
        if(cheats.size() > 0) {
            dbg_printf("Cheat data present\n");
            
            if(cheats.size() < 256) {
                 for (int i=0; i<cheats.size(); i++) {
                    dbg_printf(cheats[i].c_str());
                    dbg_printf(" ");
                    cheatData[i] = strtol(("0x"+cheats[i]).c_str(),NULL,16); 
                }
                cheatData[cheats.size()] = 0xCF000000;
            } else {
				printf("1024 bytes CHEAT_DATA size limit reached, the cheats are ignored!\n");
			}
		}

		backlightMode = bootstrapini.GetInt( "NDS-BOOTSTRAP", "BACKLIGHT_MODE", 0);

		dbg_printf("Running %s\n", ndsPath.c_str());
		runFile(ndsPath.c_str(),
				savPath.c_str(),
				getSaveSize(savPath.c_str()),
				bootstrapini.GetInt( "NDS-BOOTSTRAP", "LANGUAGE", -1),
				bootstrapini.GetInt( "NDS-BOOTSTRAP", "DSI_MODE", 0),
				patchMpuRegion,
				patchMpuSize,
				bootstrapini.GetInt( "NDS-BOOTSTRAP", "CONSOLE_MODEL", 0),
				bootstrapini.GetInt( "NDS-BOOTSTRAP", "LOADING_SCREEN", 1),
				romread_LED,
				bootstrapini.GetInt( "NDS-BOOTSTRAP", "GAME_SOFT_RESET", 0),
				bootstrapini.GetInt( "NDS-BOOTSTRAP", "ASYNC_PREFETCH", 0),
				(u32*)cheatData);	
	} else {
		consoleDemoInit();
		printf("SD init failed!\n");
	}

	stop();
}

