#include "text.hpp"
#include "font_decode.hpp"
#include <3ds.h>
#include <cmath>
#include <cstdio>

static const float kAdvanceFactor = 1.0f;

TextEngine& TextEngine::instance()
{
	static TextEngine eng;
	return eng;
}

bool TextEngine::loadTtfFont(const char* path, std::vector<uint8_t>& outData, stbtt_fontinfo& outInfo)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (sz <= 0 || sz > 16 * 1024 * 1024)
	{
		fclose(f);
		return false;
	}

	outData.resize((size_t)sz);
	size_t rd = fread(outData.data(), 1, (size_t)sz, f);
	fclose(f);

	if (rd != (size_t)sz)
	{
		outData.clear();
		return false;
	}

	int offset = stbtt_GetFontOffsetForIndex(outData.data(), 0);
	if (offset < 0)
	{
		outData.clear();
		return false;
	}

	if (!stbtt_InitFont(&outInfo, outData.data(), offset))
	{
		outData.clear();
		return false;
	}

	return true;
}

bool TextEngine::init()
{
	if (ready()) return true;

	// 1. Essai de chargement de la police TrueType (RomFS puis SD)
	const char* regPaths[] = {
		"romfs:/NotoSans-Regular.ttf",
		"sdmc:/N3DSWeb/NotoSans-Regular.ttf",
		"sdmc:/N3DSWeb/font.ttf"
	};
	for (const char* p : regPaths)
	{
		if (loadTtfFont(p, ttfRegularData_, ttfRegularInfo_))
		{
			ttfLoaded_ = true;
			break;
		}
	}

	if (ttfLoaded_)
	{
		stbtt_GetFontVMetrics(&ttfRegularInfo_, &ttfAscent_, &ttfDescent_, &ttfLineGap_);

		const char* boldPaths[] = {
			"romfs:/NotoSans-Bold.ttf",
			"sdmc:/N3DSWeb/NotoSans-Bold.ttf",
			"sdmc:/N3DSWeb/font_bold.ttf"
		};
		for (const char* p : boldPaths)
		{
			if (loadTtfFont(p, ttfBoldData_, ttfBoldInfo_))
			{
				ttfBoldReady_ = true;
				break;
			}
		}

		cellWidth_ = 14;
		cellHeight_ = 24;
		sTextScale_ = 1.0f;
		sheetFmt_ = 99; // code personnalise pour TTF
		return true;
	}

	// 2. Repli : police systeme de la console (CFNT / TGLP)
	if (R_FAILED(fontEnsureMapped())) return false;
	font_ = fontGetSystemFont();
	if (!font_) return false;
	tglp_ = fontGetGlyphInfo(font_);
	if (!tglp_) return false;
	if (tglp_->cellWidth == 0 || tglp_->cellHeight == 0 ||
		tglp_->nRows == 0 || tglp_->nLines == 0 || tglp_->sheetWidth == 0 ||
		tglp_->sheetHeight == 0 || tglp_->sheetData == nullptr)
	{
		font_ = nullptr;
		return false;
	}

	sTextScale_ = 30.0f / (float)tglp_->cellHeight;
	cellWidth_  = tglp_->cellWidth;
	cellHeight_ = tglp_->cellHeight;
	sheetWidth_ = tglp_->sheetWidth;
	sheetHeight_ = tglp_->sheetHeight;
	nRows_ = tglp_->nRows;
	nLines_ = tglp_->nLines;
	charPerSheet_ = nRows_ * nLines_;
	sheetFmt_ = tglp_->sheetFmt;
	sheetSize_ = tglp_->sheetSize;
	sheetData_ = tglp_->sheetData;
	nSheets_ = (int)tglp_->nSheets;
	fallback_ = font_->finf.alterCharIndex;
	if (fallback_ < 0) fallback_ = 0;

	if (nSheets_ <= 0 || nSheets_ > 8192 || sheetSize_ == 0)
	{
		font_ = nullptr;
		return false;
	}
	resetGlyphStats();
	return true;
}

const char* TextEngine::sheetFormatName() const
{
	if (ttfLoaded_) return "TrueType (Noto Sans)";
	switch (sheetFmt_)
	{
		case 0x0: return "RGBA8";
		case 0x5: return "LA8";
		case 0x8: return "A8";
		case 0x9: return "LA4";
		case 0xA: return "L4";
		case 0xB: return "A4";
		default:  return "inconnu";
	}
}

int TextEngine::linePitch(float userScale) const
{
	if (ttfLoaded_)
	{
		float pixelHeight = 24.0f * userScale;
		float scale = stbtt_ScaleForPixelHeight(&ttfRegularInfo_, pixelHeight);
		int pitch = (int)ceilf((float)(ttfAscent_ - ttfDescent_ + ttfLineGap_) * scale);
		if (pitch < (int)ceilf(pixelHeight * 1.25f))
			pitch = (int)ceilf(pixelHeight * 1.25f);
		return pitch;
	}
	if (!font_) return (int)ceilf(24.0f * userScale);
	return (int)(ceilf(userScale * sTextScale_ * (float)font_->finf.lineFeed));
}

const CachedGlyph& TextEngine::getGlyph(uint32_t cp, int sizePx, bool bold) const
{
	GlyphKey key{ cp, (uint16_t)sizePx, (uint8_t)(bold ? 1 : 0) };
	auto it = glyphCache_.find(key);
	if (it != glyphCache_.end())
		return it->second;

	CachedGlyph g;
	const stbtt_fontinfo* fi = (bold && ttfBoldReady_) ? &ttfBoldInfo_ : &ttfRegularInfo_;
	float scale = stbtt_ScaleForPixelHeight(fi, (float)sizePx);

	int adv = 0, lsb = 0;
	stbtt_GetCodepointHMetrics(fi, (int)cp, &adv, &lsb);
	g.xadvance = (float)adv * scale;

	if (bold && !ttfBoldReady_)
		g.xadvance += 1.0f;

	int w = 0, h = 0, xoff = 0, yoff = 0;
	unsigned char* bmp = stbtt_GetCodepointBitmap(fi, 0, scale, (int)cp, &w, &h, &xoff, &yoff);
	if (bmp && w > 0 && h > 0)
	{
		g.w = w;
		g.h = h;
		g.xoff = xoff;
		g.yoff = yoff;
		g.alpha.assign(bmp, bmp + (size_t)(w * h));
		stbtt_FreeBitmap(bmp, nullptr);
	}

	if (glyphCache_.size() > 2048)
		glyphCache_.clear();

	auto ins = glyphCache_.emplace(key, std::move(g));
	return ins.first->second;
}

int TextEngine::sheetPixelPos(int x, int y) const
{
	return fontdecode::sheetPixelPos(x, y, sheetWidth_);
}

u8 TextEngine::alphaAt(int glyphIndex, int cellX, int cellY) const
{
	if (cellX < 0 || cellY < 0 || cellX >= cellWidth_ || cellY >= cellHeight_)
		return 0;

	const int sheetIndex = (charPerSheet_ > 0) ? (glyphIndex / charPerSheet_) : 0;
	const int slot = (charPerSheet_ > 0) ? (glyphIndex % charPerSheet_) : 0;
	const int slotLine = (nRows_ > 0) ? (slot / nRows_) : 0;
	const int slotRow = (nRows_ > 0) ? (slot % nRows_) : 0;

	const int originX = slotRow * (cellWidth_ + 1) + 1;
	const int originY = slotLine * (cellHeight_ + 1) + 1;

	const int x = originX + cellX;
	const int y = originY + cellY;
	if (x >= sheetWidth_ || y >= sheetHeight_)
		return 0;

	const u8* sheet = sheetData_ + (size_t)sheetIndex * (size_t)sheetSize_;
	const int pos = fontdecode::sheetPixelPos(x, y, sheetWidth_);
	if (pos < 0 || (u32)pos >= sheetSize_ * 2u)
		return 0;
	return fontdecode::alphaAtPixelPos(sheet, pos, sheetFmt_);
}

int TextEngine::resolveGlyph(u32 codePoint) const
{
	int gi = fontGlyphIndexFromCodePoint(font_, codePoint);
	if (gi < 0) gi = fallback_;

	const int maxGlyph = (charPerSheet_ > 0 && nSheets_ > 0) ?
		charPerSheet_ * nSheets_ - 1 : 0;
	if (gi > maxGlyph) gi = fallback_;

	if (gi < glyphMin_) glyphMin_ = gi;
	if (gi > glyphMax_) glyphMax_ = gi;
	const int sheet = (charPerSheet_ > 0) ? (gi / charPerSheet_) : 0;
	if (sheet > sheetMax_) sheetMax_ = sheet;
	return gi;
}

float TextEngine::measure(const std::string& utf8, float userScale) const
{
	if (ttfLoaded_)
	{
		float pixelHeight = 24.0f * userScale;
		float scale = stbtt_ScaleForPixelHeight(&ttfRegularInfo_, pixelHeight);

		float total = 0.0f;
		size_t i = 0;
		while (i < utf8.size())
		{
			uint32_t cp;
			int n = utf8Decode(utf8.c_str() + i, cp);
			if (n == 0) break;
			i += n;

			int adv = 0, lsb = 0;
			stbtt_GetCodepointHMetrics(&ttfRegularInfo_, (int)cp, &adv, &lsb);
			total += (float)adv * scale;
		}
		return total;
	}

	if (!font_) return 0.0f;
	float total = 0.0f;
	size_t i = 0;
	while (i < utf8.size())
	{
		uint32_t cp;
		int n = utf8Decode(utf8.c_str() + i, cp);
		if (n == 0) break;
		i += n;
		const int gi = resolveGlyph(cp);
		fontGlyphPos_s pos;
		fontCalcGlyphPos(&pos, font_, gi, 0, 1.0f, 1.0f);
		total += pos.xAdvance * kAdvanceFactor;
	}
	return total * sTextScale_ * userScale;
}

std::vector<std::pair<size_t, size_t>> TextEngine::splitWords(const std::string& utf8)
{
	std::vector<std::pair<size_t, size_t>> words;
	size_t i = 0, ws = 0;
	while (i < utf8.size())
	{
		char c = utf8[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		{
			if (ws < i) words.push_back({ ws, i - ws });
			if (c == ' ' && i == ws) words.push_back({ i, 1 });
			i++;
			ws = i;
		}
		else
		{
			uint32_t cp; int n = utf8Decode(utf8.c_str() + i, cp);
			if (n == 0) break;
			i += n;
		}
	}
	if (ws < utf8.size()) words.push_back({ ws, utf8.size() - ws });
	return words;
}

float TextEngine::drawLine(Bitmap& bmp, const std::string& utf8, int x, int y,
	float userScale, uint32_t color, bool bold, bool underline) const
{
	if (!bmp.px) return 0.0f;

	if (ttfLoaded_)
	{
		float pixelHeight = 24.0f * userScale;
		int sizePx = (int)roundf(pixelHeight);
		if (sizePx < 8) sizePx = 8;
		float scale = stbtt_ScaleForPixelHeight(&ttfRegularInfo_, pixelHeight);
		const uint8_t colorAlpha = (uint8_t)(color >> 24);

		int baselineY = y + (int)ceilf((float)ttfAscent_ * scale);
		float pen = 0.0f;

		size_t i = 0;
		while (i < utf8.size())
		{
			uint32_t cp;
			int n = utf8Decode(utf8.c_str() + i, cp);
			if (n == 0) break;
			i += n;

			const CachedGlyph& g = getGlyph(cp, sizePx, bold);
			if (g.w > 0 && g.h > 0)
			{
				int dstX = x + (int)floorf(pen) + g.xoff;
				int dstY = baselineY + g.yoff;

				for (int gy = 0; gy < g.h; gy++)
				{
					int sy = dstY + gy;
					if (sy < bmp.originY || sy >= bmp.originY + bmp.h) continue;

					const uint8_t* src = g.alpha.data() + gy * g.w;
					uint16_t* dst = bmp.px +
						(size_t)(sy - bmp.originY) * bmp.w;

					for (int gx = 0; gx < g.w; gx++)
					{
						int sx = dstX + gx;
						if (sx < 0 || sx >= bmp.w) continue;

						uint8_t a = src[gx];
						if (a)
						{
							if (colorAlpha != 0xFF)
								a = (uint8_t)((a * colorAlpha + 127) / 255);
							Bitmap::blendPixel(dst[sx], color, a);
							if (bold && !ttfBoldReady_ && sx + 1 < bmp.w)
							{
								Bitmap::blendPixel(dst[sx + 1], color, (uint8_t)(a >> 1));
							}
						}
					}
				}
			}
			pen += g.xadvance;
		}

		if (underline && pen > 0.0f)
		{
			int uy = baselineY + 2;
			if (uy >= bmp.h) uy = bmp.h - 1;
			if (colorAlpha == 0xFF) bmp.fillRect(x, uy, (int)ceilf(pen), 1, color);
			else bmp.blendRect(x, uy, (int)ceilf(pen), 1, color);
		}
		return pen;
	}

	if (!font_) return 0.0f;

	const float scale = sTextScale_ * userScale;
	if (scale <= 0.0f) return 0.0f;
	const u8 colorAlpha = (u8)(color >> 24);
	float pen = 0.0f;
	const int drawnHeight = (int)ceilf((float)cellHeight_ * scale);

	size_t i = 0;
	while (i < utf8.size())
	{
		uint32_t cp;
		int n = utf8Decode(utf8.c_str() + i, cp);
		if (n == 0) break;
		i += n;

		const int gi = resolveGlyph(cp);

		fontGlyphPos_s pos;
		fontCalcGlyphPos(&pos, font_, gi, 0, 1.0f, 1.0f);

		const int glyphW = (int)pos.width;
		if (glyphW > 0 && glyphW <= cellWidth_)
		{
			const int dstX = x + (int)floorf((pen + pos.xOffset) * scale);
			const int dstW = (int)ceilf((float)glyphW * scale);
			for (int dy = 0; dy < drawnHeight; dy++)
			{
				const int sy = y + dy;
				if (sy < bmp.originY || sy >= bmp.originY + bmp.h) continue;
				const int cellY = (int)((float)dy / scale);
				if (cellY >= cellHeight_) continue;

				for (int dx = 0; dx < dstW; dx++)
				{
					const int sx = dstX + dx;
					if (sx < 0 || sx >= bmp.w) continue;
					const int cellX = (int)((float)dx / scale);
					if (cellX >= glyphW) continue;

					const u8 a = alphaAt(gi, cellX, cellY);
					if (!a) continue;

					uint16_t& d = bmp.px[
						(size_t)(sy - bmp.originY) * bmp.w + sx];
					const u8 coverage = colorAlpha == 0xFF ? a :
						(u8)((a * colorAlpha + 127) / 255);
					Bitmap::blendPixel(d, color, coverage);
					if (bold && sx + 1 < bmp.w)
						Bitmap::blendPixel(bmp.px[
							(size_t)(sy - bmp.originY) * bmp.w + sx + 1],
							color, (u8)(coverage >> 1));
				}
			}
		}
		pen += pos.xAdvance * kAdvanceFactor;
	}

	const float totalW = pen * scale;
	if (underline && totalW > 0.0f)
	{
		const int uy = y + drawnHeight - 1;
		if (colorAlpha == 0xFF) bmp.fillRect(x, uy, (int)ceilf(totalW), 1, color);
		else bmp.blendRect(x, uy, (int)ceilf(totalW), 1, color);
	}
	return totalW;
}

u8 TextEngine::glyphAlphaAt(int glyphIndex, int cellX, int cellY) const
{
	if (ttfLoaded_)
	{
		const CachedGlyph& g = getGlyph((uint32_t)glyphIndex, 24, false);
		if (cellX < 0 || cellY < 0 || cellX >= g.w || cellY >= g.h) return 0;
		return g.alpha[(size_t)cellY * g.w + cellX];
	}
	return alphaAt(glyphIndex, cellX, cellY);
}

int TextEngine::glyphIndexOfCodePoint(uint32_t codePoint) const
{
	if (ttfLoaded_) return (int)codePoint;
	if (!font_) return -1;
	int gi = fontGlyphIndexFromCodePoint(font_, codePoint);
	return gi < 0 ? -1 : gi;
}

float TextEngine::glyphAdvance(int glyphIndex) const
{
	if (ttfLoaded_)
	{
		int adv = 0, lsb = 0;
		stbtt_GetCodepointHMetrics(&ttfRegularInfo_, glyphIndex, &adv, &lsb);
		float scale = stbtt_ScaleForPixelHeight(&ttfRegularInfo_, 24.0f);
		return (float)adv * scale;
	}
	if (!font_) return 0.0f;
	fontGlyphPos_s pos;
	fontCalcGlyphPos(&pos, font_, glyphIndex, 0, 1.0f, 1.0f);
	return pos.xAdvance;
}

float TextEngine::glyphInkWidth(int glyphIndex) const
{
	if (ttfLoaded_)
	{
		const CachedGlyph& g = getGlyph((uint32_t)glyphIndex, 24, false);
		return (float)g.w;
	}
	if (!font_) return 0.0f;
	fontGlyphPos_s pos;
	fontCalcGlyphPos(&pos, font_, glyphIndex, 0, 1.0f, 1.0f);
	return pos.width;
}

float TextEngine::glyphLeftBearing(int glyphIndex) const
{
	if (ttfLoaded_)
	{
		int adv = 0, lsb = 0;
		stbtt_GetCodepointHMetrics(&ttfRegularInfo_, glyphIndex, &adv, &lsb);
		float scale = stbtt_ScaleForPixelHeight(&ttfRegularInfo_, 24.0f);
		return (float)lsb * scale;
	}
	if (!font_) return 0.0f;
	fontGlyphPos_s pos;
	fontCalcGlyphPos(&pos, font_, glyphIndex, 0, 1.0f, 1.0f);
	return pos.xOffset;
}
