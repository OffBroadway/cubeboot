#ifndef _DVD_H_
#define _DVD_H_

#include <gctypes.h>
#include <ogc/dvd.h>

#define T_FILE 0
#define T_DIR 1

// BNR Magic
#define BANNER_MAGIC_1 0x424E5231
#define BANNER_MAGIC_2 0x424E5232

// Taken from Swiss-GC
// The Retail Game DVD Disk Header
// Info source: YAGCD
// Size of Struct: 0x2440 (effective size is 0x440)
typedef struct {
	u8  ConsoleID;		//G = Gamecube, R = Wii.
	u8  GamecodeA;		//2 Ascii letters to indicate the GameID.
	u8  GamecodeB;		//2 Ascii letters to indicate the GameID.
	u8	CountryCode;	//J=JAP . P=PAL . E=USA . D=OoT MasterQuest
	u8	MakerCodeA;		//Eg 08 = Sega etc.
	u8  MakerCodeB;
	u8	DiscID;
	u8	Version;
	u8	AudioStreaming; //01 = Enable it. 00 = Don't
	u8 	StreamBufSize;	//For the AudioEnable. (always 00?)
	u8	unused_1[18];	
	u32 DVDMagicWord;	//0xC2339F3D
	char GameName[64];	//String Name of Game, rarely > 32 chars
	u8  unused_2[416];
	u32 NKitMagicWord;
	u32 NKitVersion;
	u32 ImageCRC;
	u32 ForceCRC;
	u32 ImageSize;
	u32 JunkID;
	u8  unused_3[488];
	u32 ApploaderSize;
	u32 ApploaderFunc1;
	u32 ApploaderFunc2;
	u32 ApploaderFunc3;
	u8  unused_4[16];
	// additional info
	u32 DOLOffset;		//offset of main executable DOL (bootfile)
	u32 FSTOffset;		//offset of the FST ("fst.bin")
	u32	FSTSize;		//size of FST
	u32	MaxFSTSize;		//maximum size of FST (usually same as FSTSize)*
	u32 FSTAddress;
	u32 UserPos;		//user position(?)
	u32 UserLength;		//user length(?)
	u32 unused_5;
	// === NOTE: start bi2
	// u32 DebugMonSize;
	// u32 SimMemSize;
	// u32 ArgOffset;
	// u32 DebugFlag;
	// u32 TRKLocation;
	// u32 TRKSize;
	// u32 RegionCode;
	// u32 TotalDisc;
	// u32 LongFileName;
	// u32 PADSpec;
	// u32 DOLLimit;
	// u8  unused_6[8148];
} DiskHeader __attribute__((aligned(32)));

typedef struct {
	u32 DebugMonSize;
	u32 SimMemSize;
	u32 ArgOffset;
	u32 DebugFlag;
	u32 TRKLocation;
	u32 TRKSize;
	u32 RegionCode;
	u32 TotalDisc;
	u32 LongFileName;
	u32 PADSpec;
	u32 DOLLimit;
	// u8  unused_6[8148];
} DiskHeaderInformation __attribute__((aligned(32)));

typedef struct {
	u32 DOLOffset;		//offset of main executable DOL (bootfile)
	u32 FSTOffset;		//offset of the FST ("fst.bin")
	u32	FSTSize;		//size of FST
	u32	MaxFSTSize;		//maximum size of FST (usually same as FSTSize)*
	u32 FSTAddress;
	u32 UserPos;		//user position(?)
	u32 UserLength;		//user length(?)
	u32 unused_5;
} DiskInfo __attribute__((aligned(32)));

typedef struct _FSTEntry {
    u8 filetype : 1; //00 - Flags (0: File, 1: Directory)
    u8 offset[3]; //01-03 - Pointer to name in String Table
    u32 addr;
    u32 len;
} FSTEntry;

// void DVDInit(void);
// DiskHeader *__DVDFSInit(void);

_Static_assert(sizeof(bool) == 1); // just make sure this is a single byte

#define BANNER_SINGLE_LANG 0
#define BANNER_MULTI_LANG 1

typedef struct {
	bool valid;
	u8 game_id[6];
	u8 disc_num;
	u8 disc_ver;
	u8 bnr_type;
	u32 bnr_offset;
	u32 dol_offset;
	u32 fst_offset;
	u32 fst_size;
	u32 max_fst_size;
} dolphin_game_into_t;

_Static_assert(sizeof(dolphin_game_into_t) == 32);

dolphin_game_into_t get_game_info_with_open_game(u8 fd);
dolphin_game_into_t get_game_info(char *game_path);

#endif
