//=============================================================================
// tests/gfx_probe/main.cpp - Sonde graphique
//
// Etape 1 (verifiee) : les aplats de couleur et un triangle s'affichent
// correctement. La couche C3D/C2D fonctionne donc sur cette machine.
//
// Etape 2 (cette version) : teste la couche TEXTURE, qui est la seule chose
// que le navigateur utilise en plus des aplats. Trois textures RGB565 sont
// affichees cote a cote sur l'ecran du haut :
//
//   - texture A : REMPLIE A PLAT, sans entrelacement (donc bruit si le GPU
//     attend un entrelacement) ;
//   - texture B : entrelacee en tuiles 8x8 (Morton), avec un damier lisible ;
//   - texture C : entrelacee en tuiles 8x8, avec des barres verticales.
//
// Lecture du resultat : celle des trois qui montre l'image attendue indique
// la convention correcte pour C3D_TexUpload dans cet environnement.
//=============================================================================
#include <3ds.h>
#include <citro2d.h>
#include <cstdio>
#include <cstring>
#include <malloc.h>

#define TEX_W 256
#define TEX_H 256

//--------------------------------------------------------------------------
// Entrelacement 8x8 (Morton) : x aux bits BAS, y ensuite.
// Voir source/render/morton.hpp pour l'explication.
//--------------------------------------------------------------------------
static inline int mortonIndex(int x, int y, int tilesX)
{
	const int tx = x >> 3;
	const int ty = y >> 3;
	const int lx = x & 7;
	const int ly = y & 7;
	const int within =
		(lx & 1)        | ((ly & 1) << 1) |
		((lx & 2) << 1) | ((ly & 2) << 2) |
		((lx & 4) << 2) | ((ly & 4) << 3);
	return (ty * tilesX + tx) * 64 + within;
}

//--------------------------------------------------------------------------
// Motif de test ASYMETRIQUE : un « L » plein, blanc sur fond noir.
//
// Un carre dans un coin ne prouve rien : il est symetrique par rapport a la
// diagonale et parait donc correct meme si l'entrelacement est transpose.
// La barre horizontale du « L », elle, devient une barre VERTICALE si les
// axes sont inverses : le verdict est sans ambiguite.
//
// La barre est volontairement longue (64 px, soit 8 tuiles) et epaisse de
// 12 px, pour qu'une transposition saute aux yeux.
//--------------------------------------------------------------------------
static void fillTexture(u16* dst, bool swizzled, int pattern)
{
	const int tilesX = TEX_W / 8;
	for (int y = 0; y < TEX_H; y++)
	{
		for (int x = 0; x < TEX_W; x++)
		{
			u16 color = 0x0000; // noir
			if (pattern == 0)
			{
				// « L » : barre horizontale en BAS + barre verticale a GAUCHE.
				// (en haut a gauche si l'orientation est correcte)
				if (y < 12 && x < 64) color = 0xFFFF;            // barre du haut
				if (x < 12 && y < 64) color = 0xFFFF;            // barre de gauche
			}
			else if (pattern == 1)
			{
				// Repere ponctuel en haut a gauche : petit carre de 8x8.
				if (x < 8 && y < 8) color = 0xF800;              // rouge
			}
			else
			{
				// Diagonal : doit rester une diagonale.
				if (x == y) color = 0x07E0;                      // vert
			}
			const int index = swizzled ? mortonIndex(x, y, tilesX)
			                           : (y * TEX_W + x);
			dst[index] = color;
		}
	}
}

int main()
{
	gfxInitDefault();

	auto note = [](const char* text) {
		FILE* f = fopen("sdmc:/gfxprobe.txt", "a");
		if (f) { fprintf(f, "%s\n", text); fclose(f); }
	};

	note("--- sonde texture ---");
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

	C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	//--- trois textures RGB565 ---
	C3D_Tex texLinear, texSwizzle, texBars;
	void* dataLinear = linearAlloc(TEX_W * TEX_H * 2);
	void* dataSwizzle = linearAlloc(TEX_W * TEX_H * 2);
	void* dataBars = linearAlloc(TEX_W * TEX_H * 2);

	bool ok = true;
	if (!dataLinear || !dataSwizzle || !dataBars) { note("linearAlloc ECHEC"); ok = false; }

	if (ok)
	{
		ok &= C3D_TexInit(&texLinear, TEX_W, TEX_H, GPU_RGB565);
		ok &= C3D_TexInit(&texSwizzle, TEX_W, TEX_H, GPU_RGB565);
		ok &= C3D_TexInit(&texBars, TEX_W, TEX_H, GPU_RGB565);
		note(ok ? "C3D_TexInit x3 ok" : "C3D_TexInit ECHEC");
	}

	if (ok)
	{
		memset(dataLinear, 0, TEX_W * TEX_H * 2);
		memset(dataSwizzle, 0, TEX_W * TEX_H * 2);
		memset(dataBars, 0, TEX_W * TEX_H * 2);

		fillTexture((u16*)dataLinear, false, 0);   // « L », sans entrelacement
		fillTexture((u16*)dataSwizzle, true, 0);   // « L », entrelace
		fillTexture((u16*)dataBars, true, 2);      // diagonale, entrelace

		C3D_TexUpload(&texLinear, dataLinear);
		C3D_TexUpload(&texSwizzle, dataSwizzle);
		C3D_TexUpload(&texBars, dataBars);
		note("textures remplies et envoyees");
	}

	// Sous-regions 128x128 prises dans le coin haut-gauche.
	// Convention PICA200 : v = 1.0 correspond a la PREMIERE ligne de la
	// memoire texture (le haut de l'image), et v decroit vers le bas.
	// Une sous-region du haut a donc top = 1.0 et bottom < 1.0.
	Tex3DS_SubTexture subA = { 128, 128, 0.0f, 1.0f, 128.0f / TEX_W,
		1.0f - 128.0f / TEX_H };
	Tex3DS_SubTexture subB = { 128, 128, 0.0f, 1.0f, 128.0f / TEX_W,
		1.0f - 128.0f / TEX_H };
	Tex3DS_SubTexture subC = { 128, 128, 0.0f, 1.0f, 128.0f / TEX_W,
		1.0f - 128.0f / TEX_H };
	C2D_Image imgA = { &texLinear, &subA };
	C2D_Image imgB = { &texSwizzle, &subB };
	C2D_Image imgC = { &texBars, &subC };

	int frames = 0;
	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		C2D_TargetClear(top, C2D_Color32(0x10, 0x10, 0x10, 0xFF));
		C2D_SceneBegin(top);
		{
			C2D_DrawParams p;
			memset(&p, 0, sizeof(p));
			p.pos.w = 128; p.pos.h = 128; p.depth = 0.0f;
			p.pos.x = 4;   p.pos.y = 40;  C2D_DrawImage(imgA, &p, NULL);
			p.pos.x = 136; p.pos.y = 40;  C2D_DrawImage(imgB, &p, NULL);
			p.pos.x = 268; p.pos.y = 40;  C2D_DrawImage(imgC, &p, NULL);
		}

		C2D_TargetClear(bottom, C2D_Color32(0x10, 0x10, 0x10, 0xFF));
		C2D_SceneBegin(bottom);
		{
			C2D_DrawParams p;
			memset(&p, 0, sizeof(p));
			p.pos.w = 128; p.pos.h = 128; p.depth = 0.0f;
			p.pos.x = 4;   p.pos.y = 40; C2D_DrawImage(imgB, &p, NULL);
			p.pos.x = 150; p.pos.y = 40; C2D_DrawImage(imgC, &p, NULL);
		}

		C3D_FrameEnd(0);
		frames++;
	}

	char buf[64];
	snprintf(buf, sizeof(buf), "images rendues: %d", frames);
	note(buf);
	note("fin");

	if (dataLinear) linearFree(dataLinear);
	if (dataSwizzle) linearFree(dataSwizzle);
	if (dataBars) linearFree(dataBars);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
