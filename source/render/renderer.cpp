#include "renderer.hpp"
#include "morton.hpp"
#include "image_fit.hpp"
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include <math.h>

// Le dump du tampon d'affichage vers la SD servait au diagnostic : il ecrivait
// 245 Ko a chaque image et ralentissait fortement le navigateur. Desactive.
// #define DEBUG_DUMP_TOP 1

// La texture fait exactement la taille de la fenetre affichee (400x240).
//
// Avec une texture plus grande (512x256) et une sous-region, le rendu etait
// etire verticalement : la correspondance entre les texels et les pixels de
// l'ecran n'etait pas 1:1. En alignant la texture sur la fenetre, la relation
// devient exacte et il n'y a plus d'ambiguite de sous-region.
#define TEX_W 512
#define TEX_H 256

// Fenetre effectivement affichee (ecran du haut).
#define VIEW_W 400
#define VIEW_H 240

bool PageRenderer::init()
{
	if (!C3D_TexInit(&tex, TEX_W, TEX_H, GPU_RGB565)) return false;
	texData = linearAlloc(TEX_W * TEX_H * 2);
	if (!texData)
	{
		C3D_TexDelete(&tex);
		return false;
	}
	memset(texData, 0, TEX_W * TEX_H * 2);
	C3D_TexUpload(&tex, texData);
	C3D_TexSetFilter(&tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	// Region 400x240 dans une texture 512x256.
	//
	// Convention PICA200 (celle attendue par Citro2D) : v = 1.0 correspond a la
	// PREMIERE ligne de la memoire texture, c'est-a-dire le haut de l'image, et
	// v DECROIT vers le bas. Une sous-region situee en haut de la texture a donc
	// top = 1.0 et bottom < 1.0.
	//
	// Avec top = 0 et bottom = hauteur/textureHauteur, la region lue est
	// degeneree : c'est ce qui produisait un affichage brouille et colore.
	subtex_.width = 400;
	subtex_.height = 240;
	subtex_.left = 0.0f;
	subtex_.right = 400.0f / TEX_W;
	subtex_.top = 1.0f;
	subtex_.bottom = 1.0f - 240.0f / TEX_H;

	image.tex = &tex;
	image.subtex = &subtex_;
	texReady = true;
	return true;
}

void PageRenderer::shutdown()
{
	if (texData) { linearFree(texData); texData = nullptr; }
	if (texReady) { C3D_TexDelete(&tex); texReady = false; }
}

// Copie la fenetre [scrollY, scrollY+240) dans la texture avec formatage 8x8 Morton tile.
void PageRenderer::upload(const PageModel& page, int scrollY, int scrollX,
	float scale)
{
	if (!texReady || !page.bmp.px) return;
	layoutW_ = page.bmp.w;
	layoutH_ = page.height;
	const int viewHeight = (int)ceilf((float)VIEW_H * scale);

	int y0 = scrollY;
	if (y0 < 0) y0 = 0;
	if (y0 > layoutH_ - viewHeight) y0 = layoutH_ - viewHeight;
	if (y0 < 0) y0 = 0;
	int x0 = scrollX;
	const int viewWidth = (int)ceilf((float)VIEW_W * scale);
	if (x0 < 0) x0 = 0;
	if (x0 > layoutW_ - viewWidth) x0 = layoutW_ - viewWidth;
	if (x0 < 0) x0 = 0;

	uint16_t* dst = (uint16_t*)texData;
	const uint16_t* src = page.bmp.px;

	int tilesX = TEX_W / 8;
	const uint16_t pageBackground = src[0];
	for (int r = 0; r < TEX_H; ++r)
		for (int c = 0; c < TEX_W; ++c)
			dst[mortonTileIndex(c, r, tilesX)] = pageBackground;

	for (int r = 0; r < VIEW_H; r++)
	{
		const int logicalY = y0 + (int)((float)r * scale);
		const int sourceY = logicalY - page.bmp.originY;
		if (sourceY < 0 || sourceY >= page.bmp.h) continue;
		const uint16_t* srow = src + (size_t)sourceY * layoutW_;
		for (int c = 0; c < VIEW_W; c++)
		{
			const int logicalX = std::min(layoutW_ - 1,
				x0 + (int)((float)c * scale));
			dst[mortonTileIndex(c, r, tilesX)] = srow[logicalX];
		}
	}

	dirty_ = false;
	lastScroll_ = y0;
	lastScrollX_ = x0;

#ifdef DEBUG_DUMP_TOP
	FILE* fd = fopen("sdmc:/N3DSWeb/.toptex.bin", "wb");
	if (fd)
	{
		fwrite(texData, 1, TEX_W * 240 * 2, fd);
		fclose(fd);
	}
#endif
}

//--------------------------------------------------------------------------
// Rendu (appele une fois par frame, apres C2D_SceneBegin de l'ecran haut)
//--------------------------------------------------------------------------
void PageRenderer::draw(const PageModel& page, int scrollY, int scrollX,
	int zoomPercent)
{
	if (!texReady) return;
	if (page.height <= 0) return;
	if (zoomPercent < 100) zoomPercent = 100;
	if (zoomPercent > 300) zoomPercent = 300;
	const float scale =
		(float)(page.bmp.w > 0 ? page.bmp.w : NW_PAGE_WIDTH) * 100.0f /
		((float)VIEW_W * (float)zoomPercent);
	const int viewHeight = (int)ceilf((float)VIEW_H * scale);
	int boundedScroll = scrollY;
	if (boundedScroll < 0) boundedScroll = 0;
	if (boundedScroll > page.height - viewHeight) boundedScroll = page.height - viewHeight;
	if (boundedScroll < 0) boundedScroll = 0;
	const int viewWidth = (int)ceilf((float)VIEW_W * scale);
	const int maxScrollX = std::max(0, page.bmp.w - viewWidth);
	const int boundedScrollX = std::max(0, std::min(scrollX, maxScrollX));

	if (dirty_ || boundedScroll != lastScroll_ ||
		boundedScrollX != lastScrollX_ || layoutH_ != page.height ||
		layoutW_ != page.bmp.w)
	{
		upload(page, boundedScroll, boundedScrollX, scale);
		drawImages(page, boundedScroll, boundedScrollX, scale);
		drawFixedBoxes(page, boundedScrollX, scale);
		C3D_TexUpload(&tex, texData);
	}

	C2D_DrawParams params;
	memset(&params, 0, sizeof(params));
	params.pos.x = 0.0f;
	params.pos.y = 0.0f;
	params.pos.w = (float)VIEW_W;
	params.pos.h = (float)VIEW_H;
	params.depth = 0.0f;

	C2D_DrawImage(image, &params, NULL);
}

void PageRenderer::drawFixedBoxes(const PageModel& page, int scrollX,
	float scale)
{
	if (page.fixedBoxes.empty() || !page.bmp.px || !texData) return;
	if (scale <= 0.0f) return;
	const int segmentStart = page.segmentIndex * NW_PAGE_SEGMENT_HEIGHT;
	const int tilesX = TEX_W / 8;
	const uint16_t* src = page.bmp.px;
	uint16_t* dst = (uint16_t*)texData;

	for (const FixedBox& box : page.fixedBoxes)
	{
		const int screenX0 = std::max(0,
			(int)floorf((box.x - scrollX) / scale));
		const int screenX1 = std::min(VIEW_W,
			(int)ceilf((box.x + box.w - scrollX) / scale));
		const int screenY0 = std::max(0,
			(int)floorf((box.y - segmentStart) / scale));
		const int screenY1 = std::min(VIEW_H,
			(int)ceilf((box.y + box.h - segmentStart) / scale));
		for (int y = screenY0; y < screenY1; ++y)
		{
			const int logicalY = box.y + (int)((y - screenY0) * scale);
			const int sourceY = logicalY - page.bmp.originY;
			if (sourceY < 0 || sourceY >= page.bmp.h) continue;
			const uint16_t* sourceRow = src + (size_t)sourceY * page.bmp.w;
			for (int x = screenX0; x < screenX1; ++x)
			{
				const int logicalX = scrollX + (int)(x * scale);
				if (logicalX < 0 || logicalX >= page.bmp.w) continue;
				dst[mortonTileIndex(x, y, tilesX)] = sourceRow[logicalX];
			}
		}
	}
}

//--------------------------------------------------------------------------
// Dessine les images de la page.
//--------------------------------------------------------------------------
void PageRenderer::drawImages(const PageModel& page, int scrollY, int scrollX,
	float scale)
{
	if (!images_ || page.images.empty() || !texReady || !texData) return;

	int tilesX = TEX_W / 8;
	uint16_t* dst = (uint16_t*)texData;

	for (const auto& box : page.images)
	{
		const StoredImage* img = images_->find(box.src);
		if (!img || !img->valid || img->pixels.empty()) continue;
		if (box.w <= 0 || box.h <= 0 || img->width <= 0 || img->height <= 0) continue;

		ImageFitRect fitRect;
		if (!calculateImageFitRect(box.w, box.h, img->width, img->height,
			box.fit, box.positionX, box.positionY, fitRect))
			continue;

		int row0 = (int)floorf((float)(box.y - scrollY) / scale);
		int row1 = (int)ceilf((float)(box.y + box.h - scrollY) / scale);
		if (row1 <= 0 || row0 >= VIEW_H) continue;
		if (row0 < 0) row0 = 0;
		if (row1 > VIEW_H) row1 = VIEW_H;

		int col0 = (int)floorf((float)(box.x - scrollX) / scale);
		int col1 = (int)ceilf((float)(box.x + box.w - scrollX) / scale);
		if (col0 < 0) col0 = 0;
		if (col1 > VIEW_W) col1 = VIEW_W;
		if (col1 <= col0) continue;

		for (int row = row0; row < row1; row++)
		{
			const int boxY = (int)((float)row * scale) + scrollY - box.y;
			if ((float)boxY < fitRect.destY ||
				(float)boxY >= fitRect.destY + fitRect.destH)
				continue;
			const int srcY = box.fit == CssImageFitFill
				? boxY * img->height / box.h
				: (int)floorf(fitRect.sourceY +
					((float)boxY - fitRect.destY) * fitRect.sourceH /
					fitRect.destH);
			if (srcY < 0 || srcY >= img->height) continue;

			for (int col = col0; col < col1; col++)
			{
				const int boxX = scrollX + (int)((float)col * scale) - box.x;
				if ((float)boxX < fitRect.destX ||
					(float)boxX >= fitRect.destX + fitRect.destW)
					continue;
				const int srcX = box.fit == CssImageFitFill
					? boxX * img->width / box.w
					: (int)floorf(fitRect.sourceX +
						((float)boxX - fitRect.destX) * fitRect.sourceW /
						fitRect.destW);
				if (srcX < 0 || srcX >= img->width) continue;

				uint16_t px = img->pixels[(size_t)srcY * img->width + srcX];
				dst[mortonTileIndex(col, row, tilesX)] = px;
			}
		}
	}
}
