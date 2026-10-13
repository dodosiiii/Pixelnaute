#pragma once
//=============================================================================
// tests/pc/3ds_stub.h - Faux en-tete 3ds.h pour la verification hors console.
//
// Permet de compiler source/render/text.cpp tel quel sur un PC. Les fonctions
// de police sont reimplementees ici avec une feuille A4 entrelacee fabriquee
// par le test : c'est le meme code de decodage qui est ainsi verifie.
//=============================================================================
#ifndef N3DSWEB_PC_STUB_3DS_H
#define N3DSWEB_PC_STUB_3DS_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef uint32_t Result;

//---------------------------------------------------------------------------
// Structures de police (identiques a libctru)
//---------------------------------------------------------------------------
typedef struct
{
	s8 left;
	u8 glyphWidth;
	u8 charWidth;
} charWidthInfo_s;

typedef struct
{
	u8 cellWidth;
	u8 cellHeight;
	u8 baselinePos;
	u8 maxCharWidth;
	u32 sheetSize;
	u16 nSheets;
	u16 sheetFmt;
	u16 nRows;
	u16 nLines;
	u16 sheetWidth;
	u16 sheetHeight;
	u8* sheetData;
} TGLP_s;

typedef struct tag_CWDH_s CWDH_s;
struct tag_CWDH_s
{
	u16 startIndex;
	u16 endIndex;
	CWDH_s* next;
	charWidthInfo_s widths[0];
};

typedef struct tag_CMAP_s CMAP_s;
struct tag_CMAP_s
{
	u16 codeBegin;
	u16 codeEnd;
	u16 mappingMethod;
	u16 reserved;
	CMAP_s* next;
	union
	{
		u16 indexOffset;
		u16 indexTable[0];
		struct
		{
			u16 nScanEntries;
			struct { u16 code; u16 glyphIndex; } scanEntries[0];
		};
	};
};

typedef struct
{
	u32 signature;
	u32 sectionSize;
	u8 fontType;
	u8 lineFeed;
	u16 alterCharIndex;
	charWidthInfo_s defaultWidth;
	u8 encoding;
	TGLP_s* tglp;
	CWDH_s* cwdh;
	CMAP_s* cmap;
	u8 height;
	u8 width;
	u8 ascent;
	u8 padding;
} FINF_s;

typedef struct
{
	u32 signature;
	u16 endianness;
	u16 headerSize;
	u32 version;
	u32 fileSize;
	u32 nBlocks;
	FINF_s finf;
} CFNT_s;

typedef struct
{
	int sheetIndex;
	float xOffset;
	float xAdvance;
	float width;
	struct { float left, top, right, bottom; } texcoord;
	struct { float left, top, right, bottom; } vtxcoord;
} fontGlyphPos_s;

#define R_FAILED(res) ((res) != 0)

//---------------------------------------------------------------------------
// API de police utilisee par text.cpp (reimplementee par le stub)
//---------------------------------------------------------------------------
Result fontEnsureMapped(void);
CFNT_s* fontGetSystemFont(void);
FINF_s* fontGetInfo(CFNT_s* font);
TGLP_s* fontGetGlyphInfo(CFNT_s* font);
int fontGlyphIndexFromCodePoint(CFNT_s* font, u32 codePoint);
charWidthInfo_s* fontGetCharWidthInfo(CFNT_s* font, int glyphIndex);
void fontCalcGlyphPos(fontGlyphPos_s* out, CFNT_s* font, int glyphIndex,
	u32 flags, float scaleX, float scaleY);

//---------------------------------------------------------------------------
// Divers utilises par common.hpp
//---------------------------------------------------------------------------
#ifndef BIT
#define BIT(n) (1u << (n))
#endif

static inline u16 RGB8_to_565(u8 r, u8 g, u8 b)
{
	return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Horloge systeme : common.hpp definit timeMs() a partir de osGetTime().
unsigned long long osGetTime(void);

#endif // N3DSWEB_PC_STUB_3DS_H
