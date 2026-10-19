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
#include "../../source/render/bitmap.hpp"
#include "../../source/render/layout.hpp"
#include "../../source/render/font_decode.hpp"
#include "../../source/search/search.hpp"
#include "../../source/search/search_page.hpp"
#include "../../source/html/parser.hpp"
#include "../../source/image/image_store.hpp"
#include "../../source/network/encoding.hpp"
#include "../../source/diag/diag.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

void diagStage(const char*) {}

static bool testImageStore()
{
	std::string bmp(58, '\0');
	bmp[0] = 'B'; bmp[1] = 'M';
	bmp[2] = 58; bmp[10] = 54;
	bmp[14] = 40; bmp[18] = 1; bmp[22] = 1;
	bmp[26] = 1; bmp[28] = 24; bmp[34] = 4;
	bmp[54] = (char)0x20; bmp[55] = (char)0x40; bmp[56] = (char)0x60;
	ImageStore store;
	if (!store.storeDownloaded("http://test/0.bmp", bmp) || store.count() != 1)
	{
		printf("  image 0: %s\n", store.lastError().c_str());
		return false;
	}
	if (!store.find("http://test/0.bmp") || store.bytesUsed() != sizeof(u16))
	{
		printf("  cache apres 1 image: count=%d bytes=%zu\n", store.count(), store.bytesUsed());
		return false;
	}
	for (int i = 1; i < IMG_MAX_COUNT; ++i)
		if (!store.storeDownloaded("http://test/" + std::to_string(i) + ".bmp", bmp))
		{
			printf("  image %d: %s\n", i, store.lastError().c_str());
			return false;
		}
	store.find("http://test/0.bmp"); // rendre la premiere image la plus recente
	if (!store.storeDownloaded("http://test/12.bmp", bmp)) return false;
	const bool ok = store.count() == IMG_MAX_COUNT &&
		store.bytesUsed() <= IMG_CACHE_MAX_BYTES &&
		store.find("http://test/0.bmp") != nullptr &&
		store.find("http://test/1.bmp") == nullptr;
	return ok;
}

static bool testResponseEncoding()
{
	const std::string binary("\xFF\xD8\x00\x80\xFE", 5);
	if (convertEncodingToUtf8(binary, "image/jpeg", false) != binary) return false;
	const std::string latin("caf\xE9", 4);
	return convertEncodingToUtf8(latin, "text/html; charset=windows-1252") == "caf\xC3\xA9";
}

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

static void testLinearGradient()
{
	Bitmap bitmap;
	if (!bitmap.create(9, 1))
	{
		check(false, "allocation d'un bitmap de test gradient");
		return;
	}
	const uint32_t colors[] = { 0xFF0000FF, 0xFFFF0000 };
	const int stops[] = { 0, 10000 };
	bitmap.fillLinearGradientRect(0, 0, 9, 1, colors, stops, 2, 90);
	const uint16_t first = bitmap.getPixel(0, 0);
	const uint16_t middle = bitmap.getPixel(4, 0);
	const uint16_t last = bitmap.getPixel(8, 0);
	const int firstRed = (first >> 11) & 0x1F;
	const int firstBlue = first & 0x1F;
	const int lastRed = (last >> 11) & 0x1F;
	const int lastBlue = last & 0x1F;
	check(firstRed > firstBlue && lastBlue > lastRed,
		"le gradient horizontal interpole les couleurs dans le bon sens");
	check(middle != first && middle != last,
		"le gradient contient une couleur intermediaire");
	bitmap.destroy();
}

// 3. Les glyphes ecrits doivent se relire a l'identique.
static void testBitmapViewport()
{
	Bitmap bitmap;
	bool ok = bitmap.create(4, 4, 10);
	if (ok)
	{
		bitmap.clear(C_BLACK);
		bitmap.fillRect(0, 8, 4, 4, C_RED);
		ok = bitmap.originY == 10 &&
			bitmap.getPixel(1, 9) == 0 &&
			bitmap.getPixel(1, 10) == Bitmap::rgb32To565(C_RED) &&
			bitmap.getPixel(1, 11) == Bitmap::rgb32To565(C_RED) &&
			bitmap.getPixel(1, 12) == 0;
	}
	check(ok, "bitmap segment clippe les coordonnees absolues");
}

static void testBitmapCssEffects()
{
	Bitmap bitmap;
	bool ok = bitmap.create(12, 12);
	if (ok)
	{
		bitmap.clear(C_BLACK);
		bitmap.fillRoundedRect(2, 2, 8, 8, 4, C_RED);
		const uint16_t black = Bitmap::rgb32To565(C_BLACK);
		const uint16_t red = Bitmap::rgb32To565(C_RED);
		ok = bitmap.getPixel(2, 2) == black &&
			bitmap.getPixel(6, 2) == red &&
			bitmap.getPixel(6, 6) == red;
		bitmap.blendRect(0, 0, 1, 1, 0x800000FF);
		ok = ok && bitmap.getPixel(0, 0) == Bitmap::rgb32To565(0xFF000080);
	}
	check(ok, "coins arrondis et transparence CSS sur bitmap");
}

static void testPageSegments()
{
	std::string html = "<html><body>";
	for (int i = 0; i < 500; ++i)
		html += "<p><a href='/segment'>Texte de test pour verifier le rendu progressif.</a></p>";
	html += "</body></html>";

	Document doc;
	const bool parsed = htmlParse(html, doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	collectStyleBlocks(doc, css);
	PageLayout layout(doc, css, settings);
	PageModel first;
	const bool firstRendered = parsed && layout.renderSegment(first, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	const bool firstOk = firstRendered &&
		first.bmp.h == NW_PAGE_SEGMENT_HEIGHT &&
		first.bmp.originY == 0 && first.segmentIndex == 0 &&
		first.truncated && first.height >= NW_PAGE_SEGMENT_HEIGHT &&
		first.height <= NW_PAGE_SEGMENT_HEIGHT + 32;

	PageModel next;
	const bool nextRendered = parsed && layout.renderSegment(next, "https://test/", 1,
		NW_PAGE_SEGMENT_HEIGHT + NW_PAGE_SEGMENT_OVERLAP);
	const bool nextOk = nextRendered &&
		next.bmp.h == NW_PAGE_SEGMENT_HEIGHT + NW_PAGE_SEGMENT_OVERLAP &&
		next.bmp.originY == NW_PAGE_SEGMENT_HEIGHT - NW_PAGE_SEGMENT_OVERLAP &&
		next.segmentIndex == 1 && next.truncated &&
		next.height == NW_MAX_PAGE_CONTENT_HEIGHT;
	PageModel beyondLimit;
	const bool beyondLimitRejected = !layout.renderSegment(beyondLimit,
		"https://test/", NW_MAX_PAGE_SEGMENTS, NW_PAGE_SEGMENT_HEIGHT);
	bool sharedLinkAtOverlap = false;
	for (const LinkBox& firstLink : first.links)
	{
		if (firstLink.y < next.bmp.originY || firstLink.y >= first.height) continue;
		for (const LinkBox& nextLink : next.links)
		{
			if (firstLink.x == nextLink.x && firstLink.y == nextLink.y &&
				firstLink.w == nextLink.w && firstLink.url == nextLink.url)
			{
				sharedLinkAtOverlap = true;
				break;
			}
		}
		if (sharedLinkAtOverlap) break;
	}

	Document shortDoc;
	const bool shortParsed = htmlParse("<html><body><p>Page courte.</p></body></html>",
		shortDoc);
	CssEngine shortCss;
	collectStyleBlocks(shortDoc, shortCss);
	PageLayout shortLayout(shortDoc, shortCss, settings);
	PageModel shortPage;
	const bool shortRendered = shortParsed &&
		shortLayout.renderSegment(shortPage, "https://test/", 0, NW_PAGE_SEGMENT_HEIGHT);
	const bool shortOk = shortRendered && !shortPage.truncated &&
		shortPage.height < NW_PAGE_SEGMENT_HEIGHT;
	if (!firstOk || !nextOk || !sharedLinkAtOverlap || !beyondLimitRejected || !shortOk)
		printf("  segments: parsed=%d, first=(%d,h=%d,origin=%d,index=%d,truncated=%d), "
			"next=(%d,h=%d,origin=%d,index=%d,truncated=%d,height=%d), "
			"sharedLink=%d, beyondLimitRejected=%d, short=(%d,h=%d,truncated=%d)\n",
			parsed, first.bmp.h, first.height, first.bmp.originY, first.segmentIndex,
			first.truncated, nextRendered, next.bmp.h, next.bmp.originY,
			next.segmentIndex, next.truncated, next.height, sharedLinkAtOverlap,
			beyondLimitRejected, shortRendered,
			shortPage.height, shortPage.truncated);
	check(firstOk && nextOk && sharedLinkAtOverlap && beyondLimitRejected && shortOk,
		"segments de 2 000 px limites a 4 000 px, raccord et page courte");
}

static void testGridLayout()
{
	const std::string html =
		"<html><head><style>"
		".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));"
		"gap:6px 10px;min-height:min(62vh,560px)}"
		".first{background:#ff0000}.second{background:#0000ff}"
		".wide{grid-column:1 / 3;background:#00ff00}"
		"</style></head><body><div class=\"grid\">"
		"<div class=\"first\">Premiere colonne</div>"
		"<div class=\"second\">Deuxieme colonne plus large</div>"
		"<div class=\"wide\">Element sur deux colonnes</div>"
		"</div></body></html>";
	Document doc;
	bool ok = htmlParse(html, doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	collectStyleBlocks(doc, css);
	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	if (ok)
	{
		const uint16_t red = Bitmap::rgb32To565(0xFF0000FF);
		const uint16_t blue = Bitmap::rgb32To565(0xFFFF0000);
		const uint16_t green = Bitmap::rgb32To565(0xFF00FF00);
		bool greenSpansRow = false;
		for (int y = 0; y < std::min(page.height, 100); ++y)
			if (page.bmp.getPixel(8, y) == green &&
				page.bmp.getPixel(NW_PAGE_WIDTH - 8, y) == green)
				greenSpansRow = true;
		ok = page.height >= 148 && page.bmp.getPixel(12, 2) == red &&
			page.bmp.getPixel(260, 2) == blue && greenSpansRow;
	}
	check(ok, "CSS Grid: colonnes fractionnaires et element multicolonne");

	const std::string autoFitHtml =
		"<html><head><style>"
		".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,180px),1fr));gap:8px}"
		".first{background:#ff0000}.second{background:#0000ff}"
		"</style></head><body><div class=\"grid\">"
		"<div class=\"first\">Carte une</div><div class=\"second\">Carte deux</div>"
		"</div></body></html>";
	Document autoFitDoc;
	ok = htmlParse(autoFitHtml, autoFitDoc);
	CssEngine autoFitCss;
	collectStyleBlocks(autoFitDoc, autoFitCss);
	PageLayout autoFitLayout(autoFitDoc, autoFitCss, settings);
	PageModel autoFitPage;
	ok = ok && autoFitLayout.renderSegment(autoFitPage, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	if (ok)
	{
		const uint16_t autoFitRed = Bitmap::rgb32To565(0xFF0000FF);
		const uint16_t autoFitBlue = Bitmap::rgb32To565(0xFFFF0000);
		ok = autoFitPage.bmp.getPixel(12, 2) == autoFitRed &&
			autoFitPage.bmp.getPixel(260, 2) == autoFitBlue;
	}
	check(ok, "CSS Grid: repeat(auto-fit, minmax())");

	const std::string flexHtml =
		"<html><head><style>"
		".row{display:flex;justify-content:flex-end;gap:0}"
		".first{width:40px;height:24px;background:#ff0000}"
		".second{width:40px;height:24px;background:#0000ff}"
		"</style></head><body><div class=\"row\">"
		"<div class=\"first\"></div><div class=\"second\"></div>"
		"</div></body></html>";
	Document flexDoc;
	ok = htmlParse(flexHtml, flexDoc);
	CssEngine flexCss;
	collectStyleBlocks(flexDoc, flexCss);
	PageLayout flexLayout(flexDoc, flexCss, settings);
	PageModel flexPage;
	ok = ok && flexLayout.renderSegment(flexPage, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	if (ok)
	{
		const uint16_t flexRed = Bitmap::rgb32To565(0xFF0000FF);
		const uint16_t flexBlue = Bitmap::rgb32To565(0xFFFF0000);
		int redX = NW_PAGE_WIDTH, blueX = NW_PAGE_WIDTH;
		for (int x = 0; x < NW_PAGE_WIDTH; ++x)
		{
			if (redX == NW_PAGE_WIDTH && flexPage.bmp.getPixel(x, 2) == flexRed)
				redX = x;
			if (blueX == NW_PAGE_WIDTH && flexPage.bmp.getPixel(x, 2) == flexBlue)
				blueX = x;
		}
		ok = redX < NW_PAGE_WIDTH && blueX < NW_PAGE_WIDTH && redX > 250 &&
			blueX > redX;
	}
	check(ok, "CSS Flexbox: direction par defaut et justify-content:flex-end");

	const std::string flexColumnHtml =
		"<html><head><style>"
		".column{display:flex;flex-direction:column;gap:7px}"
		".first{height:20px;background:#ff0000}"
		".second{height:20px;background:#0000ff}"
		"</style></head><body><div class=\"column\">"
		"<div class=\"first\"></div><div class=\"second\"></div>"
		"</div></body></html>";
	Document flexColumnDoc;
	ok = htmlParse(flexColumnHtml, flexColumnDoc);
	CssEngine flexColumnCss;
	collectStyleBlocks(flexColumnDoc, flexColumnCss);
	PageLayout flexColumnLayout(flexColumnDoc, flexColumnCss, settings);
	PageModel flexColumnPage;
	ok = ok && flexColumnLayout.renderSegment(flexColumnPage, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	if (ok)
	{
		const uint16_t flexColumnRed = Bitmap::rgb32To565(0xFF0000FF);
		const uint16_t flexColumnBlue = Bitmap::rgb32To565(0xFFFF0000);
		int redY = flexColumnPage.height, redBottom = -1;
		int blueY = flexColumnPage.height;
		for (int y = 0; y < flexColumnPage.height; ++y)
		{
			if (flexColumnPage.bmp.getPixel(12, y) == flexColumnRed)
			{
				redY = std::min(redY, y);
				redBottom = y;
			}
			if (blueY == flexColumnPage.height &&
				flexColumnPage.bmp.getPixel(12, y) == flexColumnBlue)
				blueY = y;
		}
		ok = redY < flexColumnPage.height && blueY < flexColumnPage.height &&
			blueY - redBottom - 1 >= 4;
	}
	check(ok, "CSS Flexbox: flex-direction:column avec gap vertical");
}

static void testRootThemeColors()
{
	const std::string html =
		"<html><head><style>"
		":root{--page:#000000;--ink:#f2e7d5;--accent:#00ff00}"
		"body{background:var(--page);color:var(--ink)}"
		".accent{color:var(--accent)}"
		"strong{font-weight:700;color:#ff0000}"
		"</style></head><body><p>Texte <span class=\"accent\">colore</span> "
		"<strong>important</strong></p></body></html>";
	Document doc;
	bool ok = htmlParse(html, doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	collectStyleBlocks(doc, css);
	const int htmlNode = doc.firstByTag(doc.root, "html");
	const int bodyNode = doc.firstByTag(doc.root, "body");
	Style rootStyle = css.resolve(doc, htmlNode, defaultStyle());
	Style bodyStyle = css.resolve(doc, bodyNode, rootStyle);
	int strongNode = -1;
	for (size_t i = 0; i < doc.nodes.size(); ++i)
		if (doc.nodes[i].isElement() && doc.nodes[i].tag == "strong")
			strongNode = (int)i;
	const Style strongStyle = css.resolve(doc, strongNode, bodyStyle);
	ok = ok && bodyStyle.color == 0xFFD5E7F2 && bodyStyle.colorSet &&
		bodyStyle.bgSet &&
		bodyStyle.bg == 0xFF000000 && strongStyle.bold &&
		strongStyle.color == 0xFF0000FF;
	int accentNode = -1;
	for (size_t i = 0; i < doc.nodes.size(); ++i)
		if (doc.nodes[i].isElement() && doc.nodes[i].tag == "span")
			accentNode = (int)i;
	const Style accentStyle = css.resolve(doc, accentNode, bodyStyle);
	ok = ok && accentStyle.colorSet && accentStyle.color == 0xFF00FF00;

	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	bool foundInk = false;
	bool foundRed = false;
	bool foundAccent = false;
	const uint16_t darkBackground = Bitmap::rgb32To565(0xFF000000);
	if (ok)
	{
		ok = page.bmp.getPixel(NW_PAGE_WIDTH - 10, 2) == darkBackground;
		for (int y = 0; y < std::min(page.height, 80); ++y)
			for (int x = 0; x < NW_PAGE_WIDTH; ++x)
			{
				const uint16_t pixel = page.bmp.getPixel(x, y);
				foundInk = foundInk || (((pixel >> 5) & 0x3F) > 8 &&
					(pixel & 0x1F) > 8);
				foundRed = foundRed ||
					pixel == Bitmap::rgb32To565(0xFF0000FF);
				foundAccent = foundAccent ||
					pixel == Bitmap::rgb32To565(0xFF00FF00);
			}
		ok = ok && foundInk && foundRed && foundAccent;
	}
	check(ok, "CSS :root variables, couleurs de texte et gras visibles");
}

static void testSegmentedBackgroundKeepsText()
{
	std::string html = "<html><body><div style=\"background: #000000; "
		"color: #f2e7d5; padding: 8px\">";
	for (int i = 0; i < 240; ++i)
		html += "<p>Visible pale text on a dark background</p>";
	html += "</div></body></html>";
	Document doc;
	bool ok = htmlParse(html, doc);
	CssEngine css;
	collectStyleBlocks(doc, css);
	WebSettings settings;
	settings.enableJS = false;
	PageLayout layout(doc, css, settings);
	PageModel firstSegment;
	ok = ok && layout.renderSegment(firstSegment, "https://test/",
		0, NW_PAGE_SEGMENT_HEIGHT);
	PageModel secondSegment;
	ok = ok && layout.renderSegment(secondSegment, "https://test/", 1,
		NW_PAGE_SEGMENT_HEIGHT + NW_PAGE_SEGMENT_OVERLAP);
	const uint16_t darkBackground = Bitmap::rgb32To565(0xFF000000);
	bool firstSegmentHasText = false;
	bool secondSegmentHasText = false;
	int overlapPixelMismatches = 0;
	if (ok)
	{
		for (int y = 0; y < firstSegment.bmp.h && !firstSegmentHasText; ++y)
			for (int x = 0; x < firstSegment.bmp.w; ++x)
			{
				const uint16_t pixel = firstSegment.bmp.getPixel(x, y);
				if (pixel != darkBackground &&
					((pixel >> 5) & 0x3F) > 8 && (pixel & 0x1F) > 8)
				{
					firstSegmentHasText = true;
					break;
				}
			}
		for (int y = secondSegment.bmp.originY;
			y < NW_PAGE_SEGMENT_HEIGHT; ++y)
			for (int x = 0; x < firstSegment.bmp.w; ++x)
				if (firstSegment.bmp.getPixel(x, y) !=
					secondSegment.bmp.getPixel(x, y))
				{
					if (overlapPixelMismatches < 8)
						printf("  segment pixel (%d,%d): %04x != %04x\n",
							x, y, firstSegment.bmp.getPixel(x, y),
							secondSegment.bmp.getPixel(x, y));
					++overlapPixelMismatches;
				}
		for (int y = secondSegment.bmp.originY;
			y < NW_PAGE_SEGMENT_HEIGHT && !secondSegmentHasText; ++y)
			for (int x = 0; x < secondSegment.bmp.w; ++x)
			{
				const uint16_t pixel = secondSegment.bmp.getPixel(x, y);
				if (pixel != darkBackground &&
					((pixel >> 5) & 0x3F) > 8 && (pixel & 0x1F) > 8)
				{
					secondSegmentHasText = true;
					break;
				}
			}
		ok = firstSegment.truncated && secondSegment.truncated &&
			firstSegmentHasText && secondSegmentHasText &&
			overlapPixelMismatches == 0;
		if (!ok)
			printf("  segment text: firstTruncated=%d secondTruncated=%d "
				"firstText=%d secondText=%d height=%d origin=%d "
				"overlapMismatches=%d\n",
				firstSegment.truncated, secondSegment.truncated,
				firstSegmentHasText, secondSegmentHasText, secondSegment.height,
				secondSegment.bmp.originY, overlapPixelMismatches);
	}
	check(ok, "texte conservé sur les deux segments d'un bloc avec fond");
}

static void testModernCssLayout()
{
	const std::string html =
		"<html><body><div style=\"background: rgb(255 0 0 / 50%); "
		"border-radius: 18px; box-shadow: 0px 2px 6px rgba(0,0,0,0.5); "
		"padding: 8px\">Carte CSS</div></body></html>";
	Document doc;
	bool ok = htmlParse(html, doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	collectStyleBlocks(doc, css);
	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	if (ok)
	{
		const uint16_t blendedRed = Bitmap::rgb32To565(0xFF7F7FFF);
		const uint16_t white = Bitmap::rgb32To565(0xFFFFFFFF);
		const uint16_t topCorner = page.bmp.getPixel(4, 0);
		const uint16_t cardCenter = page.bmp.getPixel(200, 4);
		const uint16_t outsideShadow = page.bmp.getPixel(200, page.height - 4);
		const int centerRed = (cardCenter >> 11) & 0x1F;
		const int centerGreen = (cardCenter >> 5) & 0x3F;
		const int centerBlue = cardCenter & 0x1F;
		ok = cardCenter != white && centerRed > centerGreen &&
			centerRed > centerBlue && topCorner != cardCenter &&
			outsideShadow != white && blendedRed != white;
	}
	check(ok, "rendu CSS combine alpha, coins arrondis et ombre");
}

static void testPreformattedCssText()
{
	Document doc;
	bool ok = htmlParse(
		"<html><body><pre style=\"color: rgb(255 0 0); background: #0000ff; "
		"padding: 6px; font-size: 20px; "
		"line-height: 30px\">RED\nTEXT</pre></body></html>", doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	collectStyleBlocks(doc, css);
	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);

	int redPixels = 0;
	int firstRedY = -1;
	int lastRedY = -1;
	bool backgroundRendered = false;
	if (ok)
	{
		const uint16_t background = page.bmp.getPixel(10, 1);
		const int backgroundRed = (background >> 11) & 0x1F;
		const int backgroundBlue = background & 0x1F;
		backgroundRendered = backgroundBlue > backgroundRed &&
			page.bmp.getPixel(0, 1) != background;
		for (int y = 0; y < page.bmp.h; ++y)
			for (int x = 0; x < page.bmp.w; ++x)
			{
				const uint16_t pixel = page.bmp.getPixel(x, y);
				const int red = (pixel >> 11) & 0x1F;
				const int green = (pixel >> 5) & 0x3F;
				const int blue = pixel & 0x1F;
				if (red > blue + 4 && red > green / 2)
				{
					++redPixels;
					if (firstRedY < 0) firstRedY = y;
					lastRedY = y;
				}
			}
	}
	check(ok && backgroundRendered && redPixels > 10 && firstRedY >= 0 &&
		lastRedY - firstRedY >= 10,
		"bloc pre respecte fond, padding, couleur, taille et interligne CSS");
}

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
	const int parserFailures = htmlSelfTest();
	if (parserFailures == 0)
		printf("Parseur HTML : OK\n");
	else
	{
		printf("Parseur HTML : %d echec(s)\n", parserFailures);
		g_failures += parserFailures;
	}
	int cssFailures = cssSelfTest();
	if (cssFailures == 0)
		printf("CSS : OK\n");
	else
	{
		printf("CSS : %d echec(s)\n", cssFailures);
		g_failures += cssFailures;
	}
	const bool imageOk = testImageStore();
	printf("Cache images / LRU : %s\n", imageOk ? "OK" : "ECHEC");
	if (!imageOk) g_failures++;
	const bool encodingOk = testResponseEncoding();
	printf("Reponses binaires / encodage texte : %s\n", encodingOk ? "OK" : "ECHEC");
	if (!encodingOk) g_failures++;
	testSwizzleBijection();
	testAlphaRoundTrip();
	testLinearGradient();
	testBitmapViewport();

	buildFont();
	testGlyphReadBack();
	renderSheet();
	testBitmapCssEffects();
	testPageSegments();
	testGridLayout();
	testRootThemeColors();
	testSegmentedBackgroundKeepsText();
	testModernCssLayout();
	testPreformattedCssText();

	printf("\n%s (%d echec%s)\n", g_failures == 0 ? "TOUT EST OK" : "DES ECHECS",
		g_failures, g_failures > 1 ? "s" : "");
	return g_failures == 0 ? 0 : 1;
}
