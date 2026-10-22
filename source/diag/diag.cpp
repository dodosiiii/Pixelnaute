#include "diag.hpp"
#include <3ds/allocator/linear.h>
#include <malloc.h>
#include <stdio.h>

static const char* g_stage = "boot";
static size_t g_heapPeakUsed = 0;

//--------------------------------------------------------------------------
// Jalons de demarrage
//
// Chaque jalon est ecrit puis le fichier est referme immediatement : sans
// fermeture, un plantage perdrait le tampon et on ne saurait pas ou
// l'application s'est arretee. Le cout est negligeable (une dizaine de
// jalons au demarrage).
//--------------------------------------------------------------------------
void diagBoot(int step, const char* label)
{
	FILE* f = fopen("sdmc:/N3DSWeb/boot.txt", step == 0 ? "w" : "a");
	if (!f) return;
	fprintf(f, "etape=%d %s\n", step, label ? label : "");
	fclose(f);
}

void diagBootNote(const char* label, long value)
{
	FILE* f = fopen("sdmc:/N3DSWeb/boot.txt", "a");
	if (!f) return;
	fprintf(f, "%s=%ld\n", label ? label : "note", value);
	fclose(f);
}

size_t diagHeapPeakUsed()
{
	const struct mallinfo memory = mallinfo();
	const size_t used = memory.uordblks > 0 ? (size_t)memory.uordblks : 0;
	if (used > g_heapPeakUsed) g_heapPeakUsed = used;
	return g_heapPeakUsed;
}

size_t diagApplicationHeapFree()
{
	const struct mallinfo memory = mallinfo();
	if (memory.uordblks < 0) return 0;
	const size_t used = (size_t)memory.uordblks;
	const size_t total = envGetHeapSize();
	return used < total ? total - used : 0;
}

void diagBootMemory(const char* stage)
{
	const struct mallinfo memory = mallinfo();
	const size_t used = memory.uordblks > 0 ? (size_t)memory.uordblks : 0;
	const size_t freeBytes = diagApplicationHeapFree();
	const size_t arenaFree = memory.fordblks > 0 ? (size_t)memory.fordblks : 0;
	if (used > g_heapPeakUsed) g_heapPeakUsed = used;

	FILE* f = fopen("sdmc:/N3DSWeb/boot.txt", "a");
	if (!f) return;
	fprintf(f, "memoire_etape=%s tas_utilise=%lu tas_total=%lu tas_libre=%lu tas_arene_libre=%lu pic_utilise=%lu lineaire_libre=%lu\n",
		stage ? stage : "inconnue",
		(unsigned long)used,
		(unsigned long)envGetHeapSize(),
		(unsigned long)freeBytes,
		(unsigned long)arenaFree,
		(unsigned long)g_heapPeakUsed,
		(unsigned long)linearSpaceFree());
	fclose(f);
}

void diagStage(const char* label)
{
	g_stage = label ? label : "";
	FILE* f = fopen("sdmc:/N3DSWeb/step.txt", "w");
	if (!f) return;
	fprintf(f, "%s\n", g_stage);
	fclose(f);
}

void diagCrash(const char* reason)
{
	FILE* f = fopen("sdmc:/N3DSWeb/crash.txt", "a");
	if (!f) return;
	fprintf(f, "%s pendant : %s, heap=%lu, tas_libre=%lu, mem_lineaire_libre=%lu\n",
		reason ? reason : "arret", g_stage,
		(unsigned long)envGetHeapSize(),
		(unsigned long)diagApplicationHeapFree(),
		(unsigned long)linearSpaceFree());
	fclose(f);
}

//--------------------------------------------------------------------------
// Ecriture d'un tampon de pixels RGB565.
//
// Les dimensions sont bornees : un framebuffer de taille inattendue ferait
// sinon ecrire des dizaines de mega-octets et echouer l'ecriture entiere.
//--------------------------------------------------------------------------
static bool writePixels(const char* path, const u16* pixels, int width, int height)
{
	if (!pixels || width <= 0 || height <= 0) return false;
	if (width > 1024 || height > 1024) return false;
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	const size_t count = (size_t)width * (size_t)height;
	const size_t written = fwrite(pixels, sizeof(u16), count, f);
	fclose(f);
	return written == count;
}

bool diagDump(const DiagSnapshot& snap,
	const u16* topPixels, int topWidth, int topHeight,
	const u16* uiPixels, int uiWidth, int uiHeight)
{
	const bool topOk = writePixels("sdmc:/N3DSWeb/diag_top.bin", topPixels,
		topWidth, topHeight);
	const bool uiOk = writePixels("sdmc:/N3DSWeb/diag_ui.bin", uiPixels,
		uiWidth, uiHeight);

	FILE* f = fopen("sdmc:/N3DSWeb/diag.txt", "w");
	if (!f) return false;

	fprintf(f, "N3DSWeb diagnostic\n");
	fprintf(f, "version=" NW_VERSION_STRING "\n");
	fprintf(f, "top=%dx%d top_dump=%s\n", topWidth, topHeight,
		topOk ? "ok" : "echec");
	fprintf(f, "ui=%dx%d ui_dump=%s\n", uiWidth, uiHeight,
		uiOk ? "ok" : "echec");
	fprintf(f, "page_height=%d scroll=%d tabs=%d\n",
		snap.pageHeight, snap.scrollY, snap.tabCount);

	fprintf(f, "\n[police systeme]\n");
	fprintf(f, "font_ready=%d\n", snap.font.fontReady ? 1 : 0);
	if (snap.font.fontReady)
	{
		fprintf(f, "cell=%dx%d\n", snap.font.cellWidth, snap.font.cellHeight);
		fprintf(f, "sheet_width=%d\n", snap.font.sheetWidth);
		fprintf(f, "sheet_height=%d\n", snap.font.sheetHeight);
		fprintf(f, "rows=%d\n", snap.font.nRows);
		fprintf(f, "lines=%d\n", snap.font.nLines);
		fprintf(f, "per_sheet=%d\n", snap.font.charPerSheet);
		fprintf(f, "sheets=%d\n", snap.font.sheetCount);
		fprintf(f, "sheet_fmt=%d\n", snap.font.sheetFmt);
		fprintf(f, "sheet_size=%u\n", (unsigned)snap.font.sheetSize);
		fprintf(f, "text_scale=%.4f\n", (double)snap.font.textScale);
		fprintf(f, "fallback=%d\n", snap.font.fallback);
		fprintf(f, "glyph_index_min=%d\n", snap.font.glyphMin);
		fprintf(f, "glyph_index_max=%d\n", snap.font.glyphMax);
		fprintf(f, "sheet_index_max=%d\n", snap.font.sheetMax);

		// Caracteres de reference : indice de glyphe et avance. Un caractere
		// latin dont l'indice tombe dans les 200 signifie que la table de
		// correspondance renvoyee par le systeme n'est pas celle attendue.
		for (int i = 0; i < snap.font.probeCount; i++)
			fprintf(f, "probe[%s]=index %d, advance %.2f\n",
				snap.font.probeLabel[i], snap.font.probeIndex[i],
				(double)snap.font.probeAdvance[i]);
	}
	fclose(f);
	return true;
}

//--------------------------------------------------------------------------
// Raster d'un glyphe, ecrit a part comme une image 1 octet par pixel.
// Permet de verifier visuellement le decodage sans console de debogage.
//--------------------------------------------------------------------------
bool diagDumpGlyph(const char* path, const u8* alpha, int width, int height)
{
	if (!alpha || width <= 0 || height <= 0) return false;
	if (width > 512 || height > 512) return false;
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	const size_t written = fwrite(alpha, 1, (size_t)width * (size_t)height, f);
	fclose(f);
	return written == (size_t)width * (size_t)height;
}
