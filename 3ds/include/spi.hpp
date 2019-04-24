/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/

/*
 *  This file is part of TWLSaveTool.
 *  Copyright (C) 2015-2016 TuxSH
 *
 *  TWLSaveTool is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef SPI_HPP
#define SPI_HPP

#include <3ds.h>
#include <stdio.h>
#include <string.h>

extern "C" {

#define SPI_512B_EEPROM_CMD_WRLO 2
#define SPI_512B_EEPROM_CMD_WRHI 10
#define SPI_512B_EEPROM_CMD_RDLO 3
#define SPI_512B_EEPROM_CMD_RDHI 11

#define SPI_EEPROM_CMD_WRITE 2 

#define SPI_CMD_PP 2
#define SPI_CMD_READ 3
#define SPI_CMD_RDSR 5
#define SPI_CMD_WREN 6

#define SPI_FLASH_CMD_PW 10
#define SPI_FLASH_CMD_RDID 0x9f
#define SPI_FLASH_CMD_SE 0xd8

#define SPI_FLG_WIP 1
#define SPI_FLG_WEL 2

extern u8* fill_buf; 

typedef enum {
    NO_CHIP = -1,
    
    EEPROM_512B = 0,
    
    EEPROM_8KB = 1,
    EEPROM_64KB = 2,
    EEPROM_128KB = 3,
    EEPROM_STD_DUMMY = 1,
    
    FLASH_256KB_1 = 4,
    FLASH_256KB_2 = 5,
    FLASH_512KB_1 = 6,
    FLASH_512KB_2 = 7,
    FLASH_1MB = 8,
    FLASH_8MB = 9, // <- can't restore savegames, and maybe not read them atm
    FLASH_STD_DUMMY = 4,
    
    FLASH_512KB_INFRARED = 10,
    FLASH_256KB_INFRARED = 11, // AFAIK, only "Active Health with Carol Vorderman" has such a flash save memory
    FLASH_INFRARED_DUMMY = 9,
    
    CHIP_LAST = 11,
} CardType;

Result SPIWriteRead(CardType type, void* cmd, u32 cmdSize, void* answer, u32 answerSize, void* data, u32 dataSize);
Result SPIWaitWriteEnd(CardType type);
Result SPIEnableWriting(CardType type);
Result SPIReadJEDECIDAndStatusReg(CardType type, u32* id, u8* statusReg);
Result SPIGetCardType(CardType* type, int infrared);
u32 SPIGetPageSize(CardType type);
u32 SPIGetCapacity(CardType type);
Result SPIWriteSaveData(CardType type, u32 offset, void* data, u32 size);
Result SPIReadSaveData(CardType type, u32 offset, void* data, u32 size);
Result SPIEraseSector(CardType type, u32 offset);

} // extern "C"

#endif