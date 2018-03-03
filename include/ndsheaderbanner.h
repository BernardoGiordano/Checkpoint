/*---------------------------------------------------------------------------------

	memory.h -- Declaration of memory regions


	Copyright (C) 2005 Michael Noland (joat) and Jason Rogers (dovoto)

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any
	damages arising from the use of this software.

	Permission is granted to anyone to use this software for any
	purpose, including commercial applications, and to alter it and
	redistribute it freely, subject to the following restrictions:

	1.	The origin of this software must not be misrepresented; you
		must not claim that you wrote the original software. If you use
		this software in a product, an acknowledgment in the product
		documentation would be appreciated but is not required.

	2.	Altered source versions must be plainly marked as such, and
		must not be misrepresented as being the original software.

	3.	This notice may not be removed or altered from any source
		distribution.

---------------------------------------------------------------------------------*/
/*! \file ndsheaderbanner.h
\brief Defines the Nintendo DS file header and icon/title structs.
*/

#ifndef NDS_HEADER
#define NDS_HEADER

#include "common.h"

/*!
	\brief the GBA file header format.
	See gbatek for more info.
*/
typedef struct {
	u32 entryPoint;		//!< 32 bits arm opcode to jump to executable code.
	u8 logo[156];		//!< nintendo logo needed for booting the game.
	char title[0xC];	//!< 12 characters for the game title.
	char gamecode[0x4];	//!< 4 characters for the game code. first character is usally A or B, next 2 characters is a short title
						//!< and last character is for destination/language.
	u16 makercode;		//!< identifies the (commercial) developer.
	u8 is96h;			//!< fixed value that is always 96h.
	u8 unitcode;		//!< identifies the required hardware.
	u8 devicecode;		//!< used by nintedo's hardware debuggers. normally 0.
	u8 unused[7];
	u8 version;			//!< the version of the game.
	u8 complement;		//!< complement checksum of the gba header.
	u16 checksum;		//!< a 16 bit checksum? (gbatek says its unused/reserved).
} sGBAHeader;

//#define GBA_HEADER (*(tGBAHeader *)0x08000000)

/*!
	\brief the NDS file header format
	See gbatek for more info.
*/
typedef struct {
	char gameTitle[12];			//!< 12 characters for the game title.
	char gameCode[4];			//!< 4 characters for the game code.
	char makercode[2];			//!< identifies the (commercial) developer.
	u8 unitCode;				//!< identifies the required hardware.
	u8 deviceType;				//!< type of device in the game card
	u8 deviceSize;				//!< capacity of the device (1 << n Mbit)
	u8 reserved1[9];
	u8 romversion;				//!< version of the ROM.
	u8 flags;					//!< bit 2: auto-boot flag.

	u32 arm9romOffset;			//!< offset of the arm9 binary in the nds file.
	u32 arm9executeAddress;		//!< adress that should be executed after the binary has been copied.
	u32 arm9destination;		//!< destination address to where the arm9 binary should be copied.
	u32 arm9binarySize;			//!< size of the arm9 binary.

	u32 arm7romOffset;			//!< offset of the arm7 binary in the nds file.
	u32 arm7executeAddress;		//!< adress that should be executed after the binary has been copied.
	u32 arm7destination;		//!< destination address to where the arm7 binary should be copied.
	u32 arm7binarySize;			//!< size of the arm7 binary.

	u32 filenameOffset;			//!< File Name Table (FNT) offset.
	u32 filenameSize;			//!< File Name Table (FNT) size.
	u32 fatOffset;				//!< File Allocation Table (FAT) offset.
	u32 fatSize;				//!< File Allocation Table (FAT) size.

	u32 arm9overlaySource;		//!< File arm9 overlay offset.
	u32 arm9overlaySize;		//!< File arm9 overlay size.
	u32 arm7overlaySource;		//!< File arm7 overlay offset.
	u32 arm7overlaySize;		//!< File arm7 overlay size.

	u32 cardControl13;			//!< Port 40001A4h setting for normal commands (used in modes 1 and 3)
	u32 cardControlBF;			//!< Port 40001A4h setting for KEY1 commands (used in mode 2)
	u32 bannerOffset;			//!< offset to the banner with icon and titles etc.

	u16 secureCRC16;			//!< Secure Area Checksum, CRC-16.

	u16 readTimeout;			//!< Secure Area Loading Timeout.

	u32 unknownRAM1;			//!< ARM9 Auto Load List RAM Address (?)
	u32 unknownRAM2;			//!< ARM7 Auto Load List RAM Address (?)

	u32 bfPrime1;				//!< Secure Area Disable part 1.
	u32 bfPrime2;				//!< Secure Area Disable part 2.
	u32 romSize;				//!< total size of the ROM.

	u32 headerSize;				//!< ROM header size.
	u32 zeros88[14];
	u8 gbaLogo[156];			//!< Nintendo logo needed for booting the game.
	u16 logoCRC16;				//!< Nintendo Logo Checksum, CRC-16.
	u16 headerCRC16;			//!< header checksum, CRC-16.

	u32 debugRomSource;			//!< debug ROM offset.
	u32 debugRomSize;			//!< debug size.
	u32 debugRomDestination;	//!< debug RAM destination.
	u32 offset_0x16C;			//reserved?

	u8 zero[0x90];

	// 0x200
	// TODO: More DSi-specific fields.
	u8 dsi1[0x30];
	u32 dsi_tid;
	u8 dsi2[0x180];
} sNDSHeader;


//#define __NDSHeader ((tNDSHeader *)0x02FFFE00)

// Make sure the banner size is correct.
static_assert(sizeof(sNDSHeader) == 0x3B4, "sizeof(sNDSHeader) is not 0x3B4 bytes");


/*!
	\brief the NDS banner format.
	See gbatek for more information.
*/
typedef struct {
	u16 version;		//!< version of the banner.
	u16 crc[4];		//!< CRC-16s of the banner.
	u8 reserved[22];
	u8 icon[512];		//!< 32*32 icon of the game with 4 bit per pixel.
	u16 palette[16];	//!< the palette of the icon.
	u16 titles[8][128];	//!< title of the game in 8 different languages.

	// [0xA40] Reserved space, possibly for other titles.
	u8 reserved2[0x800];

	// DSi-specific.
	u8 dsi_icon[8][512];	//!< DSi animated icon frame data.
	u16 dsi_palette[8][16];	//!< Palette for each DSi icon frame.
	u16 dsi_seq[64];	//!< DSi animated icon sequence.
} sNDSBanner;

// sNDSBanner version.
typedef enum {
	NDS_BANNER_VER_ORIGINAL	= 0x0001,
	NDS_BANNER_VER_ZH	= 0x0002,
	NDS_BANNER_VER_ZH_KO	= 0x0003,
	NDS_BANNER_VER_DSi	= 0x0103,
} sNDSBannerVersion;

// sNDSBanner sizes.
typedef enum {
	NDS_BANNER_SIZE_ORIGINAL	= 0x0840,
	NDS_BANNER_SIZE_ZH		= 0x0940,
	NDS_BANNER_SIZE_ZH_KO		= 0x0A40,
	NDS_BANNER_SIZE_DSi		= 0x23C0,
} sNDSBannerSize;

// Make sure the banner size is correct.
static_assert(sizeof(sNDSBanner) == NDS_BANNER_SIZE_DSi, "sizeof(sNDSBanner) is not 0x23C0 bytes");

// Language indexes.
typedef enum {
	// DS and 3DS
	NDS_LANG_JAPANESE	= 0,
	NDS_LANG_ENGLISH	= 1,
	NDS_LANG_FRENCH		= 2,
	NDS_LANG_GERMAN		= 3,
	NDS_LANG_ITALIAN	= 4,
	NDS_LANG_SPANISH	= 5,
	NDS_LANG_CHINESE	= 6,
	NDS_LANG_KOREAN		= 7,

	// 3DS only
	N3DS_LANG_CHINESE_SIMPLIFIED	= 6,
	N3DS_LANG_DUTCH			= 8,
	N3DS_LANG_PORTUGUESE		= 9,
	N3DS_LANG_RUSSIAN		= 10,
	N3DS_LANG_CHINESE_TRADITIONAL	= 11,
} sNDSLanguage;

/**
 * Get the icon from an NDS banner.
 * @param binFile NDS banner.
 * @return Icon texture. (NULL on error)
 */
void* grabIcon(const sNDSBanner* ndsBanner);

#endif // NDS_HEADER
