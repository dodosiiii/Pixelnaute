#pragma once
//=============================================================================
// morton.hpp - Entrelacement des textures PICA200 (tuiles 8x8)
//
// Les textures de la 3DS ne sont pas rangees ligne par ligne : chaque tuile de
// 8x8 pixels est entrelacee en ordre Morton (Z-order). Il faut donc ranger les
// pixels dans cet ordre avant de les envoyer a la memoire graphique.
//
// ORDRE DES BITS : x occupe les bits BAS, y vient ensuite.
//
//   index = x0 | (y0 << 1) | (x1 << 2) | (y1 << 3) | (x2 << 4) | (y2 << 5)
//
// Autrement dit, dans une tuile, les pixels sont ranges dans cet ordre :
//   (0,0) (1,0) (0,1) (1,1) (2,0) (3,0) (2,1) (3,1) ...
// Le x varie donc le plus vite, ce qui est la convention du PICA200.
//
// ATTENTION au piege : ecrire
//   (ly & 1) | ((lx & 1) << 1) | ((ly & 2) << 1) | ...
// inverse x et y (y0 se retrouve au bit 0). La tuile est alors transposee par
// rapport a la diagonale. Consequence visible : les aplats unis restent
// corrects (un fond plein ne change pas), mais tout detail est dechiquete —
// le texte est eclate en morceaux et les bords horizontaux deviennent des
// dents de peigne tous les 8 pixels. C'est ce qui donne un aspect brouille.
//
// La police systeme utilise le meme entrelacement : voir font_decode.hpp,
// dont la formule est identique et correcte.
//=============================================================================

// Position memoire, dans une texture, du pixel (x, y) de la tuile 8x8.
// `tilesX` = nombre de tuiles par ligne de texture (largeur / 8).
static inline int mortonTileIndex(int x, int y, int tilesX)
{
	const int tileX = x >> 3;
	const int tileY = y >> 3;
	const int lx = x & 7;
	const int ly = y & 7;

	// x aux bits bas, y ensuite : (x0, y0, x1, y1, x2, y2).
	const int within =
		(lx & 1)        | ((ly & 1) << 1) |
		((lx & 2) << 1) | ((ly & 2) << 2) |
		((lx & 4) << 2) | ((ly & 4) << 3);

	return (tileY * tilesX + tileX) * 64 + within;
}
