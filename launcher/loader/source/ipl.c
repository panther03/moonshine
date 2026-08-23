#include <gccore.h>
#include <string.h>
#include "ipl.h"

static const u8 BS2Ntsc448IntAa[] = {
	0x00,0x00,0x00,0x00,
	0x02,0x50,0x00,0xE2,
	0x01,0xC0,0x00,0x28,
	0x00,0x10,0x02,0x80,
	0x01,0xC0,0x00,0x00,
	0x00,0x00,0x00,0x01,
	0x00,0x01,0x03,0x02,
	0x09,0x06,0x03,0x0A,
	0x03,0x02,0x09,0x06,
	0x03,0x0A,0x09,0x02,
	0x03,0x06,0x09,0x0A,
	0x09,0x02,0x03,0x06,
	0x09,0x0A,0x08,0x08,
	0x0A,0x0C,0x0A,0x08,
	0x08,0x00,0x00,0x00
};

// bootrom descrambler reversed by segher
// Copyright 2008 Segher Boessenkool <segher@kernel.crashing.org>
void Descrambler(unsigned char* data, unsigned int size)
{
	unsigned char acc = 0;
	unsigned char nacc = 0;

	unsigned short t = 0x2953;
	unsigned short u = 0xd9c2;
	unsigned short v = 0x3ff1;

	unsigned char x = 1;
	unsigned int it;
	for (it = 0; it < size; )
	{
		int t0 = t & 1;
		int t1 = (t >> 1) & 1;
		int u0 = u & 1;
		int u1 = (u >> 1) & 1;
		int v0 = v & 1;

		x ^= t1 ^ v0;
		x ^= (u0 | u1);
		x ^= (t0 ^ u1 ^ v0) & (t0 ^ u0);

		if (t0 == u0)
		{
			v >>= 1;
			if (v0)
				v ^= 0xb3d0;
		}

		if (t0 == 0)
		{
			u >>= 1;
			if (u0)
				u ^= 0xfb10;
		}

		t >>= 1;
		if (t0)
			t ^= 0xa740;

		nacc++;
		acc = 2*acc + x;
		if (nacc == 8)
		{
			data[it++] ^= acc;
			nacc = 0;
		}
	}
}

#define IPL_ROM_FONT_SJIS	0x1AFF00

#define DECRYPT_START		0x100
#define CODE_START			0x820
#define IPL_BASE			0x81300000u

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static const u8 kEnglishValues[] = {
	38, 10, 39, 15, 7, 1, 4, 45, 46, 42, 40, 43, 31, 29, 30, 80,
};

static const u16 kEnglishUs12a[] = {
	0x0B906, 0x0B926, 0x0B92E, 0x0B932, 0x0B93A, 0x0B93E,
	0x0B946, 0x0B94A, 0x0B952, 0x0B956, 0x0B95E, 0x0B962,
	0x0B976, 0x0B97A, 0x0B982, 0x0B98E,
};

static const u16 kEnglishUs12b[] = {
	0x0B91E, 0x0B93E, 0x0B946, 0x0B94A, 0x0B952, 0x0B956,
	0x0B95E, 0x0B962, 0x0B96A, 0x0B96E, 0x0B976, 0x0B97A,
	0x0B98E, 0x0B992, 0x0B99A, 0x0B9A6,
};

static const u16 kEnglishUs11[] = {
	0x0B592, 0x0B5B2, 0x0B5BA, 0x0B5BE, 0x0B5C6, 0x0B5CA,
	0x0B5D2, 0x0B5D6, 0x0B5DE, 0x0B5E2, 0x0B5EA, 0x0B5EE,
	0x0B602, 0x0B606, 0x0B60E, 0x0B61A,
};

static const u16 kEnglishUs10[] = {
	0x0B40A, 0x0B412, 0x0B416, 0x0B422, 0x0B42E, 0x0B43A,
	0x0B446, 0x0B44E, 0x0B452, 0x0B45A, 0x0B45E, 0x0B466,
	0x0B476, 0x0B47E, 0x0B482, 0x0B48E,
};

static const u8 kPalAnimHalfOffsets[] = {
	0x06, 0x0A, 0x16, 0x1A, 0x1E, 0x22, 0x26, 0x2A, 0x32, 0x36,
};
static const u8 kPalAnimHalfValues[] = {
	10, 255, 7, 6, 5, 16, 18, 20, 40, 60,
};
static const u8 kPalAnimWordOffsets[] = { 0x70, 0x78, 0x7C, 0x80, 0x84 };
static const u32 kPalAnimWordValues[] = {
	0xB3E30016, 0x9963000B, 0x9BC3001C, 0x9BA30037, 0x99430035,
};

static void ApplyEnglishPatches(const u16 *offsets, u8 secondValue)
{
	u32 i;
	for (i = 0; i < ARRAY_COUNT(kEnglishValues); ++i)
	{
		const u16 value = i == 1 ? secondValue : kEnglishValues[i];
		*(u16 *)(IPL_BASE + offsets[i]) = value;
	}
}

static void ApplyPalAnimation(u32 base)
{
	u32 i;
	for (i = 0; i < ARRAY_COUNT(kPalAnimHalfOffsets); ++i)
		*(u16 *)(base + kPalAnimHalfOffsets[i]) = kPalAnimHalfValues[i];
	for (i = 0; i < ARRAY_COUNT(kPalAnimWordOffsets); ++i)
		*(u32 *)(base + kPalAnimWordOffsets[i]) = kPalAnimWordValues[i];
}

static void ApplyVideoFilter(u32 address, bool progressive, bool sharp)
{
	if (progressive)
	{
		*(u16 *)address = 0x0408;
		*(u32 *)(address + 2) = 0x0C100C08;
		*(u16 *)(address + 6) = 0x0400;
	}
	if (sharp)
	{
		*(u16 *)address = 0;
		*(u32 *)(address + 2) = 0x15161500;
		*(u16 *)(address + 6) = 0;
	}
}

static void __attribute__((noinline)) ApplyJingle(u32 address, int jingle)
{
	if (jingle > 0)
		*(u32 *)address = 0x38600000u | (u32)jingle;
}

void load_ipl(unsigned char *buf, bool prog, bool sharp, int jingle, int type)
{
	Descrambler(buf + DECRYPT_START, IPL_ROM_FONT_SJIS - DECRYPT_START);
	memcpy((void*)0x81300000, buf + CODE_START, IPL_ROM_FONT_SJIS - CODE_START);
	
	//This is my GC's NTSC-U 1.2 IPL, April 15 2003
	if(*(u32 *)0x8130B904 == 0x3800000C) {
		// 480p video mode
		if(prog) {
			*(s16 *)0x81300876 = type; //swiss
			*(s16 *)0x8137ECBA = type; //2 = 480p
			*(s16 *)0x8137ECCE = 0; //single field
		}
		else {
			//test if this removes the video refresh
			*(s16 *)0x81300876 = type; //swiss
			//*(s16 *)0x8137ECBA = type; //video mode
		}
		ApplyVideoFilter(0x8137ECEA, prog, sharp);
		
		// Accept any region code. JP games don't need this, but PAL ones do
		*(s16 *)0x81300ACE = 1;
		*(s16 *)0x81300AF2 = 1;
		
		// Force boot sound. 1=kid, 2=kabuki
		ApplyJingle(0x81303184, jingle);
		
		// JP games otherwise show this NTSC-U IPL in Japanese.
		ApplyEnglishPatches(kEnglishUs12a, 10);
	}
	else if(*(u32 *)0x8130B91C == 0x3800000C) { //other NTSC-U 1.2
		// 480p video mode
		if(prog) {
			*(s16 *)0x81300876 = type; //swiss
			*(s16 *)0x8137F13A = type; //2 = 480p
			*(s16 *)0x8137F14E = 0; //single field
		}
		ApplyVideoFilter(0x8137F16A, prog, sharp);
		
		// Accept any region code.
		*(s16 *)0x81300ACE = 1;
		*(s16 *)0x81300AF2 = 1;
		
		// Force boot sound. 1=kid, 2=kabuki
		ApplyJingle(0x8130319C, jingle);
		
		ApplyEnglishPatches(kEnglishUs12b, 10);
	}
	else if(*(u32 *)0x8130B590 == 0x3800000C) { //NTSC-U 1.1
		// 480p video mode
		if(prog) {
			*(s16 *)0x81300522 = type; //swiss
			*(s16 *)0x8137D9F2 = type; //2 = 480p
			*(s16 *)0x8137DA06 = 0; //single field
		}
		ApplyVideoFilter(0x8137DA22, prog, sharp);
		
		// Accept any region code.
		*(s16 *)0x8130077E = 1;
		*(s16 *)0x813007A2 = 1;
		
		// Force boot sound. 1=kid, 2=kabuki
		ApplyJingle(0x81302DE8, jingle);
		
		ApplyEnglishPatches(kEnglishUs11, 10);
	}
	else if(*(u32 *)0x8130B408 == 0x3800000B) { //NTSC-U 1.0
		// 480p video mode
		if(prog) {
			*(s16 *)0x81300712 = 2 & ~0x3; //swiss, 480p does not work
		//	*(s16 *)0x8135DDE2 = 2; //2 = 480p
		//	*(s16 *)0x8135DDF6 = 0; //field rendering
		}
		ApplyVideoFilter(0x8135DE12, prog, sharp);
		
		// Accept any region code
		*(s16 *)0x81300E8A = 1;
		*(s16 *)0x81300EA2 = 1;
		*(s16 *)0x81300EAA = 1;
		
		// Force boot sound. 1=kid, 2=kabuki
		ApplyJingle(0x81302F00, jingle);
		
		ApplyEnglishPatches(kEnglishUs10, 38);
	}
	else if(*(u32 *)0x81300610 == 0x38600004) { //PAL 1.2
		// 480p video mode, this needs to be fixed for PAL60
		if(prog) {
			*(s16 *)0x81300612 = type; //swiss
			
			ApplyPalAnimation(0x8130F300);
			
			memcpy((void*)0x81382470, BS2Ntsc448IntAa, sizeof(BS2Ntsc448IntAa));
			
			*(s16 *)0x81382472 = type; //2 = 480p
			*(s16 *)0x81382486 = 0; //field rendering
		}
		ApplyVideoFilter(0x813824A2, prog, sharp);
		
		// Accept any region code (unneeded)
		*(s16 *)0x81300882 = 1;
		*(s16 *)0x813008A6 = 1;
		
		// Force boot sound. 1=kid, 2=kabuki
		ApplyJingle(0x81302F50, jingle);
	}
	else if(*(u32 *)0x81300520 == 0x38600004) { //PAL 1.0
		// 480p video mode, this needs to be fixed for PAL60
		if(prog) {
			*(s16 *)0x81300522 = type; //swiss
			
			ApplyPalAnimation(0x8130F1C0);
			
			memcpy((void*)0x81380FD0, BS2Ntsc448IntAa, sizeof(BS2Ntsc448IntAa));
			
			*(s16 *)0x81380FD2 = type; //2 = 480p
			*(s16 *)0x81380FE6 = 0; //field rendering
		}
		ApplyVideoFilter(0x81381002, prog, sharp);
		
		// Accept any region code
		*(s16 *)0x81300882 = 1;
		*(s16 *)0x813008A6 = 1;
		
		// Force boot sound. 1=kid, 2=kabuki
		ApplyJingle(0x81302DE8, jingle);
	}
	else if(*(u32 *)0x81300520 == 0x38600008) { //MPAL 1.1
		// 480p video mode
		if(prog) {
			*(s16 *)0x81300522 = type; //swiss
			
			// Correct animation speed for 50Hz
		/*	*(s16 *)0x8130E9D6 = 8;
			*(s16 *)0x8130E9DA = 15;
			*(s16 *)0x8130E9E6 = 6;
			*(s16 *)0x8130E9EA = 5;
			*(s16 *)0x8130E9EE = 4;
			*(s16 *)0x8130E9F2 = 13;
			*(s16 *)0x8130E9F6 = 255;
			*(s16 *)0x8130E9FA = 17;
			*(s16 *)0x8130EA02 = 33;
			*(s16 *)0x8130EA06 = 50;
			*(u32 *)0x8130EA40 = 0xB1630016;
			*(u32 *)0x8130EA48 = 0x9943000B;
			*(u32 *)0x8130EA4C = 0x9BA3001C;
			*(u32 *)0x8130EA50 = 0x9B830037;
			*(u32 *)0x8130EA54 = 0x99630035; */
			
			memcpy((void*)0x8137D910, BS2Ntsc448IntAa, sizeof(BS2Ntsc448IntAa));
			
			*(s16 *)0x8137D912 = type; //2 = 480p
			*(s16 *)0x8137D926 = 0; //field rendering
		}
		ApplyVideoFilter(0x8137D942, prog, sharp);
		
		// Accept any region code
		*(s16 *)0x81300882 = 1;
		*(s16 *)0x813008A6 = 1;
		
		// Force boot sound. 1=kid, 2=kabuki
		ApplyJingle(0x81302DE8, jingle);
	}
	
	DCFlushRange((void*)0x81300000, IPL_ROM_FONT_SJIS - CODE_START);
}
