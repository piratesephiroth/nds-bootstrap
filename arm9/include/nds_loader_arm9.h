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

#ifndef NDS_LOADER_ARM9_H
#define NDS_LOADER_ARM9_H

#ifdef __cplusplus
extern "C" {
#endif

#define LOAD_DEFAULT_NDS 0

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
	bool logging,
	bool initDisc,
	bool dldiPatchNds,
	int argc, const char** argv,
	u32* cheat_data, u32 cheat_data_len
);
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
	bool logging,
	int argc, const char** argv,
	u32* cheat_data, u32 cheat_data_len
);

#ifdef __cplusplus
}
#endif

#endif // NDS_LOADER_ARM9_H
