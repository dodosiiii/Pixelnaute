#pragma once
//=============================================================================
// bitmap.hpp - Bitmap RGB565 pour le rendu de page logiciel
//=============================================================================
#include "../common.hpp"
#include <algorithm>
#include <cstdint>

struct BitmapClipRect
{
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

class Bitmap
{
public:
	int w = 0;       // largeur logique (payload)
	int h = 0;       // hauteur (px) du buffer alloue
	int originY = 0; // premiere ligne logique stockee dans le buffer
	uint16_t* px = nullptr;
	BitmapClipRect clip;

	Bitmap() = default;
	Bitmap(const Bitmap&) = delete;
	Bitmap& operator=(const Bitmap&) = delete;
	Bitmap(Bitmap&& o) noexcept
		: w(o.w), h(o.h), originY(o.originY), px(o.px), clip(o.clip)
		{ o.w = o.h = o.originY = 0; o.px = nullptr; o.clip = {}; }
	Bitmap& operator=(Bitmap&& o) noexcept
	{
		if (this == &o) return *this;
		destroy();
		w = o.w; h = o.h; originY = o.originY; px = o.px;
		clip = o.clip;
		o.w = o.h = o.originY = 0; o.px = nullptr; o.clip = {};
		return *this;
	}

	bool create(int width, int height, int yOrigin = 0)
	{
		destroy();
		if (width <= 0 || width > 2048 || height <= 0 || height > 20000 || yOrigin < 0 ||
			(size_t)width > SIZE_MAX / sizeof(uint16_t) / (size_t)height)
		{
			return false;
		}
		const size_t bytes = (size_t)width * (size_t)height * sizeof(uint16_t);
		// Une seule surface ne doit jamais monopoliser le tas application.
		if (bytes > 8u * 1024u * 1024u) return false;
		w = width;
		h = height;
		originY = yOrigin;
		clip = { 0, yOrigin, width, yOrigin + height };
		px = (uint16_t*)malloc(bytes);
		if (!px) { w = h = originY = 0; clip = {}; return false; }
		return true;
	}

	void destroy()
	{
		if (px) { free(px); px = nullptr; }
		w = h = originY = 0;
		clip = {};
	}

	BitmapClipRect getClipRect() const { return clip; }

	void setClipRect(int x0, int y0, int x1, int y1)
	{
		clip.x0 = std::max(clip.x0, std::max(0, x0));
		clip.y0 = std::max(clip.y0, std::max(originY, y0));
		clip.x1 = std::min(clip.x1, std::min(w, x1));
		clip.y1 = std::min(clip.y1, std::min(originY + h, y1));
	}

	void restoreClipRect(const BitmapClipRect& saved)
	{
		clip.x0 = std::max(0, std::min(w, saved.x0));
		clip.y0 = std::max(originY, std::min(originY + h, saved.y0));
		clip.x1 = std::max(clip.x0, std::min(w, saved.x1));
		clip.y1 = std::max(clip.y0, std::min(originY + h, saved.y1));
	}

	inline bool isPixelVisible(int x, int y) const
	{
		return x >= clip.x0 && x < clip.x1 &&
			y >= clip.y0 && y < clip.y1;
	}

	// Conversion couleur 0xAABBGGRR -> RGB565
	static inline uint16_t rgb32To565(uint32_t c)
	{
		uint8_t r = (c >> 0) & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
		return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	}

	inline void setPixel(int x, int y, uint16_t v)
	{
		if (!isPixelVisible(x, y)) return;
		px[(size_t)(y - originY) * w + x] = v;
	}

	inline uint16_t getPixel(int x, int y) const
	{
		if (x < 0 || y < originY || x >= w || y >= originY + h) return 0;
		return px[(size_t)(y - originY) * w + x];
	}

	void clear(uint32_t color);
	void fillRect(int x, int y, int rw, int rh, uint32_t color);
	void fillRoundedRect(int x, int y, int rw, int rh, int radius, uint32_t color);
	void addRoundedRect(int x, int y, int rw, int rh, int radius,
		uint32_t color, int thickness);
	void shadowRoundedRect(int x, int y, int rw, int rh, int radius,
		int offsetX, int offsetY, int blur, int spread, uint32_t color);
	void fillLinearGradientRect(int x, int y, int rw, int rh,
		const uint32_t* colors, const int* stops, int count, int angleDegrees,
		int radius = 0);
	void fillRadialGradientRect(int x, int y, int rw, int rh,
		const uint32_t* colors, const int* stops, int count, int radius = 0);
	void addRect(int x, int y, int rw, int rh, uint32_t edge, int tw); // bordure
	void blendRect(int x, int y, int rw, int rh, uint32_t color);     // alpha
	void hline(int x, int x2, int y, uint32_t color);
	void vline(int x, int y, int y2, uint32_t color);

	// Passe un sprite (source RGB565 + alpha 0 ou 255 par pixel) avec transparence.
	void blit565(int dx, int dy, const uint16_t* src, int sw, int sh, int spitch);

	// Melange un pixel alpha (0-255) sur une couleur.
	void blendPixel16(int x, int y, uint32_t rgb, u8 a);

	// Melange sur une valeur RGB565 (utile a l'interieur des boucles).
	static inline void blendPixel(uint16_t& d, uint32_t rgb, u8 a)
	{
		if (a == 0) return;
		u8 r = (rgb >> 0) & 0xFF, g = (rgb >> 8) & 0xFF, b = (rgb >> 16) & 0xFF;
		u8 ia = (u8)(255 - a);
		u8 dr = (d >> 11) & 0x1F, dg = (d >> 5) & 0x3F, db = d & 0x1F;
		u8 nr = (u8)((r * a + (dr << 3) * ia) / 255);
		u8 ng = (u8)((g * a + (dg << 2) * ia) / 255);
		u8 nb = (u8)((b * a + (db << 3) * ia) / 255);
		d = (uint16_t)(((nr >> 3) << 11) | ((ng >> 2) << 5) | (nb >> 3));
	}
};
