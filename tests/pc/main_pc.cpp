//=============================================================================
// tests/pc/main_pc.cpp - Verification hors console recherche/CSS/police
//
// Le test valide les resolvers de recherche et CSS, puis le point le plus
// risque du moteur de texte : le decodage des feuilles de glyphes systeme
// (A4 + entrelacement 8x8). Il :
//   1. fabrique une feuille A4 entrelacee exactement comme la console ;
//   2. remplit des glyphes connus (petite police 5x7) ;
//   3. les relit via fontdecode (le code partage avec la 3DS) et compare ;
//   4. dessine une planche de controle en PNG pour inspection visuelle.
//
// Compilation (voir tests/pc/build_pc.ps1) puis execution : rend un PNG.
//=============================================================================
#include "3ds_stub.h"

#include "../../source/css/css.hpp"
#include "../../source/render/font_decode.hpp"
#include "../../source/search/search.hpp"
#include "../../source/search/search_page.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

//---------------------------------------------------------------------------
// Petite police 5x7 pour un jeu de caracteres reduit (lisibilite du test).
// Chaque glyphe : 7 lignes, 5 bits (bit 4 = colonne de gauche).
//---------------------------------------------------------------------------
struct GlyphDef { char c; unsigned char rows[7]; };

static const GlyphDef kGlyphs[] = {
	{ ' ', { 0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
	{ 'A', { 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 } },
	{ 'B', { 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E } },
	{ 'C', { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E } },
	{ 'D', { 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E } },
	{ 'E', { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F } },
	{ 'F', { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10 } },
	{ 'G', { 0x0E,0x11,0x10,0x17,0x11,0x11,0x0F } },
	{ 'H', { 0x11,0x11,0x11,0x1F,0x11,0x11,0x11 } },
	{ 'I', { 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E } },
	{ 'J', { 0x07,0x02,0x02,0x02,0x02,0x12,0x0C } },
	{ 'K', { 0x11,0x12,0x14,0x18,0x14,0x12,0x11 } },
	{ 'L', { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F } },
	{ 'M', { 0x11,0x1B,0x15,0x15,0x11,0x11,0x11 } },
	{ 'N', { 0x11,0x19,0x15,0x13,0x11,0x11,0x11 } },
	{ 'O', { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E } },
	{ 'P', { 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10 } },
	{ 'Q', { 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D } },
	{ 'R', { 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11 } },
	{ 'S', { 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E } },
	{ 'T', { 0x1F,0x04,0x04,0x04,0x04,0x04,0x04 } },
	{ 'U', { 0x11,0x11,0x11,0x11,0x11,0x11,0x0E } },
	{ 'V', { 0x11,0x11,0x11,0x11,0x11,0x0A,0x04 } },
	{ 'W', { 0x11,0x11,0x11,0x15,0x15,0x1B,0x11 } },
	{ 'X', { 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11 } },
	{ 'Y', { 0x11,0x11,0x0A,0x04,0x04,0x04,0x04 } },
	{ 'Z', { 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F } },
	{ 'a', { 0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F } },
	{ 'b', { 0x10,0x10,0x1E,0x11,0x11,0x11,0x1E } },
	{ 'c', { 0x00,0x00,0x0E,0x11,0x10,0x11,0x0E } },
	{ 'd', { 0x01,0x01,0x0F,0x11,0x11,0x11,0x0F } },
	{ 'e', { 0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E } },
	{ 'f', { 0x06,0x09,0x08,0x1C,0x08,0x08,0x08 } },
	{ 'g', { 0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E } },
	{ 'h', { 0x10,0x10,0x1E,0x11,0x11,0x11,0x11 } },
	{ 'i', { 0x04,0x00,0x0C,0x04,0x04,0x04,0x0E } },
	{ 'j', { 0x02,0x00,0x06,0x02,0x02,0x12,0x0C } },
	{ 'k', { 0x10,0x10,0x12,0x14,0x18,0x14,0x12 } },
	{ 'l', { 0x0C,0x04,0x04,0x04,0x04,0x04,0x0E } },
	{ 'm', { 0x00,0x00,0x1A,0x15,0x15,0x15,0x15 } },
	{ 'n', { 0x00,0x00,0x1E,0x11,0x11,0x11,0x11 } },
	{ 'o', { 0x00,0x00,0x0E,0x11,0x11,0x11,0x0E } },
	{ 'p', { 0x00,0x1E,0x11,0x11,0x1E,0x10,0x10 } },
	{ 'q', { 0x00,0x0F,0x11,0x11,0x0F,0x01,0x01 } },
	{ 'r', { 0x00,0x00,0x16,0x19,0x10,0x10,0x10 } },
	{ 's', { 0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E } },
	{ 't', { 0x08,0x08,0x1C,0x08,0x08,0x09,0x06 } },
	{ 'u', { 0x00,0x00,0x11,0x11,0x11,0x13,0x0D } },
	{ 'v', { 0x00,0x00,0x11,0x11,0x11,0x0A,0x04 } },
	{ 'w', { 0x00,0x00,0x11,0x11,0x15,0x15,0x0A } },
	{ 'x', { 0x00,0x00,0x11,0x0A,0x04,0x0A,0x11 } },
	{ 'y', { 0x00,0x11,0x11,0x11,0x0F,0x01,0x0E } },
	{ 'z', { 0x00,0x00,0x1F,0x02,0x04,0x08,0x1F } },
	{ '0', { 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E } },
	{ '1', { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E } },
	{ '2', { 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F } },
	{ '3', { 0x1F,0x02,0x04,0x02,0x01,0x11,0x0E } },
	{ '4', { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 } },
	{ '5', { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E } },
	{ '6', { 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E } },
	{ '7', { 0x1F,0x01,0x02,0x04,0x08,0x08,0x08 } },
	{ '8', { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E } },
	{ '9', { 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C } },
	{ '.', { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C } },
	{ ',', { 0x00,0x00,0x00,0x00,0x0C,0x04,0x08 } },
	{ ':', { 0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00 } },
	{ '-', { 0x00,0x00,0x00,0x1F,0x00,0x00,0x00 } },
	{ '/', { 0x01,0x02,0x02,0x04,0x08,0x08,0x10 } },
	{ '!', { 0x04,0x04,0x04,0x04,0x04,0x00,0x04 } },
	{ '?', { 0x0E,0x11,0x01,0x06,0x04,0x00,0x04 } },
	{ '(', { 0x02,0x04,0x08,0x08,0x08,0x04,0x02 } },
	{ ')', { 0x08,0x04,0x02,0x02,0x02,0x04,0x08 } },
	{ 'e', { 0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E } },
	{ '3', { 0x1F,0x02,0x04,0x02,0x01,0x11,0x0E } },
	{ 'D', { 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E } },
	{ 'S', { 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E } },
	{ 'W', { 0x11,0x11,0x11,0x15,0x15,0x1B,0x11 } },
	{ 'b', { 0x10,0x10,0x1E,0x11,0x11,0x11,0x1E } },
};

static const int kGlyphCount = (int)(sizeof(kGlyphs) / sizeof(kGlyphs[0]));

//---------------------------------------------------------------------------
// Geometrie de la feuille : valeurs reelles de la police systeme New 3DS.
//---------------------------------------------------------------------------
static const int CELL_W  = 24;
static const int CELL_H  = 30;
static const int SHEET_W = 128;
static const int SHEET_H = 32;
static const int N_ROWS  = 5;    // 5 colonnes de 25 px = 125 <= 128
static const int N_LINES = 1;
static const int SHEET_FMT = 0xB; // GPU_A4
static const int PER_SHEET = N_ROWS * N_LINES;
static const int SHEET_BYTES = SHEET_W * SHEET_H / 2; // A4

static std::vector<unsigned char> g_sheetData;
static TGLP_s g_tglp;
static FINF_s g_finf;
static CFNT_s g_font;

static int glyphIndexOf(char c)
{
	for (int i = 0; i < kGlyphCount; i++)
		if (kGlyphs[i].c == c) return i;
	// Caracteres absents : on retombe sur l'espace (index 0).
	return 0;
}

//---------------------------------------------------------------------------
// Fabrique les feuilles : glyphes 5x7 places dans les cellules, puis encodage
// A4 avec le meme entrelacement que la console.
//---------------------------------------------------------------------------
static void buildFont()
{
	const int sheets = (kGlyphCount + PER_SHEET - 1) / PER_SHEET;
	g_sheetData.assign((size_t)sheets * SHEET_BYTES, 0);

	for (int g = 0; g < kGlyphCount; g++)
	{
		const int sheetIndex = g / PER_SHEET;
		const int slot = g % PER_SHEET;
		const int slotLine = slot / N_ROWS;
		const int slotRow = slot % N_ROWS;
		const int originX = slotRow * (CELL_W + 1) + 1;
		const int originY = slotLine * (CELL_H + 1) + 1;

		unsigned char* sheet =
			g_sheetData.data() + (size_t)sheetIndex * SHEET_BYTES;

		// Le glyphe 5x7 est centre verticalement dans les 30 lignes de la
		// cellule : c'est ce que fait la vraie police.
		const int top = (CELL_H - 7) / 2;
		for (int row = 0; row < 7; row++)
		{
			const unsigned char bits = kGlyphs[g].rows[row];
			for (int col = 0; col < 5; col++)
			{
				const bool on = (bits >> (4 - col)) & 1;
				if (!on) continue;
				const int x = originX + col;
				const int y = originY + top + row;
				const int pos = fontdecode::sheetPixelPos(x, y, SHEET_W);
				fontdecode::setAlphaAtPixelPos(sheet, pos, SHEET_FMT, 0xFF);
			}
		}
	}

	g_tglp.cellWidth = CELL_W;
	g_tglp.cellHeight = CELL_H;
	g_tglp.baselinePos = 25;
	g_tglp.maxCharWidth = CELL_W;
	g_tglp.sheetSize = (u32)SHEET_BYTES;
	g_tglp.nSheets = (u16)sheets;
	g_tglp.sheetFmt = (u16)SHEET_FMT;
	g_tglp.nRows = N_ROWS;
	g_tglp.nLines = N_LINES;
	g_tglp.sheetWidth = SHEET_W;
	g_tglp.sheetHeight = SHEET_H;
	g_tglp.sheetData = g_sheetData.data();

	g_finf.lineFeed = CELL_H;
	g_finf.alterCharIndex = 0;
	g_finf.tglp = &g_tglp;
	g_finf.cwdh = nullptr;
	g_finf.cmap = nullptr;
	g_finf.height = CELL_H;
	g_finf.ascent = 25;
	g_finf.defaultWidth.left = 0;
	g_finf.defaultWidth.glyphWidth = 5;
	g_finf.defaultWidth.charWidth = 7;

	g_font.finf = g_finf;
}

//---------------------------------------------------------------------------
// Implementation de l'API de police (cote "console")
//---------------------------------------------------------------------------
Result fontEnsureMapped(void) { return 0; }
CFNT_s* fontGetSystemFont(void) { return &g_font; }
FINF_s* fontGetInfo(CFNT_s*) { return &g_font.finf; }
TGLP_s* fontGetGlyphInfo(CFNT_s*) { return &g_tglp; }

int fontGlyphIndexFromCodePoint(CFNT_s*, u32 cp)
{
	if (cp > 127) return 0;
	return glyphIndexOf((char)cp);
}

charWidthInfo_s* fontGetCharWidthInfo(CFNT_s*, int)
{
	return &g_finf.defaultWidth;
}

void fontCalcGlyphPos(fontGlyphPos_s* out, CFNT_s*, int glyphIndex, u32,
	float scaleX, float scaleY)
{
	(void)scaleY;
	charWidthInfo_s* cwi = &g_finf.defaultWidth;
	out->sheetIndex = glyphIndex / PER_SHEET;
	out->xOffset = scaleX * cwi->left;
	out->xAdvance = scaleX * cwi->charWidth;
	out->width = scaleX * cwi->glyphWidth;
	out->texcoord.left = out->texcoord.top = 0.0f;
	out->texcoord.right = out->texcoord.bottom = 0.0f;
}

unsigned long long osGetTime(void) { return 0; }

//---------------------------------------------------------------------------
// Sortie de l'image : dump RGB888 brut, converti en PNG par
// tools/pc_render_to_png.py (Pillow). Ecrire un PNG ici n'apporterait rien.
//---------------------------------------------------------------------------
static bool writeRgbDump(const char* path, int w, int h,
	const std::vector<unsigned char>& rgb)
{
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	fwrite(rgb.data(), 1, rgb.size(), f);
	fclose(f);
	return true;
}

//---------------------------------------------------------------------------
// Verifications
//---------------------------------------------------------------------------
static int g_failures = 0;

static void check(bool condition, const char* label)
{
	if (!condition)
	{
		printf("  ECHEC : %s\n", label);
		g_failures++;
	}
}

// 1. L'entrelacement doit etre une bijection de la feuille.
static void testSwizzleBijection()
{
	std::vector<int> seen((size_t)SHEET_W * SHEET_H, 0);
	int maxPos = -1;
	for (int y = 0; y < SHEET_H; y++)
		for (int x = 0; x < SHEET_W; x++)
		{
			const int pos = fontdecode::sheetPixelPos(x, y, SHEET_W);
			if (pos >= 0 && pos < (int)seen.size()) seen[(size_t)pos]++;
			if (pos > maxPos) maxPos = pos;
		}
	int distinct = 0;
	for (int v : seen) if (v == 1) distinct++;
	printf("Feuille %dx%d : %d positions distinctes, max %d (attendu %d / %d)\n",
		SHEET_W, SHEET_H, distinct, maxPos, SHEET_W * SHEET_H, SHEET_W * SHEET_H - 1);
	check(distinct == SHEET_W * SHEET_H, "bijection de l'entrelacement");
	check(maxPos == SHEET_W * SHEET_H - 1, "position maximale");
	// En A4 la feuille doit tenir dans sheetSize octets.
	check(maxPos / 2 < SHEET_BYTES, "la feuille tient dans sheetSize");
}

// 2. Chaque valeur d'alpha doit survivre a un aller-retour A4.
static void testAlphaRoundTrip()
{
	int errors = 0;
	for (int pos = 0; pos < SHEET_W * SHEET_H; pos++)
	{
		const unsigned char alpha = (unsigned char)((pos * 17) & 0xFF);
		std::vector<unsigned char> sheet(SHEET_BYTES, 0);
		fontdecode::setAlphaAtPixelPos(sheet.data(), pos, SHEET_FMT, alpha);
		const unsigned char back =
			fontdecode::alphaAtPixelPos(sheet.data(), pos, SHEET_FMT);
		// A4 ne conserve que les 4 bits de poids fort (x0x11).
		if (back != (unsigned char)((alpha >> 4) * 0x11)) errors++;
	}
	printf("Aller-retour A4 : %d erreurs sur %d positions\n", errors,
		SHEET_W * SHEET_H);
	check(errors == 0, "aller-retour A4");
}

// 3. Les glyphes ecrits doivent se relire a l'identique.
static void testGlyphReadBack()
{
	int errors = 0;
	std::vector<unsigned char> sheet(SHEET_BYTES);
	for (int g = 0; g < kGlyphCount && g < PER_SHEET; g++)
	{
		const int slot = g % PER_SHEET;
		const int slotLine = slot / N_ROWS;
		const int slotRow = slot % N_ROWS;
		const int originX = slotRow * (CELL_W + 1) + 1;
		const int originY = slotLine * (CELL_H + 1) + 1;
		std::memcpy(sheet.data(),
			g_sheetData.data() + (size_t)(g / PER_SHEET) * SHEET_BYTES,
			SHEET_BYTES);
		const int top = (CELL_H - 7) / 2;
		for (int row = 0; row < 7; row++)
			for (int col = 0; col < 5; col++)
			{
				const bool expected = (kGlyphs[g].rows[row] >> (4 - col)) & 1;
				const int pos = fontdecode::sheetPixelPos(originX + col,
					originY + top + row, SHEET_W);
				const unsigned char a =
					fontdecode::alphaAtPixelPos(sheet.data(), pos, SHEET_FMT);
				if (expected != (a > 0x80)) errors++;
			}
	}
	printf("Relecture des glyphes : %d erreurs\n", errors);
	check(errors == 0, "relecture des glyphes");
}

//---------------------------------------------------------------------------
// Planche de controle : rendu de texte dans un Bitmap RGB565 puis PNG.
//---------------------------------------------------------------------------
#include "../../source/render/bitmap.hpp"
#include "../../source/render/text.hpp"

static const char* kSampleLines[] = {
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ",
	"abcdefghijklmnopqrstuvwxyz",
	"0123456789 .,:-/!?()",
	"N3DS Web - navigateur autonome",
	"DuckDuckGo Lite  Rechercher",
	"RACCOURCIS  FAVORIS  Reglages",
};

static void renderSheet()
{
	TextEngine& te = TextEngine::instance();
	if (!te.init())
	{
		printf("ECHEC : TextEngine::init()\n");
		g_failures++;
		return;
	}
	printf("Police : %dx%d cellule, feuille %dx%d, format %s\n",
		te.cellWidth(), te.cellHeight(), SHEET_W, SHEET_H,
		te.sheetFormatName());

	const int W = 400;
	const int H = 300;
	Bitmap bmp;
	if (!bmp.create(W, H)) { printf("ECHEC : allocation bitmap\n"); g_failures++; return; }
	bmp.clear(C_NAVY);

	// Un fond de la meme couleur que l'interface, puis le texte.
	struct ScaleCase { float scale; const char* label; };
	const int y0 = 8;
	int y = y0;
	for (int i = 0; i < (int)(sizeof(kSampleLines) / sizeof(kSampleLines[0])); i++)
	{
		te.drawLine(bmp, kSampleLines[i], 6, y, 1.0f, C_TEXT, false, false);
		y += te.linePitch(1.0f) + 2;
	}
	// Une ligne "lien" soulignee, comme dans la page d'accueil.
	te.drawLine(bmp, "Continuer quand meme", 6, y, 1.0f, C_ACCENT, false, true);
	y += te.linePitch(1.0f) + 4;
	// Petit texte, comme la barre d'etat.
	te.drawLine(bmp, "Chargement 42 %  -  etat du navigateur", 6, y, 0.7f,
		C_TEXT_D, false, false);
	y += te.linePitch(0.7f) + 4;
	// Texte gras.
	te.drawLine(bmp, "Gras : RACCOURCIS", 6, y, 1.0f, C_ACCENT, true, false);

	// Bandeau de controle en bas : echelle 0.5 (petites touches).
	te.drawLine(bmp, "petite echelle 0.5 : OK 123 abc", 6, H - 22, 0.5f,
		C_TEXT_D, false, false);

	std::vector<unsigned char> rgb((size_t)W * H * 3);
	for (int py = 0; py < H; py++)
		for (int px = 0; px < W; px++)
		{
			const uint16_t v = bmp.px[(size_t)py * W + px];
			const int r = ((v >> 11) & 0x1F) * 255 / 31;
			const int g = ((v >> 5) & 0x3F) * 255 / 63;
			const int b = (v & 0x1F) * 255 / 31;
			const size_t o = ((size_t)py * W + px) * 3;
			rgb[o] = (unsigned char)r;
			rgb[o + 1] = (unsigned char)g;
			rgb[o + 2] = (unsigned char)b;
		}

	if (writeRgbDump("text_sheet.rgb", W, H, rgb))
		printf("Dump RGB ecrit : text_sheet.rgb (%dx%d)\n", W, H);
	else
	{
		printf("ECHEC : ecriture du dump\n");
		g_failures++;
	}

	// Verifie qu'il y a bien de l'encre : un decodage rate donnerait soit un
	// ecran vide, soit des blocs pleins. On mesure la proportion de pixels
	// eclaircis par rapport au fond.
	int inked = 0;
	for (size_t i = 0; i < (size_t)W * H; i++)
		if (bmp.px[i] != Bitmap::rgb32To565(C_NAVY)) inked++;
	const double ratio = (double)inked / (double)(W * H);
	printf("Pixels dessines : %d (%.1f %%)\n", inked, ratio * 100.0);
	check(ratio > 0.01, "le texte dessine quelque chose");
	check(ratio < 0.60, "le texte ne remplit pas tout (pas de blocs pleins)");

	//------------------------------------------------------------------------
	// Mesure de l'avance reelle du texte.
	//
	// L'avance theorique est la somme des xAdvance renvoyes par la police.
	// Si le texte dessine s'etend beaucoup plus loin, la mise a l'echelle
	// horizontale du rendu est en cause.
	//------------------------------------------------------------------------
	{
		const std::string sample = "N3DS Web";
		const float theoretical = te.measure(sample, 1.0f);
		printf("Avance theorique de \"%s\" : %.2f px\n", sample.c_str(),
			(double)theoretical);

		Bitmap probe;
		probe.create(600, 40);
		probe.clear(C_NAVY);
		te.drawLine(probe, sample, 2, 4, 1.0f, C_WHITE, false, false);

		const uint16_t background = Bitmap::rgb32To565(C_NAVY);
		int first = -1, last = -1;
		for (int x = 0; x < probe.w; x++)
			for (int y = 0; y < probe.h; y++)
				if (probe.px[(size_t)y * probe.w + x] != background)
				{
					if (first < 0) first = x;
					last = x;
					break;
				}
		const int span = (first >= 0 && last >= 0) ? (last - first + 1) : 0;
		printf("Encre mesuree : %d px (colonnes %d..%d)\n", span, first, last);
		if (theoretical > 1.0f)
		{
			printf("Rapport encre/avance = %.2f\n", (double)span / (double)theoretical);
		}
		probe.destroy();
	}
}

int main()
{
	printf("=== Verification PC de la recherche, du CSS et de la police ===\n\n");
	int searchFailures = searchSelfTest();
	if (searchFailures == 0)
		printf("Recherche : OK\n");
	else
	{
		printf("Recherche : %d echec(s)\n", searchFailures);
		g_failures += searchFailures;
	}
	int challengeFailures = searchPageSelfTest();
	if (challengeFailures == 0)
		printf("Detection anti-bot : OK\n");
	else
	{
		printf("Detection anti-bot : %d echec(s)\n", challengeFailures);
		g_failures += challengeFailures;
	}
	int cssFailures = cssSelfTest();
	if (cssFailures == 0)
		printf("CSS : OK\n");
	else
	{
		printf("CSS : %d echec(s)\n", cssFailures);
		g_failures += cssFailures;
	}
	testSwizzleBijection();
	testAlphaRoundTrip();

	buildFont();
	testGlyphReadBack();
	renderSheet();

	printf("\n%s (%d echec%s)\n", g_failures == 0 ? "TOUT EST OK" : "DES ECHECS",
		g_failures, g_failures > 1 ? "s" : "");
	return g_failures == 0 ? 0 : 1;
}
