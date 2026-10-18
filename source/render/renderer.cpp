#include "renderer.hpp"
#include "morton.hpp"
#include <string.h>
#include <malloc.h>
#include <stdio.h>

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
void PageRenderer::upload(const PageModel& page, int scrollY)
{
	if (!texReady || !page.bmp.px) return;
	layoutW_ = page.bmp.w;
	layoutH_ = page.height;

	int y0 = scrollY;
	if (y0 < 0) y0 = 0;
	if (y0 > layoutH_ - NW_TOP_HEIGHT) y0 = layoutH_ - NW_TOP_HEIGHT;
	if (y0 < 0) y0 = 0;

	memset(texData, 0, TEX_W * TEX_H * 2);
	uint16_t* dst = (uint16_t*)texData;
	const uint16_t* src = page.bmp.px;

	int rows = layoutH_ - y0;
	if (rows > NW_TOP_HEIGHT) rows = NW_TOP_HEIGHT;
	int cols = layoutW_ > NW_PAGE_WIDTH ? NW_PAGE_WIDTH : layoutW_;

	int tilesX = TEX_W / 8;
	for (int r = 0; r < rows; r++)
	{
		const uint16_t* srow = src + (size_t)(y0 + r) * layoutW_;
		for (int c = 0; c < cols; c++)
		{
			// Entrelacement 8x8 (voir morton.hpp) : x aux bits bas, y ensuite.
			dst[mortonTileIndex(c, r, tilesX)] = srow[c];
		}
	}

	dirty_ = false;
	lastScroll_ = y0;

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
void PageRenderer::draw(const PageModel& page, int scrollY)
{
	if (!texReady) return;
	if (page.height <= 0) return;

	if (dirty_ || scrollY != lastScroll_ || layoutH_ != page.height || layoutW_ != page.bmp.w)
	{
		upload(page, scrollY);
		drawImages(page, scrollY);
		C3D_TexUpload(&tex, texData);
	}

	C2D_DrawParams params;
	memset(&params, 0, sizeof(params));
	params.pos.x = 0;
	params.pos.y = 0;
	params.pos.w = 400;
	params.pos.h = 240;
	params.depth = 0.0f;

	C2D_DrawImage(image, &params, NULL);
}

//--------------------------------------------------------------------------
// Dessine les images de la page.
//--------------------------------------------------------------------------
void PageRenderer::drawImages(const PageModel& page, int scrollY)
{
	if (!images_ || page.images.empty() || !texReady || !texData) return;

	int tilesX = TEX_W / 8;
	uint16_t* dst = (uint16_t*)texData;

	for (const auto& box : page.images)
	{
		const StoredImage* img = images_->find(box.src);
		if (!img || !img->valid || img->pixels.empty()) continue;
		if (box.w <= 0 || box.h <= 0 || img->width <= 0 || img->height <= 0) continue;

		int row0 = box.y - scrollY;
		int row1 = row0 + box.h;
		if (row1 <= 0 || row0 >= VIEW_H) continue;
		if (row0 < 0) row0 = 0;
		if (row1 > VIEW_H) row1 = VIEW_H;

		int col0 = box.x;
		int col1 = box.x + box.w;
		if (col0 < 0) col0 = 0;
		if (col1 > VIEW_W) col1 = VIEW_W;
		if (col1 <= col0) continue;

		for (int row = row0; row < row1; row++)
		{
			const int boxY = (row + scrollY) - box.y;
			const int srcY = boxY * img->height / box.h;
			if (srcY < 0 || srcY >= img->height) continue;

			for (int col = col0; col < col1; col++)
			{
				const int boxX = col - box.x;
				const int srcX = boxX * img->width / box.w;
				if (srcX < 0 || srcX >= img->width) continue;

				uint16_t px = img->pixels[(size_t)srcY * img->width + srcX];
				dst[mortonTileIndex(col, row, tilesX)] = px;
			}
		}
	}
}
