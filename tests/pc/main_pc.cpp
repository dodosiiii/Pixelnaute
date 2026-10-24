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
#include "../../source/render/image_fit.hpp"
#include "../../source/render/layout.hpp"
#include "../../source/render/text.hpp"
#include "../../source/render/font_decode.hpp"
#include "../../source/search/search.hpp"
#include "../../source/search/search_page.hpp"
#include "../../source/browser/compatibility_page.hpp"
#include "../../source/html/parser.hpp"
#include "../../source/image/image_store.hpp"
#include "../../source/network/encoding.hpp"
#include "../../source/pdf/pdf_reader.hpp"
#include "../../source/common.hpp"
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

static bool testImageFit()
{
	ImageFitRect rect;
	if (!calculateImageFitRect(100, 100, 200, 100, CssImageFitContain,
		50, 50, rect) ||
		rect.destX != 0.0f || rect.destY != 25.0f ||
		rect.destW != 100.0f || rect.destH != 50.0f)
		return false;
	if (!calculateImageFitRect(100, 100, 200, 100, CssImageFitCover,
		50, 50, rect) ||
		rect.sourceX != 50.0f || rect.sourceY != 0.0f ||
		rect.sourceW != 100.0f || rect.sourceH != 100.0f)
		return false;
	if (!calculateImageFitRect(100, 100, 200, 100, CssImageFitCover,
		0, 50, rect) || rect.sourceX != 0.0f)
		return false;

	Document doc;
	if (!htmlParse("<style>img { object-fit: cover; "
		"object-position: 25% 75%; }</style>"
		"<img src=\"/thumb.png\" width=\"64\" height=\"48\">", doc))
		return false;
	CssEngine css;
	collectStyleBlocks(doc, css);
	WebSettings settings;
	settings.enableJS = false;
	PageLayout layout(doc, css, settings);
	PageModel page;
	return layout.render(page, "https://example.test/page") &&
		page.images.size() == 1 &&
		page.images[0].fit == CssImageFitCover &&
		page.images[0].positionX == 25 &&
		page.images[0].positionY == 75;
}

static bool testResponseEncoding()
{
	const std::string binary("\xFF\xD8\x00\x80\xFE", 5);
	if (convertEncodingToUtf8(binary, "image/jpeg", false) != binary) return false;
	const std::string latin("caf\xE9", 4);
	return convertEncodingToUtf8(latin, "text/html; charset=windows-1252") == "caf\xC3\xA9";
}

static bool testUrlOrigins()
{
	return urlSameOrigin("https://example.com/a", "HTTPS://EXAMPLE.COM/b?x=1") &&
		urlSameOrigin("http://example.com:80/a", "http://example.com/b") &&
		urlSameOrigin("https://example.com:443/a", "https://example.com/b") &&
		urlSameOrigin("https://example.com:8443/a", "https://example.com:8443/b") &&
		urlSameOrigin("https://[2001:db8::1]/a", "https://[2001:DB8::1]:443/b") &&
		!urlSameOrigin("http://example.com/", "https://example.com/") &&
		!urlSameOrigin("https://example.com/", "https://sub.example.com/") &&
		!urlSameOrigin("https://example.com:8443/", "https://example.com/") &&
		!urlSameOrigin("https://user@example.com/", "https://user@example.com/") &&
		!urlSameOrigin("about:blank", "about:blank") &&
		!urlSameOrigin("https://example.com/", "not a URL") &&
		urlJoin("https://example.com/a/page", "HTTPS://other.example/b") ==
			"https://other.example/b" &&
		urlJoin("https://example.com/a/page", "JaVaScRiPt:alert(1)") ==
			"javascript:alert(1)";
}

static bool testCompatibilityPage()
{
	Document doc;
	if (!htmlParse(internalCompatibilityPageHtml(), doc) ||
		doc.title() != "Test affichage Pixelnaute" ||
		doc.textOf(doc.root).find("Sites reels") == std::string::npos)
	{
		printf("  echec parse/titre/contenu de la page interne\n");
		return false;
	}

	CssEngine css;
	collectStyleBlocks(doc, css);
	WebSettings settings;
	settings.enableJS = false;
	PageLayout layout(doc, css, settings);
	PageModel page;
	const bool rendered = layout.renderSegment(page, "nw://display-test", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	if (!rendered ||
		page.height < NW_TOP_HEIGHT || !page.bmp.px ||
		page.links.size() < 5 || page.fields.empty())
	{
		printf("  rendu=%d hauteur=%d bitmap=%d liens=%zu champs=%zu\n",
			rendered, page.height, page.bmp.px != nullptr, page.links.size(),
			page.fields.size());
		return false;
	}

	bool wikipedia = false, google = false, reddit = false;
	for (const LinkBox& link : page.links)
	{
		wikipedia = wikipedia || link.url.find("wikipedia.org") != std::string::npos;
		google = google || link.url.find("google.com") != std::string::npos;
		reddit = reddit || link.url.find("reddit.com") != std::string::npos;
	}
	if (!wikipedia || !google || !reddit)
		printf("  liens sites: Wikipedia=%d Google=%d Reddit=%d\n",
			wikipedia, google, reddit);
	return wikipedia && google && reddit;
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
		bitmap.clear(C_BLACK);
		bitmap.setClipRect(3, 3, 9, 9);
		bitmap.fillRect(0, 0, 12, 12, C_RED);
		ok = ok && bitmap.getPixel(4, 4) == red &&
			bitmap.getPixel(2, 4) == black &&
			bitmap.getPixel(4, 2) == black &&
			bitmap.getPixel(10, 4) == black;
		bitmap.restoreClipRect({ 0, 0, 12, 12 });
		bitmap.fillRect(0, 0, 1, 1, C_BLUE);
		ok = ok && bitmap.getPixel(0, 0) == Bitmap::rgb32To565(C_BLUE);
	}
	check(ok, "coins arrondis, transparence et decoupage bitmap CSS");
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
		next.height >= NW_PAGE_SEGMENT_HEIGHT * 2 &&
		next.height <= NW_PAGE_SEGMENT_HEIGHT * 2 + 32;
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
	if (!firstOk || !nextOk || !sharedLinkAtOverlap ||
		!beyondLimitRejected || !shortOk)
		printf("  segments: parsed=%d, first=(%d,h=%d,origin=%d,index=%d,truncated=%d), "
			"next=(%d,h=%d,origin=%d,index=%d,truncated=%d,height=%d), "
			"sharedLink=%d, beyondLimitRejected=%d, short=(%d,h=%d,truncated=%d)\n",
			parsed, first.bmp.h, first.height, first.bmp.originY, first.segmentIndex,
			first.truncated, nextRendered, next.bmp.h, next.bmp.originY,
			next.segmentIndex, next.truncated, next.height, sharedLinkAtOverlap,
			beyondLimitRejected, shortRendered, shortPage.height,
			shortPage.truncated);
	check(firstOk && nextOk && sharedLinkAtOverlap &&
		beyondLimitRejected && shortOk,
		"petits segments progresifs, raccord et page courte");
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

static void testFlexReverseLayout()
{
	WebSettings settings;
	settings.enableJS = false;
	const std::string html =
		"<html><head><style>"
		".row{flex-direction:row-reverse}.row{display:flex}"
		".column{flex-direction:column-reverse}.column{display:flex}"
		".first{width:40px;height:20px;background:#ff0000}"
		".second{width:40px;height:20px;background:#0000ff}"
		"</style></head><body>"
		"<div class=\"row\"><div class=\"first\"></div><div class=\"second\"></div></div>"
		"<div class=\"column\"><div class=\"first\"></div><div class=\"second\"></div></div>"
		"</body></html>";
	Document doc;
	bool ok = htmlParse(html, doc);
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
		int redRowX = NW_PAGE_WIDTH, blueRowX = NW_PAGE_WIDTH;
		for (int x = 0; x < NW_PAGE_WIDTH; ++x)
		{
			if (redRowX == NW_PAGE_WIDTH && page.bmp.getPixel(x, 2) == red)
				redRowX = x;
			if (blueRowX == NW_PAGE_WIDTH && page.bmp.getPixel(x, 2) == blue)
				blueRowX = x;
		}

		int redColumnY = page.height, blueColumnY = page.height;
		for (int y = 0; y < page.height; ++y)
		{
			if (redColumnY == page.height && page.bmp.getPixel(12, y) == red)
				redColumnY = y;
			if (blueColumnY == page.height && page.bmp.getPixel(12, y) == blue)
				blueColumnY = y;
		}
		ok = redRowX < NW_PAGE_WIDTH && blueRowX < NW_PAGE_WIDTH &&
			redRowX > blueRowX && redColumnY < page.height &&
			blueColumnY < page.height && redColumnY > blueColumnY;
	}
	check(ok, "CSS Flexbox: row-reverse et column-reverse");
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

static void testCssJustifiedText()
{
	const std::string html =
		"<html><body><p style=\"width:180px;text-align:justify\">"
		"Lorem ipsum dolor sit amet consectetur adipiscing elit sed do "
		"eiusmod tempor incididunt ut labore et dolore magna aliqua"
		"</p></body></html>";
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
	bool fullLine = false;
	bool inkNearRightEdge = false;
	if (ok)
	{
		const uint16_t white = Bitmap::rgb32To565(0xFFFFFFFF);
		for (const TextRunBox& run : page.textRuns)
		{
			if (run.w != 180 || run.text.find(' ') == std::string::npos)
				continue;
			fullLine = true;
			for (int y = run.y; y < run.y + run.h && !inkNearRightEdge; ++y)
				for (int x = 160; x < 184; ++x)
					if (page.bmp.getPixel(x, y) != white)
					{
						inkNearRightEdge = true;
						break;
					}
		}
	}
	check(ok && fullLine && inkNearRightEdge,
		"CSS text-align:justify distribue l'espace sur les lignes pleines");
}

static void testHtmlInlineWhitespace()
{
	Document doc;
	bool ok = htmlParse(
		"<html><body><p><span>alpha</span> <span>beta</span></p></body></html>",
		doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	TextRunBox alpha, beta;
	if (ok)
	{
		for (const TextRunBox& run : page.textRuns)
		{
			if (run.text == "alpha") alpha = run;
			else if (run.text == " beta") beta = run;
		}
		ok = alpha.w > 0 && beta.w > 0 && alpha.y == beta.y &&
			beta.x >= alpha.x + alpha.w;
	}
	check(ok, "HTML inline whitespace remains visible between sibling elements");
}

static void testHtmlMediaFallback()
{
	Document doc;
	bool ok = htmlParse(
		"<body><canvas><p>canvas fallback</p></canvas>"
		"<video><p>video fallback</p></video>"
		"<audio><p>audio fallback</p></audio></body>", doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	collectStyleBlocks(doc, css);
	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	static const char* expected[] = {
		"canvas fallback", "video fallback", "audio fallback"
	};
	for (const char* text : expected)
	{
		bool found = false;
		for (const TextRunBox& run : page.textRuns)
			if (run.text.find(text) != std::string::npos)
				found = true;
		ok = ok && found;
	}
	check(ok, "HTML canvas and media fallback content remains visible");
}

static void testCssTableDisplay()
{
	const std::string html =
		"<html><body><div class=\"board\"><div class=\"row\">"
		"<div class=\"cell\">X</div><div class=\"cell\"></div>"
		"<div class=\"cell\"></div></div><div class=\"row\">"
		"<div class=\"cell\"></div><div class=\"cell\"></div>"
		"<div class=\"cell\"></div></div><div class=\"row\">"
		"<div class=\"cell\"></div><div class=\"cell\"></div>"
		"<div class=\"cell\"></div></div></div></body></html>";
	Document doc;
	bool ok = htmlParse(html, doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	css.parseStylesheet(
		".board { display: table; margin: 0 auto; } "
		".row { display: table-row; } "
		".cell { display: table-cell; width: 40px; height: 40px; "
		"border: 2px solid red; background: #00ff00; "
		"text-align: center; vertical-align: middle; }");
	collectStyleBlocks(doc, css);
	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	if (ok)
	{
		const uint16_t green = Bitmap::rgb32To565(0xFF00FF00);
		int greenPixels = 0;
		int firstGreenX = page.bmp.w, lastGreenX = -1;
		int firstGreenY = page.bmp.h, lastGreenY = -1;
		int centeredTextY = -1;
		for (const TextRunBox& run : page.textRuns)
			if (run.text == "X") centeredTextY = run.y;
		for (int y = 0; y < std::min(180, page.bmp.h); ++y)
			for (int x = 0; x < page.bmp.w; ++x)
				if (page.bmp.getPixel(x, y) == green)
				{
					++greenPixels;
					firstGreenX = std::min(firstGreenX, x);
					lastGreenX = std::max(lastGreenX, x);
					firstGreenY = std::min(firstGreenY, y);
					lastGreenY = std::max(lastGreenY, y);
				}
		ok = greenPixels >= 4000 && firstGreenX > 50 &&
			lastGreenX < page.bmp.w - 50 &&
			lastGreenX - firstGreenX >= 100 &&
			lastGreenY - firstGreenY >= 120 &&
			centeredTextY >= firstGreenY + 5 &&
			centeredTextY < firstGreenY + 35;
		if (!ok)
			printf("  table display: green=%d bounds=(%d,%d)-(%d,%d) "
				"textY=%d bitmap=%dx%d\n", greenPixels, firstGreenX,
				firstGreenY, lastGreenX, lastGreenY, centeredTextY,
				page.bmp.w, page.bmp.h);
	}
	check(ok, "CSS display: table, table-row et table-cell");
}

static void testCssInlineBlock()
{
	Document doc;
	bool ok = htmlParse(
		"<html><body><div><a href=\"/one\" class=\"pill\">Un</a>"
		"<a href=\"/two\" class=\"pill\">Deux</a></div></body></html>",
		doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	css.parseStylesheet(
		".pill { display:inline-block; padding:3px 5px; "
		"background:#ff0000; border:1px solid #0000ff; }");
	collectStyleBlocks(doc, css);
	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	int firstX = -1, secondX = -1, firstY = -1, secondY = -1;
	bool firstLink = false, secondLink = false;
	if (ok)
	{
		for (const TextRunBox& run : page.textRuns)
		{
			if (run.text == "Un") { firstX = run.x; firstY = run.y; }
			if (run.text == "Deux") { secondX = run.x; secondY = run.y; }
		}
		for (const LinkBox& link : page.links)
		{
			if (link.url == "https://test/one") firstLink = true;
			if (link.url == "https://test/two") secondLink = true;
		}
		ok = firstX >= 0 && secondX > firstX && firstY == secondY &&
			firstLink && secondLink &&
			page.bmp.getPixel(firstX - 3, firstY + 3) ==
				Bitmap::rgb32To565(0xFF0000FF);
	}
	check(ok, "CSS inline-block garde les boutons sur une ligne et cliquables");
}

static void testCssOutOfFlowPositioning()
{
	const std::string html =
		"<html><body><div style=\"position: absolute; left: 30px; top: 20px; "
		"width: 60px; height: 24px; background: red\">ABS</div>"
		"<a href=\"/fixed\" style=\"display: block; position: fixed; "
		"right: 10px; top: 12px; width: 50px; height: 24px; "
		"background: blue\">FIX</a>"
		"<p>Normal flow</p></body></html>";
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
	int absoluteY = -1, fixedY = -1, normalY = -1;
	if (ok)
	{
		for (const TextRunBox& run : page.textRuns)
		{
			if (run.text == "ABS") absoluteY = run.y;
			else if (run.text == "FIX") fixedY = run.y;
			else if (run.text.find("Normal flow") != std::string::npos)
				normalY = run.y;
		}
		ok = absoluteY >= 20 && absoluteY <= 24 &&
			fixedY >= 12 && fixedY <= 16 &&
			normalY >= 0 && normalY < 20 && page.height < 80 &&
			!page.fixedBoxes.empty();
		bool fixedLink = false;
		for (const LinkBox& link : page.links)
			if (link.fixed && link.url == "https://test/fixed")
				fixedLink = true;
		ok = ok && fixedLink;
	}
	check(ok, "CSS position absolute et fixed hors du flux normal");

	PageModel nextSegment;
	ok = ok && layout.renderSegment(nextSegment, "https://test/", 1,
		NW_PAGE_SEGMENT_HEIGHT + NW_PAGE_SEGMENT_OVERLAP);
	bool fixedInSegment = false;
	if (ok)
		for (const TextRunBox& run : nextSegment.textRuns)
			if (run.text == "FIX" && run.y >= nextSegment.bmp.originY)
				fixedInSegment = true;
	check(ok && fixedInSegment,
		"CSS position fixed est redessine dans les segments suivants");
}

static void testCssFloatRight()
{
	const std::string html =
		"<html><body><div style=\"width: 200px; background: #eeeeee\">"
		"<span>Titre</span><span class=\"tag\">Jouable</span></div></body></html>";
	Document doc;
	bool ok = htmlParse(html, doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	css.parseStylesheet(
		".tag { float: right; background: red; padding: 2px; }");
	collectStyleBlocks(doc, css);
	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	int titleX = -1, badgeX = -1;
	if (ok)
		for (const TextRunBox& run : page.textRuns)
		{
			if (run.text == "Titre") titleX = run.x;
			if (run.text == "Jouable") badgeX = run.x;
		}
	ok = ok && titleX >= 0 && badgeX > titleX + 80 && badgeX < 190;
	check(ok, "CSS float:right place un badge en bord droit sans casser le flux");
}

static void testCssBorderStyles()
{
	auto render = [](const char* borderStyle, PageModel& page)
	{
		Document doc;
		const std::string html =
			std::string("<html><body><div style=\"background: white; border: 2px ") +
			borderStyle + " red; padding: 8px\">Bordure CSS</div></body></html>";
		if (!htmlParse(html, doc)) return false;
		WebSettings settings;
		settings.enableJS = false;
		CssEngine css;
		collectStyleBlocks(doc, css);
		PageLayout layout(doc, css, settings);
		return layout.renderSegment(page, "https://test/", 0,
			NW_PAGE_SEGMENT_HEIGHT);
	};

	PageModel dashed;
	PageModel dotted;
	bool ok = render("dashed", dashed) && render("dotted", dotted);
	int longestDashedRun = 0;
	int longestDottedRun = 0;
	if (ok)
	{
		const uint16_t red = Bitmap::rgb32To565(0xFF0000FF);
		for (int x = 0, run = 0; x < dashed.bmp.w; ++x)
		{
			run = dashed.bmp.getPixel(x, 0) == red ? run + 1 : 0;
			longestDashedRun = std::max(longestDashedRun, run);
		}
		for (int x = 0, run = 0; x < dotted.bmp.w; ++x)
		{
			run = dotted.bmp.getPixel(x, 0) == red ? run + 1 : 0;
			longestDottedRun = std::max(longestDottedRun, run);
		}
		ok = longestDottedRun > 0 && longestDashedRun > longestDottedRun;
	}
	check(ok, "CSS border-style: rendu distingue dashed et dotted");
}

static void testCssBorderSides()
{
	Document doc;
	bool ok = htmlParse(
		"<html><body><div style=\"background: white; border: 2px solid red; "
		"border-top: 4px dashed blue; border-left-width: 3px; padding: 8px\">"
		"Bordures independantes</div></body></html>", doc);
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
		const uint16_t blue = Bitmap::rgb32To565(0xFFFF0000);
		const uint16_t red = Bitmap::rgb32To565(0xFF0000FF);
		bool hasBlueTop = false;
		bool hasRedRight = false;
		for (int x = 0; x < page.bmp.w; ++x)
			if (page.bmp.getPixel(x, 0) == blue) hasBlueTop = true;
		for (int y = 0; y < std::min(64, page.bmp.h); ++y)
			for (int x = std::max(0, page.bmp.w - 10); x < page.bmp.w; ++x)
				if (page.bmp.getPixel(x, y) == red) hasRedRight = true;
		ok = hasBlueTop && hasRedRight;
	}
	check(ok, "CSS border par côté respecte styles et couleurs distincts");
}

static void testCssTextAndPercentWidths()
{
	Document doc;
	bool ok = htmlParse(
		"<html><body><div style=\"width: 50%; background: red\">"
		"<div style=\"width: 50%; background: blue; white-space: nowrap; "
		"text-transform: uppercase\">texte conserve sur une seule ligne</div>"
		"</div><p style=\"white-space: pre-line\">ligne une\nligne deux</p></body></html>",
		doc);
	WebSettings settings;
	settings.enableJS = false;
	CssEngine css;
	collectStyleBlocks(doc, css);
	PageLayout layout(doc, css, settings);
	PageModel page;
	ok = ok && layout.renderSegment(page, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	bool transformed = false;
	int preLineRuns = 0;
	if (ok)
	{
		for (const TextRunBox& run : page.textRuns)
		{
			if (run.text.find("TEXTE CONSERVE") != std::string::npos)
				transformed = true;
			if (run.text.find("ligne une") != std::string::npos ||
				run.text.find("ligne deux") != std::string::npos)
				++preLineRuns;
		}
		const uint16_t red = Bitmap::rgb32To565(0xFF0000FF);
		const uint16_t blue = Bitmap::rgb32To565(0xFFFF0000);
		ok = transformed && preLineRuns == 2 &&
			page.bmp.getPixel(50, 5) == blue &&
			page.bmp.getPixel(150, 5) == red &&
			page.bmp.getPixel(250, 5) != red;
	}
	check(ok, "CSS text-transform, white-space et largeur relative au parent");

	Document spacingDoc;
	ok = htmlParse(
		"<html><body><p>PIXEL</p>"
		"<p style=\"letter-spacing: 2px\">PIXEL</p></body></html>",
		spacingDoc);
	CssEngine spacingCss;
	collectStyleBlocks(spacingDoc, spacingCss);
	PageLayout spacingLayout(spacingDoc, spacingCss, settings);
	PageModel spacingPage;
	ok = ok && spacingLayout.renderSegment(spacingPage, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	int regularWidth = -1, spacedWidth = -1;
	if (ok)
	{
		for (const TextRunBox& run : spacingPage.textRuns)
		{
			if (run.text != "PIXEL") continue;
			if (regularWidth < 0) regularWidth = run.w;
			else spacedWidth = run.w;
		}
		ok = regularWidth >= 0 && spacedWidth - regularWidth >= 4;
		if (!ok)
		{
			printf("  letter spacing: regular=%d spaced=%d runs=%u\n",
				regularWidth, spacedWidth,
				(unsigned)spacingPage.textRuns.size());
		}
	}
	check(ok, "CSS letter-spacing ajuste la largeur et le rendu du texte");

	Document emSpacingDoc;
	ok = htmlParse(
		"<html><body><p style=\"font-size: 20px; letter-spacing: 0.1em\">"
		"PIXEL</p><p style=\"letter-spacing: 0.1em; font-size: 20px\">"
		"PIXEL</p></body></html>", emSpacingDoc);
	CssEngine emSpacingCss;
	collectStyleBlocks(emSpacingDoc, emSpacingCss);
	Style inheritedStyle = defaultStyle();
	float emWidths[2] = {};
	size_t emWidthIndex = 0;
	for (size_t i = 0; i < emSpacingDoc.nodes.size(); ++i)
	{
		if (emSpacingDoc.nodes[i].tag != "p") continue;
		const Style resolved = emSpacingCss.resolve(emSpacingDoc, (int)i,
			defaultStyle());
		if (resolved.letterSpacing != 2.0f) ok = false;
		if (emWidthIndex < 2)
			emWidths[emWidthIndex++] = TextEngine::instance().measure(
				"PIXEL", 0.55f, resolved.letterSpacing);
		inheritedStyle = resolved;
	}
	ok = ok && emWidthIndex == 2 && emWidths[0] == emWidths[1] &&
		!inheritedStyle.letterSpacingIsEm;
	check(ok, "CSS letter-spacing em suit la taille de police calculee");

	Document preWrapDoc;
	ok = htmlParse("<html><body><div style=\"width: 40px; "
		"white-space: pre-wrap\">AAAA  BBBB CCCC</div></body></html>",
		preWrapDoc);
	CssEngine preWrapCss;
	collectStyleBlocks(preWrapDoc, preWrapCss);
	PageLayout preWrapLayout(preWrapDoc, preWrapCss, settings);
	PageModel preWrapPage;
	ok = ok && preWrapLayout.renderSegment(preWrapPage, "https://test/", 0,
		NW_PAGE_SEGMENT_HEIGHT);
	std::string wrappedText;
	int firstLineY = -1;
	bool hasWrappedLine = false;
	if (ok)
	{
		for (const TextRunBox& run : preWrapPage.textRuns)
		{
			if (run.text.find_first_not_of(" ABCDEFGHIJKLMNOPQRSTUVWXYZ") !=
				std::string::npos)
				continue;
			if (run.text.empty()) continue;
			wrappedText += run.text;
			if (firstLineY < 0) firstLineY = run.y;
			else if (run.y != firstLineY) hasWrappedLine = true;
		}
		ok = wrappedText == "AAAA  BBBB CCCC" && hasWrappedLine;
	}
	check(ok, "CSS white-space: pre-wrap conserve les espaces et revient a la ligne");
}

static void testCssOverflowClipping()
{
	Document doc;
	bool ok = htmlParse(
		"<html><body><div style=\"width:40px;height:10px;overflow:hidden\">"
		"<span style=\"white-space:pre\">AAAAAAAAAAAA\nBBBBBBBBBBBB</span>"
		"</div></body></html>", doc);
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
		const uint16_t textColor = Bitmap::rgb32To565(0xFF202020);
		bool leakedOutsideClip = false;
		for (int y = 16; y < 48; ++y)
			for (int x = 0; x < page.bmp.w; ++x)
				if (page.bmp.getPixel(x, y) == textColor)
					leakedOutsideClip = true;
		for (int y = 0; y < 16; ++y)
			for (int x = 50; x < page.bmp.w; ++x)
				if (page.bmp.getPixel(x, y) == textColor)
					leakedOutsideClip = true;
		ok = !leakedOutsideClip;
	}
	check(ok, "CSS overflow:hidden decoupe le texte sur les deux axes");
}

static void testCssPercentHeights()
{
	Document doc;
	bool ok = htmlParse(
		"<html><body><div style=\"height:100px;background:red\">"
		"<div style=\"height:50%;background:blue\"></div>"
		"<div style=\"min-height:25%;max-height:40%;background:#00ff00\"></div>"
		"</div></body></html>", doc);
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
		const uint16_t blue = Bitmap::rgb32To565(0xFFFF0000);
		const uint16_t green = Bitmap::rgb32To565(0xFF00FF00);
		ok = page.bmp.getPixel(8, 5) == blue &&
			page.bmp.getPixel(8, 60) == green &&
			page.bmp.getPixel(8, 85) == Bitmap::rgb32To565(0xFF0000FF);
	}
	check(ok, "CSS height/min-height/max-height en pourcentage du parent explicite");
}

static void testCssRelativePosition()
{
	auto renderPositions = [](bool relative, TextRunBox& first,
		TextRunBox& second)
	{
		const std::string positioning = relative
			? "position:relative;left:20px;top:10px;" : "";
		const std::string html =
			"<html><body><div style=\"" + positioning +
			"background:#ff0000\">premier</div>"
			"<div style=\"background:#0000ff\">second</div>"
			"</body></html>";
		Document doc;
		if (!htmlParse(html, doc)) return false;
		WebSettings settings;
		settings.enableJS = false;
		CssEngine css;
		collectStyleBlocks(doc, css);
		PageLayout layout(doc, css, settings);
		PageModel page;
		if (!layout.renderSegment(page, "https://test/", 0,
			NW_PAGE_SEGMENT_HEIGHT))
			return false;
		for (const TextRunBox& run : page.textRuns)
		{
			if (run.text == "premier") first = run;
			else if (run.text == "second") second = run;
		}
		return !first.text.empty() && !second.text.empty();
	};

	TextRunBox relativeFirst, relativeSecond, staticFirst, staticSecond;
	const bool rendered = renderPositions(true, relativeFirst, relativeSecond) &&
		renderPositions(false, staticFirst, staticSecond);
	const bool ok = rendered &&
		relativeFirst.x == staticFirst.x + 20 &&
		relativeFirst.y == staticFirst.y + 10 &&
		relativeSecond.x == staticSecond.x &&
		relativeSecond.y == staticSecond.y;
	check(ok, "CSS position:relative decale le rendu sans deplacer le flux suivant");

	auto renderPercentPosition = [](bool relative, TextRunBox& run)
	{
		const std::string position = relative
			? "position:relative;left:10%;top:25%;" : "";
		const std::string html =
			"<html><body><div style=\"width:200px;height:100px\">"
			"<div style=\"" + position +
			"background:#ff0000\">pourcentage</div></div></body></html>";
		Document doc;
		if (!htmlParse(html, doc)) return false;
		WebSettings settings;
		settings.enableJS = false;
		CssEngine css;
		collectStyleBlocks(doc, css);
		PageLayout layout(doc, css, settings);
		PageModel page;
		if (!layout.renderSegment(page, "https://test/", 0,
			NW_PAGE_SEGMENT_HEIGHT))
			return false;
		for (const TextRunBox& text : page.textRuns)
			if (text.text == "pourcentage") run = text;
		return !run.text.empty();
	};
	TextRunBox percentStatic, percentRelative;
	const bool percentRendered =
		renderPercentPosition(false, percentStatic) &&
		renderPercentPosition(true, percentRelative);
	check(percentRendered &&
		percentRelative.x >= percentStatic.x + 18 &&
		percentRelative.x <= percentStatic.x + 20 &&
		percentRelative.y == percentStatic.y + 25,
		"CSS position:relative calcule les decalages en pourcentage");
}

static void testFlexGrowLayout()
{
	Document doc;
	bool ok = htmlParse(
		"<html><body><div style=\"display: flex; gap: 0\">"
		"<div style=\"flex-grow: 1; background: red\">A</div>"
		"<div style=\"flex-grow: 1; background: blue\">B</div>"
		"</div></body></html>", doc);
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
		ok = page.bmp.getPixel(100, 2) == red &&
			page.bmp.getPixel(300, 2) == blue;
	}
	check(ok, "Flexbox partage l'espace libre avec flex-grow");
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

static void testCssItalicText()
{
	auto render = [](const char* style, PageModel& page)
	{
		Document doc;
		const std::string html = std::string("<html><body><p style=\"font-size: 24px; ") +
			style + "\">Pixelnaute italic</p></body></html>";
		if (!htmlParse(html, doc)) return false;
		WebSettings settings;
		settings.enableJS = false;
		CssEngine css;
		collectStyleBlocks(doc, css);
		PageLayout layout(doc, css, settings);
		return layout.renderSegment(page, "https://test/", 0,
			NW_PAGE_SEGMENT_HEIGHT);
	};

	PageModel regular;
	PageModel italic;
	bool ok = render("", regular) && render("font-style: italic;", italic) &&
		regular.bmp.w == italic.bmp.w && regular.bmp.h == italic.bmp.h;
	int differentPixels = 0;
	if (ok)
	{
		const size_t pixelCount = (size_t)regular.bmp.w * regular.bmp.h;
		for (size_t i = 0; i < pixelCount; ++i)
			if (regular.bmp.px[i] != italic.bmp.px[i]) ++differentPixels;
		ok = differentPixels > 0;
	}
	check(ok, "font-style: italic modifie le rendu du texte");
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

int main(int argc, char** argv)
{
	if (argc > 1)
	{
		PdfDocument document;
		std::string error;
		if (!pdfReadFile(argv[1], document, error))
		{
			printf("PDF test failed: %s\n", error.c_str());
			return 1;
		}
		const std::string replacement = "\xEF\xBF\xBD";
		size_t replacementCount = 0;
		for (const std::string& page : document.pages)
		{
			size_t pos = 0;
			while ((pos = page.find(replacement, pos)) != std::string::npos)
			{
				++replacementCount;
				pos += replacement.size();
			}
		}
		printf("PDF pages=%zu replacements=%zu\n", document.pages.size(),
			replacementCount);
		if (!document.pages.empty())
			printf("%s\n", document.pages[0].substr(0, 6000).c_str());
		for (int zoom : { 100, 125, 150, 200, 300 })
		{
			const int width = NW_PAGE_WIDTH * zoom / 100;
			Document pageDocument;
			CssEngine css;
			WebSettings settings;
			settings.enableJS = false;
			if (!htmlParse(pdfDocumentHtml(document, "PDF test", 0, zoom),
				pageDocument))
				return 3;
			css.setViewportWidth(width);
			collectStyleBlocks(pageDocument, css);
			PageLayout layout(pageDocument, css, settings, width);
			PageModel page;
			if (!layout.renderSegment(page, "file://pdf-test", 0,
				NW_PAGE_SEGMENT_HEIGHT) || page.bmp.w != width)
				return 4;
			printf("zoom=%d%% rendered-width=%d height=%d\n", zoom,
				page.bmp.w, page.height);
		}
		return replacementCount == 0 ? 0 : 2;
	}
	printf("=== Verification PC de la recherche, du CSS et de la police ===\n\n");
	const int pdfFailures = pdfSelfTest();
	printf("PDF simple : %s\n", pdfFailures == 0 ? "OK" : "ECHEC");
	g_failures += pdfFailures;
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
	const bool imageFitOk = testImageFit();
	printf("CSS object-fit / cadrage image : %s\n", imageFitOk ? "OK" : "ECHEC");
	if (!imageFitOk) g_failures++;
	const bool encodingOk = testResponseEncoding();
	printf("Reponses binaires / encodage texte : %s\n", encodingOk ? "OK" : "ECHEC");
	if (!encodingOk) g_failures++;
	const bool urlOriginsOk = testUrlOrigins();
	printf("Origines URL / politique CORS : %s\n", urlOriginsOk ? "OK" : "ECHEC");
	if (!urlOriginsOk) g_failures++;
	testSwizzleBijection();
	testAlphaRoundTrip();
	testLinearGradient();
	testBitmapViewport();

	buildFont();
	testGlyphReadBack();
	renderSheet();
	const bool compatibilityPageOk = testCompatibilityPage();
	printf("Page de test du rendu Pixelnaute : %s\n",
		compatibilityPageOk ? "OK" : "ECHEC");
	if (!compatibilityPageOk) g_failures++;
	testBitmapCssEffects();
	testPageSegments();
	testGridLayout();
	testFlexReverseLayout();
	testRootThemeColors();
	testSegmentedBackgroundKeepsText();
	testModernCssLayout();
	testCssJustifiedText();
	testHtmlInlineWhitespace();
	testHtmlMediaFallback();
	testCssTableDisplay();
	testCssInlineBlock();
	testCssOutOfFlowPositioning();
	testCssFloatRight();
	testCssBorderStyles();
	testCssBorderSides();
	testCssTextAndPercentWidths();
	testCssOverflowClipping();
	testCssPercentHeights();
	testCssRelativePosition();
	testFlexGrowLayout();
	testPreformattedCssText();
	testCssItalicText();

	printf("\n%s (%d echec%s)\n", g_failures == 0 ? "TOUT EST OK" : "DES ECHECS",
		g_failures, g_failures > 1 ? "s" : "");
	return g_failures == 0 ? 0 : 1;
}
