#include "bitmap.hpp"
#include <algorithm>
#include <string.h>
#include <math.h>

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
	int y0 = y < originY ? originY : y;
	int x1 = x + rw; if (x1 > w) x1 = w;
	int y1 = y + rh; if (y1 > originY + h) y1 = originY + h;
	if (x1 <= x0 || y1 <= y0) return;
	uint16_t v = rgbTo565(color);
	for (int yy = y0; yy < y1; yy++)
	{
		uint16_t* row = px + (size_t)(yy - originY) * w;
		for (int xx = x0; xx < x1; xx++) row[xx] = v;
	}
}

static bool roundedRectContains(int px, int py, int x, int y, int w, int h, int radius)
{
	if (px < x || py < y || px >= x + w || py >= y + h) return false;
	if (radius <= 0) return true;
	const int cx = px < x + radius ? x + radius - 1 :
		(px >= x + w - radius ? x + w - radius : px);
	const int cy = py < y + radius ? y + radius - 1 :
		(py >= y + h - radius ? y + h - radius : py);
	const int dx = px - cx, dy = py - cy;
	return dx * dx + dy * dy <= radius * radius;
}

void Bitmap::fillRoundedRect(int x, int y, int rw, int rh, int radius, uint32_t color)
{
	if (!px || rw <= 0 || rh <= 0) return;
	radius = std::max(0, std::min(radius, std::min(rw, rh) / 2));
	const int x0 = std::max(0, x);
	const int y0 = std::max(originY, y);
	const int x1 = std::min(w, x + rw);
	const int y1 = std::min(originY + h, y + rh);
	if (x1 <= x0 || y1 <= y0) return;
	const u8 alpha = (color >> 24) & 0xFF;
	for (int py = y0; py < y1; ++py)
	{
		uint16_t* row = px + (size_t)(py - originY) * w;
		for (int pxX = x0; pxX < x1; ++pxX)
			if (roundedRectContains(pxX, py, x, y, rw, rh, radius))
			{
				if (alpha == 0xFF) row[pxX] = rgbTo565(color);
				else blendPixel(row[pxX], color, alpha);
			}
	}
}

void Bitmap::addRoundedRect(int x, int y, int rw, int rh, int radius,
	uint32_t color, int thickness)
{
	if (!px || rw <= 0 || rh <= 0) return;
	thickness = std::max(1, thickness);
	radius = std::max(0, std::min(radius, std::min(rw, rh) / 2));
	const int x0 = std::max(0, x);
	const int y0 = std::max(originY, y);
	const int x1 = std::min(w, x + rw);
	const int y1 = std::min(originY + h, y + rh);
	if (x1 <= x0 || y1 <= y0) return;
	const int innerW = rw - thickness * 2;
	const int innerH = rh - thickness * 2;
	const int innerRadius = std::max(0, radius - thickness);
	const u8 alpha = (color >> 24) & 0xFF;
	for (int py = y0; py < y1; ++py)
	{
		uint16_t* row = px + (size_t)(py - originY) * w;
		for (int pxX = x0; pxX < x1; ++pxX)
			if (roundedRectContains(pxX, py, x, y, rw, rh, radius) &&
				(innerW <= 0 || innerH <= 0 ||
				 !roundedRectContains(pxX, py, x + thickness, y + thickness,
					innerW, innerH, innerRadius)))
			{
				if (alpha == 0xFF) row[pxX] = rgbTo565(color);
				else blendPixel(row[pxX], color, alpha);
			}
	}
}

void Bitmap::shadowRoundedRect(int x, int y, int rw, int rh, int radius,
	int offsetX, int offsetY, int blur, int spread, uint32_t color)
{
	if (!px || rw <= 0 || rh <= 0 || ((color >> 24) & 0xFF) == 0) return;
	blur = std::max(0, std::min(blur, 24));
	spread = std::max(-16, std::min(spread, 16));
	const int sx = x + offsetX - spread;
	const int sy = y + offsetY - spread;
	const int sw = rw + spread * 2;
	const int sh = rh + spread * 2;
	if (sw <= 0 || sh <= 0) return;
	const int sr = std::max(0, std::min(radius + spread, std::min(sw, sh) / 2));
	const int x0 = std::max(0, sx - blur);
	const int y0 = std::max(originY, sy - blur);
	const int x1 = std::min(w, sx + sw + blur);
	const int y1 = std::min(originY + h, sy + sh + blur);
	const int baseAlpha = (color >> 24) & 0xFF;
	const uint32_t rgb = color & 0x00FFFFFF;
	for (int py = y0; py < y1; ++py)
	{
		uint16_t* row = px + (size_t)(py - originY) * w;
		for (int pxX = x0; pxX < x1; ++pxX)
		{
			const float qx = fabsf((float)pxX + 0.5f -
				((float)sx + (float)sw * 0.5f)) -
				((float)sw * 0.5f - (float)sr);
			const float qy = fabsf((float)py + 0.5f -
				((float)sy + (float)sh * 0.5f)) -
				((float)sh * 0.5f - (float)sr);
			const float outsideX = std::max(0.0f, qx);
			const float outsideY = std::max(0.0f, qy);
			const float distance = std::min(std::max(qx, qy), 0.0f) +
				sqrtf(outsideX * outsideX + outsideY * outsideY) - (float)sr;
			if (distance > (float)blur) continue;
			const float coverage = blur > 0
				? std::max(0.0f, 1.0f - std::max(0.0f, distance) / (float)(blur + 1))
				: (distance <= 0.0f ? 1.0f : 0.0f);
			const u8 alpha = (u8)(baseAlpha * coverage + 0.5f);
			blendPixel(row[pxX], rgb, alpha);
		}
	}
}

void Bitmap::fillLinearGradientRect(int x, int y, int rw, int rh,
	const uint32_t* colors, const int* stops, int count, int angleDegrees, int radius)
{
	if (!px || rw <= 0 || rh <= 0 || !colors || !stops || count < 2)
		return;
	radius = std::max(0, std::min(radius, std::min(rw, rh) / 2));
	const int x0 = std::max(0, x);
	const int y0 = std::max(originY, y);
	const int x1 = std::min(w, x + rw);
	const int y1 = std::min(originY + h, y + rh);
	if (x1 <= x0 || y1 <= y0) return;

	const float radians = (float)angleDegrees * 0.01745329251994329577f;
	const float dx = sinf(radians);
	const float dy = -cosf(radians);
	const float extent = fabsf(dx) * (float)rw + fabsf(dy) * (float)rh;
	if (extent <= 0.0f)
	{
		fillRect(x, y, rw, rh, colors[count - 1]);
		return;
	}

	for (int py = y0; py < y1; ++py)
	{
		uint16_t* row = px + (size_t)(py - originY) * w;
		for (int pxX = x0; pxX < x1; ++pxX)
		{
			if (!roundedRectContains(pxX, py, x, y, rw, rh, radius)) continue;
			const float centeredX = (float)(pxX - x) + 0.5f - (float)rw * 0.5f;
			const float centeredY = (float)(py - y) + 0.5f - (float)rh * 0.5f;
			int position = (int)((0.5f +
				(centeredX * dx + centeredY * dy) / extent) * 10000.0f);
			if (position < stops[0]) position = stops[0];
			if (position > stops[count - 1]) position = stops[count - 1];

			int stop = 1;
			while (stop < count - 1 && position > stops[stop]) ++stop;
			const int span = stops[stop] - stops[stop - 1];
			const int amount = span > 0
				? ((position - stops[stop - 1]) * 255) / span : 255;
			const uint32_t first = colors[stop - 1];
			const uint32_t second = colors[stop];
			const int r = (((int)(first & 0xFF) * (255 - amount)) +
				((int)(second & 0xFF) * amount)) / 255;
			const int g = (((int)((first >> 8) & 0xFF) * (255 - amount)) +
				((int)((second >> 8) & 0xFF) * amount)) / 255;
			const int b = (((int)((first >> 16) & 0xFF) * (255 - amount)) +
				((int)((second >> 16) & 0xFF) * amount)) / 255;
			const int a = (((int)(first >> 24) * (255 - amount)) +
				((int)(second >> 24) * amount)) / 255;
			const uint32_t color = (uint32_t)r | ((uint32_t)g << 8) |
				((uint32_t)b << 16) | ((uint32_t)a << 24);
			if (a >= 255) row[pxX] = rgbTo565(color);
			else blendPixel(row[pxX], color, (u8)a);
		}
	}
}

void Bitmap::fillRadialGradientRect(int x, int y, int rw, int rh,
	const uint32_t* colors, const int* stops, int count, int radius)
{
	if (!px || rw <= 0 || rh <= 0 || !colors || !stops || count < 2)
		return;
	radius = std::max(0, std::min(radius, std::min(rw, rh) / 2));
	const int x0 = std::max(0, x);
	const int y0 = std::max(originY, y);
	const int x1 = std::min(w, x + rw);
	const int y1 = std::min(originY + h, y + rh);
	if (x1 <= x0 || y1 <= y0) return;
	const float centerX = (float)x + (float)rw * 0.5f;
	const float centerY = (float)y + (float)rh * 0.5f;
	const float radiusX = std::max(1.0f, (float)rw * 0.5f);
	const float radiusY = std::max(1.0f, (float)rh * 0.5f);
	const float cornerDistance = 1.41421356237f;

	for (int py = y0; py < y1; ++py)
	{
		uint16_t* row = px + (size_t)(py - originY) * w;
		for (int pxX = x0; pxX < x1; ++pxX)
		{
			if (!roundedRectContains(pxX, py, x, y, rw, rh, radius)) continue;
			const float dx = ((float)pxX + 0.5f - centerX) / radiusX;
			const float dy = ((float)py + 0.5f - centerY) / radiusY;
			int position = (int)(sqrtf(dx * dx + dy * dy) /
				cornerDistance * 10000.0f);
			position = std::max(stops[0], std::min(stops[count - 1], position));
			int stop = 1;
			while (stop < count - 1 && position > stops[stop]) ++stop;
			const int span = stops[stop] - stops[stop - 1];
			const int amount = span > 0
				? ((position - stops[stop - 1]) * 255) / span : 255;
			const uint32_t first = colors[stop - 1];
			const uint32_t second = colors[stop];
			const int r = (((int)(first & 0xFF) * (255 - amount)) +
				((int)(second & 0xFF) * amount)) / 255;
			const int g = (((int)((first >> 8) & 0xFF) * (255 - amount)) +
				((int)((second >> 8) & 0xFF) * amount)) / 255;
			const int b = (((int)((first >> 16) & 0xFF) * (255 - amount)) +
				((int)((second >> 16) & 0xFF) * amount)) / 255;
			const int a = (((int)(first >> 24) * (255 - amount)) +
				((int)(second >> 24) * amount)) / 255;
			const uint32_t color = (uint32_t)r | ((uint32_t)g << 8) |
				((uint32_t)b << 16) | ((uint32_t)a << 24);
			if (a >= 255) row[pxX] = rgbTo565(color);
			else blendPixel(row[pxX], color, (u8)a);
		}
	}
}

void Bitmap::blendRect(int x, int y, int rw, int rh, uint32_t color)
{
	if (!px || rw <= 0 || rh <= 0) return;
	int x0 = x < 0 ? 0 : x;
	int y0 = y < originY ? originY : y;
	int x1 = x + rw; if (x1 > w) x1 = w;
	int y1 = y + rh; if (y1 > originY + h) y1 = originY + h;
	if (x1 <= x0 || y1 <= y0) return;
	u8 a = (color >> 24) & 0xFF;
	if (a == 0xFF) { fillRect(x0, y0, x1 - x0, y1 - y0, color); return; }
	for (int yy = y0; yy < y1; yy++)
	{
		uint16_t* row = px + (size_t)(yy - originY) * w;
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
	if (x < 0 || y < originY || x >= w || y >= originY + h) return;
	uint16_t& d = px[(size_t)(y - originY) * w + x];
	blendPixel(d, rgb, a);
}

void Bitmap::blit565(int dx, int dy, const uint16_t* src, int sw, int sh, int spitch)
{
	if (!src) return;
	for (int yy = 0; yy < sh; yy++)
	{
		int ty = dy + yy;
		if (ty < originY || ty >= originY + h) continue;
		const uint16_t* srow = src + (size_t)yy * spitch;
		uint16_t* drow = px + (size_t)(ty - originY) * w;
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