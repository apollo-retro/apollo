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

// http://www.nyx.net/~lturner/public_html/F8_ins.html
// http://www.nyx.net/~lturner/public_html/Fairchild_F8.html
// http://channelf.se/veswiki/index.php?title=Main_Page
// http://seanriddle.com/chanfinfo.html

#include <string.h>

#include "f8.h"
#include "memory.h"
#include "ports.h"

uint8_t F8_R[64]; // 64 byte Scratchpad

uint8_t F8_A   = 0; // Accumulator
uint16_t F8_PC0 = 0; // Program Counter
uint16_t F8_PC1 = 0; // Program Counter alternate
uint16_t F8_DC0 = 0; // Data Counter
uint16_t F8_DC1 = 0; // Data Counter alternate
uint8_t F8_ISAR = 0; // Indirect Scratchpad Address Register (6-bit)
uint8_t F8_W   = 0; // Status Register (flags)

int (*OpCodes[0x100])(uint8_t);

// Flags
enum
  {
   flag_Sign = 0,
   flag_Zero = 2,
   flag_Overflow = 3,
   flag_Carry = 1,
   flag_Interupt = 4
  };

/* *****************************
   *
   *  Helper functions
   *
   ***************************** */


// Read 1-byte instruction operand
uint8_t readOperand8(void)
{
	return MEMORY_read8(F8_PC0++);
}
// Read 2-byte instruction operand
uint16_t readOperand16(void)
{
	uint16_t val = MEMORY_read16(F8_PC0);
	F8_PC0+=2;
	return val;
}

// Set specific flag
void setFlag(int flag, int val)
{
	F8_W = F8_W | 1<<flag;
	F8_W = F8_W ^ 1<<flag;
	F8_W = F8_W | (val>0)<<flag;
	F8_W ^= (flag==0); // Compliment sign flag (1-positive, 0-negative)
}

// Set zero and sign flags based on val, clear overflow and carry 
void setFlags_0z0s(uint8_t val) // O Z C S
{
	setFlag(flag_Overflow, 0);
	setFlag(flag_Zero, (val==0));
	setFlag(flag_Carry, 0);
	setFlag(flag_Sign, (val & 0x80)>0);
}

// Clear all flags
void clearFlags_ozcs(void)
{
	setFlag(flag_Overflow, 0);
	setFlag(flag_Sign, 0);
	setFlag(flag_Carry, 0);
	setFlag(flag_Zero, 1);
}

// Increment Indirect Scratchpad Address Register
// affects just lower three bits, which roll-over 
void incISAR(void)
{
	F8_ISAR = (F8_ISAR&0x38) | ((F8_ISAR+1)&0x7);
}

// Decrement Indirect Scratchpad Address Register
// affects just lower three bits, which roll-over
void decISAR(void)
{
	F8_ISAR = (F8_ISAR&0x38) | ((F8_ISAR-1)&0x7);
}

// Read Scratchpad Byte
uint8_t Read8(uint8_t reg)
{
	return F8_R[reg & 0x3F];
}
// Read 16-bit int from Scratchpad
uint16_t Read16(uint8_t reg)
{
	return (F8_R[reg&0x3F]<<8) | F8_R[(reg+1)&0x3F];
}
// Write 16-bit int to Scratchpad
void Store16(uint8_t reg, uint16_t val)
{
	F8_R[reg&0x3F] = val>>8;
	F8_R[(reg+1)&0x3F] = val;
}

// Add two 8-bit signed ints
uint8_t Add8(uint8_t a, uint8_t b)
{
	uint8_t signa = a & 0x80;
	uint8_t signb = b & 0x80;
	uint16_t result = (uint16_t) a + (uint16_t) b;
	uint8_t signr = result & 0x80;

	setFlag(flag_Sign, (result & 0x80)>0);
	setFlag(flag_Zero, ((result & 0xFF)==0));
	setFlag(flag_Overflow, (signa==signb && signa!=signr)); 
	setFlag(flag_Carry, (result & 0x100)>0);
	return result;
}

// Add two 8-bit BCD numbers
uint8_t AddBCD(uint8_t a, uint8_t b)
{
	// Method from 6-3 of the Fairchild F8 Guide to Programming
	// assume a = (a + 0x66) & 0xFF
	uint16_t sum = (uint16_t) a + (uint16_t) b;

	int ci = ((a&0xF)+(b&0xF))>0xF; // carry intermediate
	int cu = sum>=0x100; // carry upper

	Add8(a, b);

	if(ci==0) { sum = (sum&0xF0) | ((sum+0xA)&0xF); }
	if(cu==0) { sum = (sum+0xA0); }

	return sum;
}

// Subtract two 8-bit signed ints
uint8_t Sub8(uint8_t a, uint8_t b)
{
	b = ((b ^ 0xFF) + 1);
	return Add8(a, b); 
}

// Bitwise And, sets flags
uint8_t And8(uint8_t a, uint8_t b)
{
	a = a & b;
	setFlags_0z0s(a);
	return a;
}
// Bitwise Or, sets flags
uint8_t Or8(uint8_t a, uint8_t b)
{
	a = a | b;
	setFlags_0z0s(a);
	return a;
}
// Bitwise Xor, sets flags
uint8_t Xor8(uint8_t a, uint8_t b)
{
	a = (a ^ b);
	setFlags_0z0s(a);
	return a;
}
// Logical Shift Right, sets flags
uint8_t ShiftRight(uint8_t val, int dist)
{
	val = (val >> dist);
	setFlags_0z0s(val);
	return val;
}
// Logical Shift Left, sets flags
uint8_t ShiftLeft(uint8_t val, int dist)
{
	val = (val << dist);
	setFlags_0z0s(val);
	return val;
}
// Computes relative branch offset
int calcBranch(uint8_t n)
{
	if((n&0x80)==0) { return(n-1); } // forward
	return -(((n-1)^0xFF)+1); // backward
	
}

/* *****************************
   *
   *  Opcode functions
   *
   ***************************** */

int LR_A_Ku(uint8_t v) // 00 LR A, Ku : A <- R12
{
	F8_A = F8_R[12];
	return 2;
}

int LR_A_Kl(uint8_t v) // 01 LR A, Kl : A <- R13
{
	F8_A = F8_R[13];
	return 2;
}

int LR_A_Qu(uint8_t v) // 02 LR A, Qu : A <- R14
{
	F8_A = F8_R[14];
	return 2;
}

int LR_A_Ql(uint8_t v) // 03 LR A, Ql : A <- R15 
{
	F8_A = F8_R[15];
	return 2;
} 

int LR_Ku_A(uint8_t v) // 04 LR Ku, A : R12 <- A 
{
	F8_R[12] = F8_A;
	return 2;
}

int LR_Kl_A(uint8_t v) // 05 LR Kl, A : R13 <- A 
{
	F8_R[13] = F8_A;
	return 2;
} 

int LR_Qu_A(uint8_t v) // 06 LR Qu, A : R14 <- A
{
	F8_R[14] = F8_A;
	return 2;
} 

int LR_Ql_A(uint8_t v) // 07 LR Ql, A : R15 <- A 
{
	F8_R[15] = F8_A;
	return 2;
} 

int LR_K_P(uint8_t v) // 08 LR K, P  : R12 <- PC1U, R13 <- PC1L
{
	Store16(12, F8_PC1);
	return 8;
}

int LR_P_K(uint8_t v)  // 09 LR P, K  : PC1U <- R12, PC1L <- R13
{
	F8_PC1 = Read16(12);
	return 8;
} 

int LR_A_IS(uint8_t v) // 0A LR A, IS : A <- ISAR 
{
	F8_A = F8_ISAR;
	return 2;
}

int LR_IS_A(uint8_t v) // 0B LR IS, A : ISAR <- A
{
	F8_ISAR = F8_A & 0x3F;
	return 2;
}

int PK(uint8_t v) // 0C PK PC1 <- PC0, PC0U <- R12, PC0L <- R13
{
	F8_PC1 = F8_PC0;
	F8_PC0 = Read16(12);
	return 5;
}

int LR_P0_Q(uint8_t v) // 0D LR P0, Q : PC0L <- R15, PC0U <- R14
{
	F8_PC0 = Read16(14);
	return 8;
}

int LR_Q_DC(uint8_t v) // 0E LR Q, DC : R14 <- DC0U, R15 <- DC0L 
{
	Store16(14, F8_DC0);
	return 8;
}

int LR_DC_Q(uint8_t v) // 0F LR DC, Q : DC0U <- R14, DC0L <- R15
{
	F8_DC0 = Read16(14);
	return 8;
}

int LR_DC_H(uint8_t v) // 10 LR DC, H : DC0U <- R10, DC0L <- R11 
{
	F8_DC0 = Read16(10);
	return 8; 
}

int LR_H_DC(uint8_t v) // 11 LR H, DC : R10 <- DC0U, R11 <- DC0L
{
	Store16(10, F8_DC0);
	return 8;
} 

int SR_1(uint8_t v) // 12 SR 1 : A >> 1
{
	F8_A = ShiftRight(F8_A, 1);
	return 2;
} 

int SL_1(uint8_t v) // 13 SL 1 : A << 1
{
	F8_A = ShiftLeft(F8_A, 1);
	return 2;
} 

int SR_4(uint8_t v) // 14 SR 4 : A >> 4
{
	F8_A = ShiftRight(F8_A, 4);
	return 2;
}

int SL_4(uint8_t v) // 15 SL 4 : A << 4
{ 
	F8_A = ShiftLeft(F8_A, 4);
	return 2;
} 

int LM(uint8_t v) // 16 LM A <- (DC0), DC0 <- DC0 + 1
{
	F8_A = MEMORY_read8(F8_DC0++);
	return 5;
} 

int ST(uint8_t v) // 17 ST (DC0) <- A, DC0 <- DC0 + 1 
{
	MEMORY_write8(F8_DC0++, F8_A);
	return 5;
}        

int COM(uint8_t v) // 18 COM A : A <- A XOR 0xFF                
{
	F8_A = F8_A ^ 0xFF;
	setFlags_0z0s(F8_A);
	return 2;
}

int LNK(uint8_t v) // 19 LNK : A <- A + C                       
{
	F8_A = Add8(F8_A, (F8_W>>flag_Carry)&1);
	return 2;
}

int DI(uint8_t v) // 1A DI : Disable Interupts                 
{
	setFlag(flag_Interupt, 0);
	return 2; 
}

int EI(uint8_t v) // 1B EI : Enable Interupts                  
{
	setFlag(flag_Interupt, 1);
	return 2;
}

int POP(uint8_t v) // 1C POP : PC0 <- PC1                       
{
	F8_PC0 = F8_PC1;
	return 4;
}

int LR_W_J(uint8_t v) // 1D LR W, J : W <- R9                      
{ 
	F8_W = F8_R[9];
	return 2;
}

int LR_J_W(uint8_t v) // 1E LR J, W : R9 <- W                      
{
	F8_R[9] = F8_W;
	return 4;
}

int INC(uint8_t v) // 1F INC : A <- A + 1                       
{
	F8_A = Add8(F8_A, 1);
	return 2;
} 

int LI_n(uint8_t v) // 20 LI n : A <- n                          
{
	F8_A = readOperand8();
	return 5; 
} 

int NI_n(uint8_t v) // 21 NI n : A <- A AND n                    
{
	F8_A = And8(F8_A, readOperand8());
	return 5;
} 

int OI_n(uint8_t v) // 22 OI n : A <- A OR n                     
{
	F8_A = Or8(F8_A, readOperand8());
	return 5;
} 

int XI_n(uint8_t v)   // 23 XI n : A <- A XOR n                    
{
	F8_A = Xor8(F8_A, readOperand8());
	return 5;
} 

int AI_n(uint8_t v)   // 24 AI n : A <- A + n                      
{
	F8_A = Add8(F8_A, readOperand8());
	return 5;
}

int CI_n(uint8_t v)   // 25 CI n : n+!(A)+1 (n-A), Only set status 
{
	Sub8(readOperand8(), F8_A);
	return 5;
} 

int IN_n(uint8_t v) // 26 IN n : Data Bus <- Port n, A <- Port n
{ 
	F8_A = PORTS_read(readOperand8());
	setFlags_0z0s(F8_A);
	return 8;
} 

int OUT_n(uint8_t v) // 27 OUT n : Data Bus <- Port n, Port n <- A
{
	PORTS_notify(readOperand8(), F8_A);
	return 8;
} 

int PI_mn(uint8_t v) // 28 PI mn : A <- m, PC1 <- PC0+1, PC0L <- n, PC0U <- A 
{ 
	F8_A = readOperand8();   // A <- m
	F8_PC1 = F8_PC0+1;          // PC1 <- PC0+1
	F8_PC0 = readOperand8(); // PC0L <- n
	F8_PC0 = F8_PC0 | (F8_A<<8);   // PC0U <- A
	return 13;
} 

int JMP_mn(uint8_t v) // 29 JMP mn : A <- m, PC0L <- n, PC0U <- A 
{
	F8_A = readOperand8(); // A <- m
	F8_PC0=readOperand8(); // PC0L <- n
	F8_PC0 |= (F8_A<<8);      // PC0U <- A
	return 11;
}

int DCI_mn(uint8_t v) // 2A DCI mn : DC0U <- m, PC0++, DC0L <- n, PC0++ 
{
	F8_DC0=readOperand16();
	return 12;
} 

int NOP(uint8_t v) // 2B NOP
{
	return 2;
} 

int XDC(uint8_t v) // 2C DC0,DC1 <- DC1,DC0 
{
	F8_DC0^=F8_DC1;
	F8_DC1^=F8_DC0;
	F8_DC0^=F8_DC1;
	return 4;
}

int DS_r(uint8_t v) // 3x DS r <- (r)+0xFF, [decrease scratchpad byte]
{
	F8_R[v&0xF] = Sub8(F8_R[v&0xF], 1);
	return 3;
} 
int DS_r_S(uint8_t v) // DS r Indirect
{
	F8_R[F8_ISAR] = Sub8(F8_R[F8_ISAR], 1);
	return 3;
}
int DS_r_I(uint8_t v) // DS r Increment
{
	F8_R[F8_ISAR] = Sub8(F8_R[F8_ISAR], 1);
	incISAR();
	return 3;
}
int DS_r_D(uint8_t v) // DS r Decrement
{
	F8_R[F8_ISAR] = Sub8(F8_R[F8_ISAR], 1);
	decISAR();
	return 3;
}

int LR_A_r(uint8_t v) // 4x LR A, r : A <- r 
{
	F8_A = F8_R[v&0xF];
	return 2;
} 
int LR_A_r_S(uint8_t v) // LR A, r Indirect
{
	F8_A = F8_R[F8_ISAR];
	return 2;
}
int LR_A_r_I(uint8_t v) // LR A, r Increment
{
	F8_A = F8_R[F8_ISAR];
	incISAR();
	return 2;
}
int LR_A_r_D(uint8_t v) // LR A, r Decrement
{
	F8_A = F8_R[F8_ISAR];
	decISAR();
	return 2;
}
	
int LR_r_A(uint8_t v)   // 5x LR r, A : r <- A
{
	F8_R[v&0xF] = F8_A;
	return 2;
} 
int LR_r_A_S(uint8_t v) // LR r, A Indirect
{
	F8_R[F8_ISAR] = F8_A;
	return 2;
}
int LR_r_A_I(uint8_t v) // LR r, A Increment
{
	F8_R[F8_ISAR] = F8_A;
	incISAR();
	return 2;
}
int LR_r_A_D(uint8_t v) // LR r, A Decrement
{
	F8_R[F8_ISAR] = F8_A;
	decISAR();
	return 2;
}

int LISU_i(uint8_t v) // 6x LISU i : ISARU <- i
{
	F8_ISAR = (F8_ISAR & 0x07) | ((v&0x7)<<3);
	return 2;
} 

int LISL_i(uint8_t v) // 6x LISL i : ISARL <- i
{
	F8_ISAR = (F8_ISAR & 0x38) | (v&0x7);
	return 2;
}

int LIS_i(uint8_t v) // 7x LIS i : A <- i 
{ 
	F8_A = v&0xF;
	return 2;
} 

int BT_t_n(uint8_t v) // 8x BT t, n : 1000 0ttt nnnn nnnn : 
{
	// AND bitmask t with W, if result is not 0: PC0<-PC0+n+1
	int t = ((F8_W) & (v&0x7))!=0;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int BP_n(uint8_t v)   // 81 BP n : branch if POSITIVE: PC0<-PC0+n+1 
{
	int t = ((F8_W>>flag_Sign)&1)==1;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int BC_n(uint8_t v)   // 82 BC n : branch if CARRY: PC0<-PC0+n+1
{
	int t = ((F8_W>>flag_Carry)&1)==1;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int BZ_n(uint8_t v)   // 84 BZ n : branch if ZERO: PC0<-PC0+n+1
{
	int t = ((F8_W>>flag_Zero)&1)==1;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int AM(uint8_t v)  // 88 AM  : A <- A+(DC0), DC0++
{
	F8_A = Add8(F8_A, MEMORY_read8(F8_DC0++));
	return 5;
} 

int AMD(uint8_t v) // 89 AMD : A <- A+(DC0) decimal adjusted, DC0++
{
	F8_A = AddBCD(F8_A, MEMORY_read8(F8_DC0++));
	return 5;
} 

int NM(uint8_t v) // 8A NM  : A <- A AND (DC0), DC0+1
{
	F8_A = And8(F8_A, MEMORY_read8(F8_DC0++));
	return 5;
}

int OM(uint8_t v) // 8B OM  : A <-  A OR (DC0), DC0+1
{
	F8_A = Or8(F8_A, MEMORY_read8(F8_DC0++));
	return 5;
} 

int XM(uint8_t v) // 8C XM  : A <-  A OR (DC0), DC0+1
{
	F8_A = Xor8(F8_A, MEMORY_read8(F8_DC0++));
	return 5;
}

int CM(uint8_t v) // 8D CM  : (DC0) - A, only set status, DC0+1
{
	Sub8(MEMORY_read8(F8_DC0++), F8_A);
	return 5;
} 

int ADC(uint8_t v) // 8E ADC : DC0 <- DC0 + A
{ 
	F8_DC0 += F8_A+(0xFF00*(F8_A>0x7F)); // A is signed
	return 5;
} 

int BR7_n(uint8_t v)   // 8F BR7 n : if ISARL != 7: PC0<-PC0+n+1 
{
	int t = (F8_ISAR&0x7)!=7;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int BR_n(uint8_t v) // 90 BR n : PC0 <- PC0+n+1
{ 
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n);
	return 7;
}

int BN_n(uint8_t v)   // 91 BN n : branch if NEGATIVE PC0<-PC0+n+1
{
	int t = ((F8_W>>flag_Sign)&1)==0;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int BNC_n(uint8_t v) // 92 BNC n : branch if NO CARRY: PC0 <- PC0+n+1 
{
	int t = ((F8_W>>flag_Carry)&1)==0;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int BF_i_n(uint8_t v) // 9x BF i, n : 1001 iiii nnnn nnnn 
{
	// AND bitmask i with W, if result is 0: PC0 <- PC0+n+1
	int t = ((F8_W) & (v&0xF))==0;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int BNZ_n(uint8_t v)  // 94 BNZ n : branch if NOT ZERO: PC0 <- PC0+n+1 
{
	int t = ((F8_W>>flag_Zero)&1)==0;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int BNO_n(uint8_t v) // 98 BNO n : branch if NO OVERFLOW: PC0 <- PC0+n+1 
{
	int t = ((F8_W>>flag_Overflow)&1)==0;
	uint8_t n = readOperand8();
	F8_PC0 = F8_PC0+calcBranch(n)*(t);
	return 6 + (t); // 3 - no jump,  3.5 - jump
}

int INS_i(uint8_t v)  // Ax INS_i : 1010 iiii : A <- (Port i)
{
	//  if i=2..15: Data Bus <- Port Address, A <- (Port i)
	F8_A = PORTS_read(v&0xF);
	setFlags_0z0s(F8_A);
	return 4 + 4*((v&0xF)>1); // 2 i=0..1, 4 i=2..15
}

int OUTS_i(uint8_t v) // Bx OUTS i : 1011 iiii : Port i <- A
{
	// if i=2..15: Data Bus <- Port Address, Port i <- A
	PORTS_notify(v&0xF, F8_A);
	return 4 + 4*((v&0xF)>1); // 2 i=0..1, 4 i=2..15
}

int AS_r(uint8_t v) // Cx AS r : A <- A+(r) 
{
	F8_A = Add8(F8_A, F8_R[v&0xF]);
	return 2;
} 
int AS_r_S(uint8_t v) // AS r Indirect
{
	F8_A = Add8(F8_A, F8_R[F8_ISAR]);
	return 2;
}
int AS_r_I(uint8_t v) // AS r Increment
{
	F8_A = Add8(F8_A, F8_R[F8_ISAR]);
	incISAR();
	return 2;
}
int AS_r_D(uint8_t v) // AS r Decrement
{
	F8_A = Add8(F8_A, F8_R[F8_ISAR]);
	decISAR();
	return 2;
}

int ASD_r(uint8_t v)   // Dx ASD r  : A <- A+(r) [BCD]
{
	F8_A = AddBCD(F8_A, F8_R[v&0xF]);
	return 4;
}
int ASD_r_S(uint8_t v) // ASD r Indirect
{
	F8_A = AddBCD(F8_A, F8_R[F8_ISAR]); 
	return 4;
}
int ASD_r_I(uint8_t v) // ASD r Increment
{
	F8_A = AddBCD(F8_A, F8_R[F8_ISAR]);
	incISAR();
	return 4;
}
int ASD_r_D(uint8_t v) // ASD r Decrement
{
	F8_A = AddBCD(F8_A, F8_R[F8_ISAR]);
	decISAR();
	return 4;
}

int XS_r(uint8_t v) // Ex XS r   : A <- A XOR (r) 
{
	F8_A = Xor8(F8_A, F8_R[v&0xF]);
	return 2;
} 
int XS_r_S(uint8_t v) // XS r Indirect
{
	F8_A = Xor8(F8_A, F8_R[F8_ISAR]);
	return 2;
}
int XS_r_I(uint8_t v) // XS r Increment
{
	F8_A = Xor8(F8_A, F8_R[F8_ISAR]);
	incISAR();
	return 2;
}
int XS_r_D(uint8_t v) // XS r Decrement
{
	F8_A = Xor8(F8_A, F8_R[F8_ISAR]);
	decISAR();
	return 2;
}

int NS_r(uint8_t v)  // Fx NS r   : A <- A AND (r) 
{
	F8_A = And8(F8_A, F8_R[v&0xF]);
	return 2;
}
int NS_r_S(uint8_t v) // NS r Indirect
{
	F8_A = And8(F8_A, F8_R[F8_ISAR]);
	return 2;
}
int NS_r_I(uint8_t v) // NS r Increment
{
	F8_A = And8(F8_A, F8_R[F8_ISAR]);
	incISAR();
	return 2;
}
int NS_r_D(uint8_t v) // NS r Decrement
{
	F8_A = And8(F8_A, F8_R[F8_ISAR]);
	decISAR();
	return 2;
}


/* *****************************
   *
   *  Map Opcodes to functions
   *
   ***************************** */
void F8_init()
{
	int i;

	OpCodes[0x00] = LR_A_Ku; // 00 LR A, Ku : A <- R12
	OpCodes[0x01] = LR_A_Kl; // 01 LR A, Kl : A <- R13
	OpCodes[0x02] = LR_A_Qu; // 02 LR A, Qu : A <- R14
	OpCodes[0x03] = LR_A_Ql; // 03 LR A, Ql : A <- R15
	OpCodes[0x04] = LR_Ku_A; // 04 LR Ku, A : R12 <- A
	OpCodes[0x05] = LR_Kl_A; // 05 LR Kl, A : R13 <- A
	OpCodes[0x06] = LR_Qu_A; // 06 LR Qu, A : R14 <- A
	OpCodes[0x07] = LR_Ql_A; // 07 LR Ql, A : R15 <- A
	OpCodes[0x08] = LR_K_P;  // 08 LR K, P  : R12 <- PC1U, R13 <- PC1L 
	OpCodes[0x09] = LR_P_K;  // 09 LR P, K  : PC1U <- R12, PC1L <- R13
	OpCodes[0x0A] = LR_A_IS; // 0A LR A, IS : A <- ISAR
	OpCodes[0x0B] = LR_IS_A; // 0B LR IS, A : ISAR <- A
	OpCodes[0x0C] = PK;      // 0C PK       : PC1 <- PC0, PC0 <- Q
	OpCodes[0x0D] = LR_P0_Q; // 0D LR P0, Q : PC0L <- R15, PC0U <- R14 
	OpCodes[0x0E] = LR_Q_DC; // 0E LR Q, DC : R14 <- DC0U, R15 <- DC0L
	OpCodes[0x0F] = LR_DC_Q; // 0F LR DC, Q : DC0U <- R14, DC0L <- R15
	OpCodes[0x10] = LR_DC_H; // 10 LR DC, H : DC0U <- R10, DC0L <- R11 
	OpCodes[0x11] = LR_H_DC; // 11 LR H, DC : R10 <- DC0U, R11 <- DC0L
	OpCodes[0x12] = SR_1;    // 12 SR 1     : A >> 1
	OpCodes[0x13] = SL_1;    // 13 SL 1     : A << 1
	OpCodes[0x14] = SR_4;    // 14 SR 4     : A >> 4
	OpCodes[0x15] = SL_4;    // 15 SL 4     : A << 4
	OpCodes[0x16] = LM;      // 16 LM       : A <- (DC0), DC0 <- DC0 + 1
	OpCodes[0x17] = ST;      // 17 ST       : DC0 <- A, DC0 <- DC0 + 1 
	OpCodes[0x18] = COM;     // 18 COM A    : A <- A XOR 0xFF
	OpCodes[0x19] = LNK;     // 19 LNK      : A <- A + C 
	OpCodes[0x1A] = DI;      // 1A DI       : Disable Interupts
	OpCodes[0x1B] = EI;      // 1B EI       : Enable Interupts
	OpCodes[0x1C] = POP;     // 1C POP      : PC0 <- PC1, A destroyed 
	OpCodes[0x1D] = LR_W_J;  // 1D LR W, J  : W <- R9
	OpCodes[0x1E] = LR_J_W;  // 1E LR J, W  : R9 <- W
	OpCodes[0x1F] = INC;     // 1F INC      : A <- A + 1
	OpCodes[0x20] = LI_n;    // 20 LI n     : A <- n 
	OpCodes[0x21] = NI_n;    // 21 NI n     : A <- A AND n 
	OpCodes[0x22] = OI_n;    // 22 OI n     : A <- A OR n 
	OpCodes[0x23] = XI_n;    // 23 XI n     : A <- A XOR n 
	OpCodes[0x24] = AI_n;    // 24 AI n     : A <- A + n 
	OpCodes[0x25] = CI_n;    // 25 CI n     : n+!(A)+1 (n-A), Only set status
	OpCodes[0x26] = IN_n;    // 26 IN n     : Data Bus <- Port n, A <- Port n
	OpCodes[0x27] = OUT_n;   // 27 OUT n    : Data Bus <- Port n, Port n <- A
	OpCodes[0x28] = PI_mn;   // 28 PI mn    : A <- m, PC1 <- PC0+1, PC0 <- mn
	OpCodes[0x29] = JMP_mn;  // 29 JMP mn   : A <- m, PC0L <- n, PC0U <- (A) 
	OpCodes[0x2A] = DCI_mn;  // 2A DCI mn   : DC0U <- m, PC0++, DC0L <- n, PC0++ 
	OpCodes[0x2B] = NOP;     // 2B NOP
	OpCodes[0x2C] = XDC;     // 2C XDC      : DC0,DC1 <- DC1,DC0 
	OpCodes[0x2D] = NOP;     // 2D         ** bad opcode **
	OpCodes[0x2E] = NOP;     // 2E         ** bad opcode **
	OpCodes[0x2F] = NOP;     // 2F         ** bad opcode **
	
	for(i=0; i<12; i++)
	{
		OpCodes[0x30+i] = DS_r;    // 3x DS      : r <- (r)+0xFF,
		OpCodes[0x40+i] = LR_A_r;  // 4x LR A, r : A <- r
		OpCodes[0x50+i] = LR_r_A;  // 5x LR r, A : r <- A
	}

	OpCodes[0x3C] = DS_r_S;
	OpCodes[0x3D] = DS_r_I;
	OpCodes[0x3E] = DS_r_D;
	OpCodes[0x3F] = NOP;

	OpCodes[0x4C] = LR_A_r_S;
	OpCodes[0x4D] = LR_A_r_I;
	OpCodes[0x4E] = LR_A_r_D;
	OpCodes[0x4F] = NOP;

	OpCodes[0x5C] = LR_r_A_S;
	OpCodes[0x5D] = LR_r_A_I;
	OpCodes[0x5E] = LR_r_A_D;
	OpCodes[0x5F] = NOP;

	for(i=0; i<8; i++)
	{
		OpCodes[0x60+i] = LISU_i;  // 6x LISU i : ISARU <- i 
		OpCodes[0x68+i] = LISL_i;  // 6x LISL i : ISARL <- i
	}

	for(i=0; i<16; i++)
	{
		OpCodes[0x70+i] = LIS_i;   // 7x LIS i  : A <- i
	}
	OpCodes[0x80] = BT_t_n; // 8x BT t, n : bit test t with W
	OpCodes[0x81] = BP_n;   // 81 BP n    : branch if POSITIVE: PC0<-PC0+n+1
	OpCodes[0x82] = BC_n;   // 82 BC n    : branch if CARRY: PC0<-PC0+n+1
	OpCodes[0x83] = BT_t_n; 
	OpCodes[0x84] = BZ_n;   // 84 BZ n    : branch if ZERO: PC0<-PC0+n+1
	OpCodes[0x85] = BT_t_n;
	OpCodes[0x86] = BT_t_n;
	OpCodes[0x87] = BT_t_n;
	OpCodes[0x88] = AM;     // 88 AM      : A <- A+(DC0), DC0++
	OpCodes[0x89] = AMD;    // 89 AMD     : A <- A+(DC0) decimal adjusted, DC0++
	OpCodes[0x8A] = NM;     // 8A NM      : A <- A AND (DC0), DC0+1
	OpCodes[0x8B] = OM;     // 8B OM      : A <- A OR (DC0), DC0+1
	OpCodes[0x8C] = XM;     // 8C XM      : A <- A XOR (DC0), DC0+1 
	OpCodes[0x8D] = CM;     // 8D CM      : (DC0) - A, only set status, DC0+1
	OpCodes[0x8E] = ADC;    // 8E ADC     : DC0 <- DC0 + A
	OpCodes[0x8F] = BR7_n;  // 8F BR7 n   : if ISARL != 7: PC0 <- PC0+n+1 
	OpCodes[0x90] = BR_n;   // 90 BR n    : PC0 <- PC0+n+1
	OpCodes[0x91] = BN_n;   // 91 BN n    : branch if NEGATIVE: 
	OpCodes[0x92] = BNC_n;  // 92 BNC n   : branch if NO CARRY: 
	OpCodes[0x93] = BF_i_n; // 9x BF i, n : bit test i with W
	OpCodes[0x94] = BNZ_n;  // 94 BNZ n   : branch if NOT ZERO: 
	OpCodes[0x95] = BF_i_n;
	OpCodes[0x96] = BF_i_n;
	OpCodes[0x97] = BF_i_n;
	OpCodes[0x98] = BNO_n;  // 98 BNO n   : branch if NO OVERFLOW:
	OpCodes[0x99] = BF_i_n;
	OpCodes[0x9A] = BF_i_n;
	OpCodes[0x9B] = BF_i_n;
	OpCodes[0x9C] = BF_i_n;
	OpCodes[0x9D] = BF_i_n;
	OpCodes[0x9E] = BF_i_n;
	OpCodes[0x9F] = BF_i_n;

	for(i=0; i<16; i++)
	{
		OpCodes[0xA0+i] = INS_i;  // Ax INS_i  : read port (short)
		OpCodes[0xB0+i] = OUTS_i; // Bx OUTS i : write port (short)
		OpCodes[0xC0+i] = AS_r;   // Cx AS r   : A <- A+(r) 
		OpCodes[0xD0+i] = ASD_r;  // Dx ASD r  : A <- A+(r) [BCD]
		OpCodes[0xE0+i] = XS_r;   // Ex XS r   : A <- A XOR (r)  
		OpCodes[0xF0+i] = NS_r;   // Fx NS r   : A <- A AND (r)
	}
	OpCodes[0xCC] = AS_r_S;
	OpCodes[0xCD] = AS_r_I;
	OpCodes[0xCE] = AS_r_D;
	OpCodes[0xCF] = NOP;

	OpCodes[0xDC] = ASD_r_S;
	OpCodes[0xDD] = ASD_r_I;
	OpCodes[0xDE] = ASD_r_D;
	OpCodes[0xDF] = NOP;

	OpCodes[0xEC] = XS_r_S;
	OpCodes[0xED] = XS_r_I;
	OpCodes[0xEE] = XS_r_D;
	OpCodes[0xEF] = NOP;

	OpCodes[0xFC] = NS_r_S;
	OpCodes[0xFD] = NS_r_I;
	OpCodes[0xFE] = NS_r_D;
	OpCodes[0xFF] = NOP;
}

int F8_exec(void) /* execute a single instruction */
{
  	uint8_t opcode = MEMORY_read8(F8_PC0++);
	return OpCodes[opcode](opcode);
}

void F8_reset(void)
{
	/* clear registers, flags */
	F8_A=0;
	F8_W=0;
	F8_ISAR = 0;
	F8_PC0=0; F8_PC1=0;
	F8_DC0=0; F8_DC1=0;

	/* clear scratchpad */
	memset(F8_R, 0, sizeof(F8_R));
}


