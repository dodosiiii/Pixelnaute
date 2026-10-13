//=============================================================================
// tests/page_probe/main.cpp - Sonde du rendu de page et de la texture
//
// But : isoler le rendu de la page d'accueil et son passage en texture, sans
// l'interface du bas. La sonde :
//   1. construit le meme HTML que la page d'accueil interne ;
//   2. le met en page avec le vrai moteur (PageLayout) ;
//   3. ecrit le bitmap obtenu en RGB brut sur la carte SD (dump_page.raw),
//      ce qui permet de verifier le rendu sans aucun affichage ;
//   4. affiche ce bitmap via une texture RGB565 exactement comme le navigateur.
//
// Si le dump est correct mais l'ecran rouge, le probleme est dans la texture.
// Si le dump est deja rouge, le probleme est dans la mise en page.
//=============================================================================
#include <3ds.h>
#include <citro2d.h>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <string>

#include "../../source/render/layout.hpp"
#include "../../source/render/text.hpp"
#include "../../source/html/parser.hpp"
#include "../../source/css/css.hpp"
#include "../../source/settings/settings.hpp"
#include "../../source/search/search_page.hpp"

#define TEX_W 512
#define TEX_H 256
#define PAGE_H 1200

static void note(const char* text)
{
	FILE* f = fopen("sdmc:/pageprobe.txt", "a");
	if (f) { fprintf(f, "%s\n", text); fclose(f); }
}

static void noteValue(const char* label, long value)
{
	FILE* f = fopen("sdmc:/pageprobe.txt", "a");
	if (f) { fprintf(f, "%s=%ld\n", label, value); fclose(f); }
}

// Ecrit un Bitmap RGB565 en RGB888 brut.
static void dumpBitmap(const Bitmap& bmp, const char* path)
{
	if (!bmp.px || bmp.w <= 0 || bmp.h <= 0) return;
	FILE* f = fopen(path, "wb");
	if (!f) return;
	for (int y = 0; y < bmp.h; y++)
		for (int x = 0; x < bmp.w; x++)
		{
			const u16 v = bmp.px[(size_t)y * bmp.w + x];
			const u8 rgb[3] = {
				(u8)(((v >> 11) & 0x1F) * 255 / 31),
				(u8)(((v >> 5) & 0x3F) * 255 / 63),
				(u8)(((v) & 0x1F) * 255 / 31)
			};
			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
}

static inline int mortonIndex(int x, int y, int tilesX)
{
	const int tx = x / 8, ty = y / 8;
	const int lx = x & 7, ly = y & 7;
	const int m = (ly & 1) | ((lx & 1) << 1) | ((ly & 2) << 1) |
	              ((lx & 2) << 2) | ((ly & 4) << 2) | ((lx & 4) << 3);
	return (ty * tilesX + tx) * 64 + m;
}

int main()
{
	gfxInitDefault();
	fsInit();
	remove("sdmc:/pageprobe.txt");
	note("--- sonde de page ---");

	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();
	C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

	TextEngine& te = TextEngine::instance();
	note(te.init() ? "police OK" : "police INDISPONIBLE");

	//--- Verification du traitement des couleurs ---------------------------
	// Un desaccord ici expliquerait un fond de page de la mauvaise couleur.
	{
		uint32_t c = 0;
		struct { const char* text; uint32_t expected; } cases[] = {
			{ "#17131A", 0xFF1A1317u },  // fond de page demande
			{ "#241D28", 0xFF281D24u },
			{ "#F2A25C", 0xFF5CA2F2u },  // accent orange
			{ "#000000", 0xFF000000u },
			{ "#FFFFFF", 0xFFFFFFFFu },
		};
		for (const auto& t : cases)
		{
			char buf[96];
			const bool ok = cssParseColor(t.text, c);
			snprintf(buf, sizeof(buf), "couleur %s -> 0x%08X (attendu 0x%08X) %s",
				t.text, c, t.expected, (ok && c == t.expected) ? "ok" : "DIFFERENT");
			note(buf);
		}
		// Et la conversion RGB565 utilisee pour dessiner.
		char buf[96];
		snprintf(buf, sizeof(buf), "0xFF1A1317 en RGB565 -> 0x%04X",
			Bitmap::rgb32To565(0xFF1A1317u));
		note(buf);
	}

	//--- HTML : soit un fichier fourni, soit une page d'accueil de test -----
	//
	// Si sdmc:/input.html existe, c'est lui qui est mis en page. Cela permet de
	// mettre au point le rendu sur de vraies pages recuperees par net_probe,
	// sans dependre du reseau.
	std::string html;
	{
		FILE* f = fopen("sdmc:/input.html", "rb");
		if (f)
		{
			fseek(f, 0, SEEK_END);
			const long size = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (size > 0 && size < 2 * 1024 * 1024)
			{
				html.resize((size_t)size);
				const size_t got = fread(&html[0], 1, (size_t)size, f);
				html.resize(got);
				note("HTML lu depuis input.html");
			}
			fclose(f);
		}

		// Page de resultats de moteur : reecriture en blocs simples.
		std::string rewritten;
		if (searchPageRewrite(html, rewritten))
		{
			char buf[96];
			snprintf(buf, sizeof(buf), "page de resultats reecrite : %d resultats",
				searchPageLastResultCount());
			note(buf);
			html = rewritten;
		}
	}

	if (html.empty())
	{
		html =
			"<html><head><title>Test</title></head>"
			"<body style=\"background: #17131A; color: #ECE8E8\">"
			"<div style=\"text-align: center; margin-top: 26px\">"
			"<span style=\"font-size: 40px; font-weight: bold; color: #F2A25C\">N3DS</span>"
			"<span style=\"font-size: 40px; font-weight: bold\"> Web</span></div>"
			"<div style=\"background: #241D28; padding: 8px; margin: 10px\">"
			"<div style=\"font-size: 13px; font-weight: bold; color: #F2A25C\">RACCOURCIS</div>"
			"<a href=\"https://exemple.fr\" style=\"display: block; background: #322839; "
			"padding: 9px; margin-bottom: 5px; font-size: 17px\">DuckDuckGo</a>"
			"<a href=\"https://exemple.fr\" style=\"display: block; background: #322839; "
			"padding: 9px; font-size: 17px\">Wikipedia</a>"
			"</div>"
			"</body></html>";
		note("HTML de test utilise");
	}

	Document doc;
	if (!htmlParse(html, doc)) { note("htmlParse ECHEC"); }
	CssEngine css;
	collectStyleBlocks(doc, css);

	WebSettings settings;
	settings.maxPageHeight = PAGE_H;
	PageLayout layout(doc, css, settings);
	PageModel page;
	const bool rendered = layout.render(page, "nw://home");
	note(rendered ? "mise en page OK" : "mise en page ECHEC");
	noteValue("page_height", page.height);
	noteValue("page_width", page.bmp.w);
	noteValue("liens", (long)page.links.size());

	//--- Etat du bitmap APRES la mise en page, avant tout affichage --------
	// Un releve par ligne : la couleur dominante des 12 premieres lignes et
	// de quelques lignes plus bas. Si le rouge apparait partout des le debut,
	// le probleme est dans le rendu de page lui-meme.
	if (page.bmp.px)
	{
		auto lineColor = [&](int y, char* out) {
			// couleur la plus frequente de la ligne
			u16 best = 0;
			int bestCount = 0;
			for (int x = 0; x < page.bmp.w; x++)
			{
				const u16 v = page.bmp.px[(size_t)y * page.bmp.w + x];
				int n = 0;
				for (int k = 0; k < page.bmp.w; k++)
					if (page.bmp.px[(size_t)y * page.bmp.w + k] == v) n++;
				if (n > bestCount) { bestCount = n; best = v; }
			}
			const int r = (best >> 11) & 0x1F, g = (best >> 5) & 0x3F, b = best & 0x1F;
			snprintf(out, 64, "ligne %d : 0x%04X RGB(%d,%d,%d) x%d",
				y, best, r * 255 / 31, g * 255 / 63, b * 255 / 31, bestCount);
		};
		int probes[6] = { 0, 1, 30, 60, 100, 200 };
		for (int i = 0; i < 6; i++)
		{
			if (probes[i] < page.height && probes[i] < page.bmp.h)
			{
				char buf[96];
				lineColor(probes[i], buf);
				note(buf);
			}
		}
		// Valeur exacte du fond de page a trois endroits, pour comparaison avec
		// la couleur demandee (#17131A).
		{
			char buf[96];
			const u16 v = page.bmp.px[0];
			snprintf(buf, sizeof(buf), "coin haut-gauche : 0x%04X", v);
			note(buf);
		}
	}

	// Couleur dominante du rendu : dit si la page elle-meme est rouge.
	if (page.bmp.px)
	{
		int reds = 0, total = 0;
		for (int y = 0; y < page.height && y < page.bmp.h; y++)
			for (int x = 0; x < page.bmp.w; x++)
			{
				const u16 v = page.bmp.px[(size_t)y * page.bmp.w + x];
				const int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
				// "rouge" = canal rouge fort, vert et bleu faibles
				if (r > 24 && g < 16 && b < 16) reds++;
				total++;
			}
		noteValue("pixels_rouges", reds);
		noteValue("pixels_total", total);
		dumpBitmap(page.bmp, "sdmc:/dump_page.raw");
		note("dump_page.raw ecrit");
	}

	//--- Etalonnage : largeur et hauteur d'encre reellement dessinees --------
	// Mesure sur un bitmap vierge, sans mise en page : compare l'avance
	// annoncee par la police a l'encre effectivement produite. Si l'encre est
	// beaucoup plus large, le facteur d'echelle du rendu est en cause.
	{
		Bitmap cal;
		cal.create(600, 60);
		const uint16_t bg = Bitmap::rgb32To565(0xFF161E16u);
		cal.clear(0xFF161E16u);

		const char* word = "NNNNNNNN";
		const float expected = te.measure(word, 1.0f);
		te.drawLine(cal, word, 4, 4, 1.0f, 0xFFFFFFFFu, false, false);

		int first = -1, last = -1, top = -1, bottom = -1;
		for (int x = 0; x < cal.w; x++)
			for (int y = 0; y < cal.h; y++)
				if (cal.px[(size_t)y * cal.w + x] != bg)
				{
					if (first < 0) first = x;
					last = x;
					if (top < 0 || y < top) top = y;
					if (y > bottom) bottom = y;
					break;
				}

		FILE* f = fopen("sdmc:/pageprobe.txt", "a");
		if (f)
		{
			fprintf(f, "\n[etalonnage]\n");
			fprintf(f, "cellule=%dx%d text_scale=%.4f base=%.3f\n",
				te.cellWidth(), te.cellHeight(), (double)te.textScale(), 1.0);
			fprintf(f, "avance annoncee pour 8 'N' : %.2f px\n", (double)expected);
			fprintf(f, "encre dessinee : largeur %d px (colonnes %d..%d), "
				"hauteur %d px (lignes %d..%d)\n",
				last - first + 1, first, last, bottom - top + 1, top, bottom);
			const int giN = te.glyphIndexOfCodePoint('N');
			fprintf(f, "glyphe N : encre annoncee largeur=%.2f avance=%.2f\n",
				(double)te.glyphInkWidth(giN), (double)te.glyphAdvance(giN));
			if (expected > 1.0f)
				fprintf(f, "rapport encre/avance = %.2f (1.0 = normal)\n",
					(double)(last - first + 1) / (double)expected);
			fclose(f);
		}
		cal.destroy();
	}

	//--- texture identique a celle du navigateur ---
	C3D_Tex tex;
	void* texData = linearAlloc(TEX_W * TEX_H * 2);
	bool texOk = texData && C3D_TexInit(&tex, TEX_W, TEX_H, GPU_RGB565);
	note(texOk ? "texture OK" : "texture ECHEC");
	if (texOk)
	{
		memset(texData, 0, TEX_W * TEX_H * 2);
		u16* dst = (u16*)texData;
		const int tilesX = TEX_W / 8;
		const int rows = (page.height < 240) ? page.height : 240;
		const int cols = (page.bmp.w < 400) ? page.bmp.w : 400;
		for (int r = 0; r < rows; r++)
			for (int c = 0; c < cols; c++)
				dst[mortonIndex(c, r, tilesX)] = page.bmp.px[(size_t)r * page.bmp.w + c];
		C3D_TexUpload(&tex, texData);
		C3D_TexSetFilter(&tex, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	}

	Tex3DS_SubTexture sub = { 400, 240, 0.0f, 1.0f, 400.0f / TEX_W,
		1.0f - 240.0f / TEX_H };
	C2D_Image img = { &tex, &sub };

	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		// Fond volontairement distinctif : si cette couleur apparait a l'ecran,
		// c'est que la texture n'a pas ete dessinee.
		C2D_TargetClear(top, C2D_Color32(0x00, 0x80, 0x00, 0xFF)); // vert
		C2D_SceneBegin(top);
		if (texOk)
		{
			C2D_DrawParams p;
			memset(&p, 0, sizeof(p));
			p.pos.x = 0; p.pos.y = 0;
			p.pos.w = 400; p.pos.h = 240;
			p.depth = 0.0f;
			C2D_DrawImage(img, &p, NULL);
		}
		C3D_FrameEnd(0);
	}

	if (texData) linearFree(texData);
	C2D_Fini();
	C3D_Fini();
	fsExit();
	gfxExit();
	return 0;
}
