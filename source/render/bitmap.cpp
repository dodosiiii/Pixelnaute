#include "bitmap.hpp"
#include <string.h>

// Conversion 0xAABBGGRR -> RGB565
static uint16_t rgbTo565(uint32_t c)
{
	u8 r = (c >> 0) & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void Bitmap::clear(uint32_t color)
{
	if (!px) return;
	uint16_t v = rgbTo565(color);
	uint16_t* p = px;
	for (int i = 0; i < w * h; i++) p[i] = v;
}

void Bitmap::fillRect(int x, int y, int rw, int rh, uint32_t color)
{
	if (!px || rw <= 0 || rh <= 0) return;
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + rw; if (x1 > w) x1 = w;
	int y1 = y + rh; if (y1 > h) y1 = h;
	if (x1 <= x0 || y1 <= y0) return;
	uint16_t v = rgbTo565(color);
	for (int yy = y0; yy < y1; yy++)
	{
		uint16_t* row = px + (size_t)yy * w;
		for (int xx = x0; xx < x1; xx++) row[xx] = v;
	}
}

void Bitmap::blendRect(int x, int y, int rw, int rh, uint32_t color)
{
	if (!px || rw <= 0 || rh <= 0) return;
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + rw; if (x1 > w) x1 = w;
	int y1 = y + rh; if (y1 > h) y1 = h;
	if (x1 <= x0 || y1 <= y0) return;
	u8 a = (color >> 24) & 0xFF;
	if (a == 0xFF) { fillRect(x0, y0, x1 - x0, y1 - y0, color); return; }
	for (int yy = y0; yy < y1; yy++)
	{
		uint16_t* row = px + (size_t)yy * w;
		for (int xx = x0; xx < x1; xx++) blendPixel(row[xx], color, a);
	}
}

void Bitmap::addRect(int x, int y, int rw, int rh, uint32_t color, int tw)
{
	if (tw <= 0) tw = 1;
	fillRect(x, y, rw, tw, color);
	fillRect(x, y + rh - tw, rw, tw, color);
	fillRect(x, y, tw, rh, color);
	fillRect(x + rw - tw, y, tw, rh, color);
}

void Bitmap::hline(int x, int x2, int y, uint32_t color) { fillRect(x, y, x2 - x + 1, 1, color); }
void Bitmap::vline(int x, int y, int y2, uint32_t color) { fillRect(x, y, 1, y2 - y + 1, color); }

void Bitmap::blendPixel16(int x, int y, uint32_t rgb, u8 a)
{
	if (x < 0 || y < 0 || x >= w || y >= h) return;
	uint16_t& d = px[(size_t)y * w + x];
	blendPixel(d, rgb, a);
}

void Bitmap::blit565(int dx, int dy, const uint16_t* src, int sw, int sh, int spitch)
{
	if (!src) return;
	for (int yy = 0; yy < sh; yy++)
	{
		int ty = dy + yy;
		if (ty < 0 || ty >= h) continue;
		const uint16_t* srow = src + (size_t)yy * spitch;
		uint16_t* drow = px + (size_t)ty * w;
		for (int xx = 0; xx < sw; xx++)
		{
			int tx = dx + xx;
			if (tx < 0 || tx >= w) continue;
			uint16_t s = srow[xx];
			if (s == 0x0001) continue; // marqueur transparent
			drow[tx] = s;
		}
	}
}