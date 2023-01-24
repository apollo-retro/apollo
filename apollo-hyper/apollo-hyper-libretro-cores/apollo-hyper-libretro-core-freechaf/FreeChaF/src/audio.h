#ifndef AUDIO_H
#define AUDIO_H
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

#include <stdint.h>

extern int16_t AUDIO_Buffer[735 * 2 * 2];
extern uint8_t AUDIO_tone;
extern int16_t AUDIO_amp;
extern unsigned int AUDIO_sampleInCycle;
extern unsigned int AUDIO_ticks;

void AUDIO_tick(int ticks);

void AUDIO_frame(void);

void AUDIO_reset(void);

void AUDIO_portReceive(uint8_t port, uint8_t val);

#endif
