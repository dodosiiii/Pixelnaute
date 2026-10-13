#pragma once
//=============================================================================
// font_decode.hpp - Decodage des feuilles de glyphes de la police systeme 3DS
//
// Ce fichier ne depend PAS de la 3DS : il est partage entre le code de la
// console (render/text.cpp) et le test PC (tests/pc/main_pc.cpp). Ainsi la
// verification hors console porte sur exactement le meme code.
//
// Format de la police partagee (ffnt / CFNT) :
//   - les glyphes sont ranges dans des feuilles de texture ;
//   - une feuille est une grille de cellules de largeur (cellWidth+1) et de
//     hauteur (cellHeight+1) : le +1 est une gouttiere d'un texel qui evite
//     que le filtrage lineaire ne deborde d'un glyphe sur le voisin ;
//   - sur la New 3DS la feuille est en A4 (4 bits par pixel, format GPU 11),
//     donc 0,5 octet par pixel ;
//   - les octets d'une feuille sont ENTRELACES en tuiles de 8x8 (ordre
//     Morton / Z), et non lineaires. C'est le point qui fait echouer un
//     parcours naif : on obtient alors des blocs opaques au lieu de lettres.
//
// Valeurs constatees sur la police systeme : cellWidth 24, cellHeight 30,
// sheetWidth 128, sheetHeight 32, nRows 5, nLines 1, sheetFmt 11, sheetSize
// 2048 octets. Ces valeurs ne doivent jamais etre codees en dur : elles sont
// lues dans TGLP a l'execution (une police de remplacement peut avoir un
// format A8 et d'autres dimensions).
//=============================================================================

namespace fontdecode {

// Formule d'entrelacement 8x8 (Morton / Z-order) d'une feuille de glyphes.
// Renvoie la position du pixel (x,y) dans la feuille.
//
//   dataX = (x&1) + 4*((x>>1)&1) + 16*((x>>2)&1) + 64*(x>>3)
//   dataY = 2*(y&1) + 8*((y>>1)&1) + 32*((y>>2)&1) + (sheetWidth*8)*(y>>3)
//
// Pour une feuille 128x32 les 4096 pixels donnent 4096 positions distinctes,
// la plus grande valant 4095.
inline int sheetPixelPos(int x, int y, int sheetWidth)
{
	const int dataX = (x & 1) + (((x >> 1) & 1) << 2) + (((x >> 2) & 1) << 4) +
	                  ((x >> 3) << 6);
	const int dataY = ((y & 1) << 1) + (((y >> 1) & 1) << 3) +
	                  (((y >> 2) & 1) << 5) + ((y >> 3) * sheetWidth * 8);
	return dataX + dataY;
}

// Decode l'alpha (0-255) d'une position de la feuille selon le format.
// Les formats reconnus sont A4 (11), LA4 (9), A8 (8), LA8 (5), RGBA8 (0).
// Un format inconnu renvoie 0 : mieux vaut ne rien dessiner qu'un bloc opaque.
inline unsigned char alphaAtPixelPos(const unsigned char* sheet, int pos, int fmt)
{
	switch (fmt)
	{
	case 0xB: // GPU_A4 : 2 pixels par octet, nibble bas en premier
	case 0x9: // GPU_LA4 : meme entrelacement
		return (unsigned char)(((sheet[pos >> 1] >> ((pos & 1) * 4)) & 0x0F) * 0x11);
	case 0x8: // GPU_A8
		return sheet[pos];
	case 0x5: // GPU_LA8 : luminance puis alpha
		return sheet[(size_t)pos * 2 + 1];
	case 0x0: // GPU_RGBA8
		return sheet[(size_t)pos * 4 + 3];
	default:
		return 0;
	}
}

// Ecrit l'alpha d'une position de feuille (uniquement A4 et A8).
// Utilise par le test PC pour fabriquer une feuille de reference.
inline void setAlphaAtPixelPos(unsigned char* sheet, int pos, int fmt,
	unsigned char alpha)
{
	switch (fmt)
	{
	case 0xB:
	case 0x9:
		{
			const unsigned char nib = (unsigned char)((alpha >> 4) & 0x0F);
			const int byteIndex = pos >> 1;
			const int shift = (pos & 1) * 4;
			sheet[byteIndex] = (unsigned char)((sheet[byteIndex] &
				~(0x0F << shift)) | (nib << shift));
		}
		break;
	case 0x8:
		sheet[pos] = alpha;
		break;
	default:
		break;
	}
}

} // namespace fontdecode
