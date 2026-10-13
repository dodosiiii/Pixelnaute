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
	uint16_t* px = nullptr;

	Bitmap() = default;
	Bitmap(const Bitmap&) = delete;
	Bitmap& operator=(const Bitmap&) = delete;
	Bitmap(Bitmap&& o) noexcept : w(o.w), h(o.h), px(o.px) { o.w = o.h = 0; o.px = nullptr; }
	Bitmap& operator=(Bitmap&& o) noexcept
	{
		if (this == &o) return *this;
		destroy();
		w = o.w; h = o.h; px = o.px;
		o.w = o.h = 0; o.px = nullptr;
		return *this;
	}

	bool create(int width, int height)
	{
		destroy();
		w = width;
		h = height;
		if (width <= 0 || height <= 0 || height > 20000) return false;
		px = (uint16_t*)malloc((size_t)width * height * 2);
		if (!px) { w = h = 0; return false; }
		return true;
	}

	void destroy()
	{
		if (px) { free(px); px = nullptr; }
		w = h = 0;
	}

	// Conversion couleur 0xAABBGGRR -> RGB565
	static inline uint16_t rgb32To565(uint32_t c)
	{
		uint8_t r = (c >> 0) & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
		return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	}

	inline void setPixel(int x, int y, uint16_t v)
	{
		if (x < 0 || y < 0 || x >= w || y >= h) return;
		px[(size_t)y * w + x] = v;
	}

	inline uint16_t getPixel(int x, int y) const
	{
		if (x < 0 || y < 0 || x >= w || y >= h) return 0;
		return px[(size_t)y * w + x];
	}

	void clear(uint32_t color);
	void fillRect(int x, int y, int rw, int rh, uint32_t color);
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