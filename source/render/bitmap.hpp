#pragma once
//=============================================================================
// bitmap.hpp - Bitmap RGB565 pour le rendu de page logiciel
//=============================================================================
#include "../common.hpp"
#include <cstdint>

class Bitmap
{
public:
	int w = 0;       // largeur logique (payload)
	int h = 0;       // hauteur (px) du buffer alloue
	int originY = 0; // premiere ligne logique stockee dans le buffer
	uint16_t* px = nullptr;

	Bitmap() = default;
	Bitmap(const Bitmap&) = delete;
	Bitmap& operator=(const Bitmap&) = delete;
	Bitmap(Bitmap&& o) noexcept : w(o.w), h(o.h), originY(o.originY), px(o.px)
		{ o.w = o.h = o.originY = 0; o.px = nullptr; }
	Bitmap& operator=(Bitmap&& o) noexcept
	{
		if (this == &o) return *this;
		destroy();
		w = o.w; h = o.h; originY = o.originY; px = o.px;
		o.w = o.h = o.originY = 0; o.px = nullptr;
		return *this;
	}

	bool create(int width, int height, int yOrigin = 0)
	{
		destroy();
		if (width <= 0 || width > 1024 || height <= 0 || height > 20000 || yOrigin < 0 ||
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
		px = (uint16_t*)malloc(bytes);
		if (!px) { w = h = originY = 0; return false; }
		return true;
	}

	void destroy()
	{
		if (px) { free(px); px = nullptr; }
		w = h = originY = 0;
	}

	// Conversion couleur 0xAABBGGRR -> RGB565
	static inline uint16_t rgb32To565(uint32_t c)
	{
		uint8_t r = (c >> 0) & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
		return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	}

	inline void setPixel(int x, int y, uint16_t v)
	{
		if (x < 0 || y < originY || x >= w || y >= originY + h) return;
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
