#pragma once
//=============================================================================
// text.hpp - Rendu de texte TrueType (TTF) via stb_truetype avec repli sur la
// police systeme de la 3DS.
//=============================================================================
#include "../common.hpp"
#include "bitmap.hpp"
#include <3ds.h>
#include <vector>
#include <unordered_map>
#include "stb_truetype.h"

struct GlyphKey
{
	uint32_t cp;
	uint16_t sizePx;
	uint8_t bold;
	bool operator==(const GlyphKey& o) const
	{
		return cp == o.cp && sizePx == o.sizePx && bold == o.bold;
	}
};

struct GlyphKeyHash
{
	size_t operator()(const GlyphKey& k) const
	{
		return (size_t)k.cp ^ ((size_t)k.sizePx << 16) ^ ((size_t)k.bold << 24);
	}
};

struct CachedGlyph
{
	int w = 0;
	int h = 0;
	int xoff = 0;
	int yoff = 0;
	float xadvance = 0.0f;
	std::vector<uint8_t> alpha;
};

class TextEngine
{
public:
	bool init();          // Chargement TTF (RomFS/SD) puis repli police systeme
	bool ready() const { return ttfLoaded_ || (font_ != nullptr); }

	float textScale() const { return sTextScale_; }  // ~1.0
	int   cellHeight() const { return cellHeight_; }
	int   cellWidth() const { return cellWidth_; }
	int   sheetFormatCode() const { return sheetFmt_; }
	int   charactersPerSheet() const { return charPerSheet_; }
	int   sheetCount() const { return nSheets_; }
	int   sheetWidthPx() const { return sheetWidth_; }
	int   sheetHeightPx() const { return sheetHeight_; }
	int   rowsPerSheet() const { return nRows_; }
	int   linesPerSheet() const { return nLines_; }
	void  resetGlyphStats() { glyphMin_ = 0x7FFFFFFF; glyphMax_ = -1; sheetMax_ = -1; }

	// Format de la police / feuille, pour le diagnostic ("TTF", "A4", etc.).
	const char* sheetFormatName() const;

	// Hauteur de ligne (px) pour une echelle utilisateur donnee.
	int linePitch(float userScale) const;

	// Largeur d'une chaine UTF-8 (px) a l'echelle donnee.
	float measure(const std::string& utf8, float userScale) const;

	// Dessine une ligne de texte (sans retour chariot) dans le bitmap.
	// y = haut de la ligne. Retourne la largeur dessinee.
	float drawLine(Bitmap& bmp, const std::string& utf8, int x, int y,
		float userScale, uint32_t color, bool bold, bool underline) const;

	// Decoupe un texte en mots en tenant compte de la largeur dispo.
	static std::vector<std::pair<size_t, size_t>> splitWords(const std::string& utf8);

	// Rend le caractere de remplacement si le glyphe manque.
	int fallbackGlyph() const { return fallback_; }

	// Indique si le moteur utilise une vraie police TTF
	bool isTrueType() const { return ttfLoaded_; }

	//--- acces de diagnostic ---
	u8 glyphAlphaAt(int glyphIndex, int cellX, int cellY) const;
	int glyphIndexOfCodePoint(uint32_t codePoint) const;
	float glyphAdvance(int glyphIndex) const;
	float glyphInkWidth(int glyphIndex) const;
	float glyphLeftBearing(int glyphIndex) const;
	void glyphStats(int& minIndex, int& maxIndex, int& maxSheet) const
	{
		minIndex = glyphMin_; maxIndex = glyphMax_; maxSheet = sheetMax_;
	}
	u32 sheetSizeBytes() const { return sheetSize_; }

private:
	// TrueType state
	bool ttfLoaded_ = false;
	bool ttfBoldReady_ = false;
	std::vector<uint8_t> ttfRegularData_;
	std::vector<uint8_t> ttfBoldData_;
	stbtt_fontinfo ttfRegularInfo_{};
	stbtt_fontinfo ttfBoldInfo_{};
	int ttfAscent_ = 0;
	int ttfDescent_ = 0;
	int ttfLineGap_ = 0;
	mutable std::unordered_map<GlyphKey, CachedGlyph, GlyphKeyHash> glyphCache_;

	const CachedGlyph& getGlyph(uint32_t cp, int sizePx, bool bold) const;
	bool loadTtfFont(const char* path, std::vector<uint8_t>& outData, stbtt_fontinfo& outInfo);

	// Police systeme (repli)
	CFNT_s* font_ = nullptr;
	TGLP_s* tglp_ = nullptr;
	float sTextScale_ = 1.0f;
	int cellWidth_ = 0, cellHeight_ = 0;
	int sheetWidth_ = 0, sheetHeight_ = 0;
	int nRows_ = 0, nLines_ = 0;
	int charPerSheet_ = 0;
	int sheetFmt_ = -1;
	u32 sheetSize_ = 0;
	const u8* sheetData_ = nullptr;
	int fallback_ = 0;
	int nSheets_ = 0;
	mutable int glyphMin_ = 0x7FFFFFFF;
	mutable int glyphMax_ = -1;
	mutable int sheetMax_ = -1;

	int sheetPixelPos(int x, int y) const;
	u8 alphaAt(int glyphIndex, int cellX, int cellY) const;
	int resolveGlyph(u32 codePoint) const;

public:
	static TextEngine& instance();
};