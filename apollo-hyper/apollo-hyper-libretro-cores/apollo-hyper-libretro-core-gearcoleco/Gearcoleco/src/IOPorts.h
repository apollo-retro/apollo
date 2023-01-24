/*
 * Gearcoleco - ColecoVision Emulator
 * Copyright (C) 2021  Ignacio Sanchez

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#ifndef IOPORTS_H
#define	IOPORTS_H

#include "definitions.h"

class IOPorts
{
public:
    IOPorts() { };
    virtual ~IOPorts() { };
    virtual void Reset() = 0;
    virtual u8 In(u8 port) = 0;
    virtual void Out(u8 port, u8 value) = 0;
};

#endif	/* IOPORTS_H */
