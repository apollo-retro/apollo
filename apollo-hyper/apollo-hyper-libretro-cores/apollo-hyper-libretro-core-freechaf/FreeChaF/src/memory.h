#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

/*
	This file is part of FreeChaF.

	FreeChaF is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FreeChaF is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FreeChaF.  If not, see http://www.gnu.org/licenses/
*/

extern int MEMORY_RAMStart;

#define MEMORY_SIZE 0x10000
extern uint8_t Memory[MEMORY_SIZE];
// sl131253 - 0x000 - 0x3FF
// sl131254 - 0x400 - 0x7FF
// cart     - 0x800 - 0x1FFF
// vram     - 0x2000 ...

void MEMORY_reset(void);
int MEMORY_loadCartROM(const void* data, size_t size);
int MEMORY_loadSysROM_libretro(const char* path, int address);
uint8_t MEMORY_read8(uint16_t address);
uint16_t MEMORY_read16(uint16_t address);
void MEMORY_write8(uint16_t address, uint8_t val);

#define R_SIZE 64
extern uint8_t F8_R[R_SIZE]; // 64 byte Scratchpad

extern uint8_t F8_A; // Accumulator
extern uint16_t F8_PC0; // Program Counter
extern uint16_t F8_PC1; // Program Counter alternate
extern uint16_t F8_DC0; // Data Counter
extern uint16_t F8_DC1; // Data Counter alternate
extern uint8_t F8_ISAR; // Indirect Scratchpad Address Register (6-bit)
extern uint8_t F8_W; // Status Register (flags)
extern uint8_t MEMORY_Multicart;

#endif
