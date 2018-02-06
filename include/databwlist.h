#ifndef _DATABWLIST_H
#define _DATABWLIST_H

// ROM data include list.
// 1 = start of data address, 2 = end of data address, 3 = data size, 4 = 1 for dataWhitelist, 0 for dataBlacklist
// 5 = GAME_CACHE_ADRESS_START, 6 = GAME_CACHE_SLOTS, 7 = GAME_READ_SIZE
//u32 dataWhitelist_BXSE0[7] = {0x0113F800, 0x01D61600, 0x00C21E00, 0x00000001,	// Sonic Colors (U)
//								0x0DC40000, 0x0000001E, 0x00020000};
//u32 dataWhitelist_VPYT0[7] = {0x00072A00, 0x0014FE00, 0x000DD400, 0x00000001,	// Pokemon Conquest (U)
//								0x0D0E0000, 0x00000079, 0x00020000};

// ROM data exclude list.
// 1 = start of data address, 2 = end of data address, 3 = data size
u32 dataBlacklist_V2GE0[7] = {0x00249C00, 0x01234E00, 0x00FEB200, 0x00000000,	// Mario vs Donkey Kong: Mini-Land Mayhem (U)
								0x0DE00000, 0x00000010, 0x00020000};

#endif // _DATABWLIST_H
