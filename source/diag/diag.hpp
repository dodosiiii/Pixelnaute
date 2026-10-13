#pragma once
//=============================================================================
// diag.hpp - Diagnostic de demarrage et instantane d'affichage (carte SD)
//
// L'application n'a pas de console de debogage. Elle ecrit donc sur la carte
// SD de quoi diagnostiquer un probleme a distance :
//
//   sdmc:/N3DSWeb/boot.txt      - jalons de demarrage (voir diagBoot)
//   sdmc:/N3DSWeb/diag.txt      - metadonnees de l'instantane
//   sdmc:/N3DSWeb/diag_top.bin  - pixels RGB565 de l'ecran du haut
//
// Les jalons de demarrage sont ecrits et refermes immediatement : si
// l'application plante, le dernier jalon ecrit indique ou.
//=============================================================================
#include "../common.hpp"
#include <3ds.h>

//--- jalons de demarrage -----------------------------------------------------
// Ecrit une ligne "etape=<n> <libelle>" dans boot.txt et ferme le fichier.
// La premiere etape (0) recree le fichier, les suivantes l'allongent.
void diagBoot(int step, const char* label);
// Ajoute une ligne libre (mesure, erreur, valeur de service).
void diagBootNote(const char* label, long value);
// Ecrase step.txt avec la derniere etape active (utile apres un plantage).
void diagStage(const char* label);
// Ajoute une entree crash.txt sans dependance aux exceptions C++.
void diagCrash(const char* reason);

//--- instantane d'affichage --------------------------------------------------
struct DiagFontInfo
{
	bool  fontReady = false;
	int   cellWidth = 0;
	int   cellHeight = 0;
	int   sheetWidth = 0;
	int   sheetHeight = 0;
	int   nRows = 0;
	int   nLines = 0;
	int   sheetFmt = -1;
	int   charPerSheet = 0;
	u32   sheetSize = 0;
	int   fallback = 0;
	float textScale = 0.0f;
	// Bornes des glyphes rencontres : si un indice depasse nSheets *
	// charPerSheet, la lecture sortirait de la police.
	int   glyphMin = 0;
	int   glyphMax = 0;
	int   sheetMax = 0;
	int   sheetCount = 0;

	// Caracteres de reference : indice de glyphe et avance, pour verifier que
	// la table de correspondance du systeme est bien celle attendue.
	int   probeCount = 0;
	char  probeLabel[8][8] = {};
	int   probeIndex[8] = {};
	float probeAdvance[8] = {};
};

struct DiagSnapshot
{
	u32 magic = 0;                 // 'N3DW'
	int topWidth = 0, topHeight = 0;
	int pageHeight = 0, scrollY = 0;
	int tabCount = 0;
	DiagFontInfo font;
};

// Ecrit diag.txt et, si les pointeurs sont fournis, les images brutes.
bool diagDump(const DiagSnapshot& snap,
	const u16* topPixels, int topWidth, int topHeight,
	const u16* uiPixels, int uiWidth, int uiHeight);

// Ecrit le raster d'un glyphe (1 octet par pixel, largeur x hauteur).
bool diagDumpGlyph(const char* path, const u8* alpha, int width, int height);
