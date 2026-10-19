#pragma once
//=============================================================================
// common.hpp - Types utilitaires, limites et couleurs partages
//=============================================================================
#include <3ds.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define NW_VERSION_MAJOR 0
#define NW_VERSION_MINOR 5
#define NW_VERSION_STRING "0.5.0"

//-----------------------------------------------------------------------------
// Limites memoire (stabilite avant tout sur New 3DS)
//-----------------------------------------------------------------------------
enum
{
	NW_MAX_HTML_BYTES    = 2 * 1024 * 1024,   // taille maximale d'une page HTML en RAM
	NW_MAX_DOM_NODES     = 8192,              // evite qu'une page complexe epuise la RAM
	NW_MAX_BODY_BYTES    = 8 * 1024 * 1024,   // taille maximale d'un telechargement
	NW_MAX_PAGE_HEIGHT   = 2000,              // limite la memoire du rendu logiciel (px)
	NW_PAGE_SEGMENT_HEIGHT = 2000,            // hauteur de contenu ajoutee par segment
	NW_MAX_PAGE_SEGMENTS = 2,                 // au plus 4 000 px de contenu logique
	NW_MAX_PAGE_CONTENT_HEIGHT = NW_PAGE_SEGMENT_HEIGHT * NW_MAX_PAGE_SEGMENTS,
	NW_PAGE_WIDTH        = 400,               // largeur de la page / ecran top
	NW_TOP_HEIGHT        = 240,               // hauteur ecran haut
	NW_PAGE_SEGMENT_OVERLAP = NW_TOP_HEIGHT, // recouvrement pour garder l'ecran complet au raccord
	NW_BOTTOM_WIDTH      = 320,               // largeur ecran bas
	NW_BOTTOM_HEIGHT     = 240,               // hauteur ecran bas
	NW_MAX_TABS          = 4,                 // onglets simultanes (RAM limitee)
	NW_MAX_HISTORY       = 64,                // entree de l'historique par onglet
	NW_MAX_REDIRECTS     = 6,                 // redirections suivies
	NW_MAX_FIELDS        = 64,                // champs de formulaire sur une page
	NW_MAX_LINKS         = 4096,              // liens sur une page
	NW_HTTP_CHUNK        = 16 * 1024,         // taille de lecture reseau
	NW_SCALE_TEXT        = 8,                 // echelle de grille du rendu texte (aucun)
};

//-----------------------------------------------------------------------------
// Couleurs (format C2D_Color32 : 0xAABBGGRR = AA BB GG RR)
//
// Palette sombre "chaleureuse" : fond prune, accents orange sable. Elle est
// appliquee a l'ecran bas, aux pages internes et aux pages web qui ne
// definissent aucune couleur (la page d'accueil force les siennes).
//-----------------------------------------------------------------------------
enum Colors
{
	C_BLACK    = 0xFF000000,
	C_WHITE    = 0xFFF8F8F8,

	// Fonds
	C_BG       = 0xFF17131A,   // fond general de l'interface
	C_BG_2     = 0xFF1F1A23,   // fond secondaire (rangees alternees)
	C_SURFACE  = 0xFF2A2230,   // surface surelevee (barre d'adresse, touches)
	C_SURFACE2 = 0xFF352B3C,   // surface survolee / active

	// Texte
	C_TEXT     = 0xFFECE8E8,   // texte principal
	C_TEXT_D   = 0xFF9A8A8A,   // texte secondaire
	C_TEXT_DD  = 0xFF6E6270,   // texte discret

	// Accents
	C_ACCENT   = 0xFFF2A25C,   // accent principal (orange sable)
	C_ACCENT_D = 0xFFB8753C,   // accent attenue
	C_LINK     = 0xFFF2A25C,   // liens
	C_GREEN    = 0xFF6FCF6F,
	C_RED      = 0xFF5C5CF0,
	C_ORANGE   = 0xFFF2A25C,

	// Touches du clavier
	C_KEY      = 0xFF332A38,
	C_KEY_HI   = 0xFF4A3E52,
	C_KEY_OK   = 0xFFF2A25C,

	// Bordures / separateurs
	C_LINE     = 0xFF3A3140,
	C_GRAY     = 0xFF9A9A9A,
	C_GRAY_L   = 0xFFD0D0D0,
	C_GRAY_D   = 0xFF3A3140,

	// Anciens noms conserves pour la compatibilite des pages internes.
	C_NAVY     = 0xFF17131A,
	C_NAVY_2   = 0xFF1F1A23,
	C_BLUE     = 0xFFF2A25C,
	C_BLUE_L   = 0xFFB8753C,
};

//-----------------------------------------------------------------------------
// Petites fonctions chaînes
//-----------------------------------------------------------------------------
static inline bool strStartsWith(const std::string& s, const char* prefix)
{
	return s.compare(0, strlen(prefix), prefix) == 0;
}

static inline bool strEndsWith(const std::string& s, const char* suffix)
{
	size_t ls = strlen(suffix);
	return s.size() >= ls && s.compare(s.size() - ls, ls, suffix) == 0;
}

static inline std::string toLower(const std::string& in)
{
	std::string out = in;
	for (auto& c : out)
		if (c >= 'A' && c <= 'Z') c += 32;
	return out;
}

static inline std::string strTrim(const std::string& in)
{
	size_t b = 0, e = in.size();
	while (b < e && (in[b] == ' ' || in[b] == '\t' || in[b] == '\r' || in[b] == '\n')) b++;
	while (e > b && (in[e-1] == ' ' || in[e-1] == '\t' || in[e-1] == '\r' || in[e-1] == '\n')) e--;
	return in.substr(b, e - b);
}

static inline std::string itos(long v)
{
	char tmp[24];
	snprintf(tmp, sizeof(tmp), "%ld", v);
	return tmp;
}

static inline std::string toHex(uint32_t v)
{
	char tmp[16];
	snprintf(tmp, sizeof(tmp), "%08lX", (unsigned long)v);
	return tmp;
}

static inline std::string ftos1(float v)
{
	char tmp[24];
	snprintf(tmp, sizeof(tmp), "%.1f", (double)v);
	return tmp;
}

//-----------------------------------------------------------------------------
// UTF-8 helpers
//-----------------------------------------------------------------------------
// Retourne la longueur (octets) du caractere a l'adresse p, met a jour cp.
// Retourne 0 a la fin de chaine, 1 si octet invalide (=> cp = 0xFFFD).
static inline int utf8Decode(const char* p, uint32_t& cp)
{
	const unsigned char* s = (const unsigned char*)p;
	if (s[0] == 0) { cp = 0; return 0; }
	if (s[0] < 0x80) { cp = s[0]; return 1; }
	if (s[0] < 0xC2) { cp = 0xFFFD; return 1; }
	if (s[0] < 0xE0 && (s[1] & 0xC0) == 0x80)
	{
		cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
		return 2;
	}
	if (s[0] < 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
	{
		cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
		return 3;
	}
	if (s[0] < 0xF8 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80)
	{
		cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12)
			| ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
		return 4;
	}
	cp = 0xFFFD;
	return 1;
}

// Encodage UTF-8 d'un codepoint.
static inline void utf8Append(std::string& out, uint32_t cp)
{
	if (cp < 0x80) { out += (char)cp; return; }
	if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); return; }
	if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); return; }
	out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
	out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
}

//-----------------------------------------------------------------------------
// Encodage URL (x-www-form-urlencoded)
//-----------------------------------------------------------------------------
static inline std::string urlEncode(const std::string& in)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : in)
	{
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~')
			out += (char)c;
		else
		{
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	return out;
}

// Decodage x-www-form-urlencoded ("+" -> espace, %XX -> octet).
static inline std::string urlDecode(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); i++)
	{
		char c = in[i];
		if (c == '+') { out += ' '; continue; }
		if (c == '%' && i + 2 < in.size())
		{
			auto hexVal = [](char h) -> int {
				if (h >= '0' && h <= '9') return h - '0';
				if (h >= 'a' && h <= 'f') return h - 'a' + 10;
				if (h >= 'A' && h <= 'F') return h - 'A' + 10;
				return -1;
			};
			int hi = hexVal(in[i + 1]);
			int lo = hexVal(in[i + 2]);
			if (hi >= 0 && lo >= 0)
			{
				out += (char)((hi << 4) | lo);
				i += 2;
				continue;
			}
		}
		out += c;
	}
	return out;
}

// Convertit les accents UTF-8 (2 octets) en caracteres simples, et supprime
// les caracteres non representables (CJK, emoji, symboles exotiques).
//
// Pourquoi : la police de la console ne dispose pas des milliers de glyphes
// du japonais ou du chinois, et le moteur de rendu travaille caractere par
// caractere. Laisser passer de l'UTF-8 produit des suites illisibles du genre
// "(niÌ"nÌ"ndoÌ" 3DS, Nintendo ï¿½DS?)". On ramene donc le texte a un
// alphabet sur : ASCII + les accents latins courants.
static inline std::string utf8ToSimplified(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	size_t i = 0;
	while (i < in.size())
	{
		uint32_t cp = 0;
		const int n = utf8Decode(in.c_str() + i, cp);
		if (n == 0) break;
		i += (size_t)n;

		if (n == 1)
		{
			// Octet simple.
			if (cp == 0xFFFD)
			{
				out += ' ';
				continue;
			}
			const unsigned char c = (unsigned char)cp;
			if (c == '\t' || c == '\n' || c == '\r' || (c >= 32 && c < 127))
				out += (char)c;
			else if (c >= 160)
				out += (char)c;   // deja du latin-1
			else
				out += ' ';
			continue;
		}

		// Accents latins : ramenes a la lettre de base.
		switch (cp)
		{
			case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4:
			case 0x00C5: case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3:
			case 0x00E4: case 0x00E5:
				out += (cp < 0xE0) ? 'A' : 'a'; break;
			case 0x00C7: case 0x00E7: out += (cp < 0xE0) ? 'C' : 'c'; break;
			case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
			case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
				out += (cp < 0xE0) ? 'E' : 'e'; break;
			case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
			case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
				out += (cp < 0xE0) ? 'I' : 'i'; break;
			case 0x00D1: case 0x00F1: out += (cp < 0xE0) ? 'N' : 'n'; break;
			case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6:
			case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6:
				out += (cp < 0xE0) ? 'O' : 'o'; break;
			case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
			case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
				out += (cp < 0xE0) ? 'U' : 'u'; break;
			case 0x00DD: case 0x00FD: case 0x00FF: out += (cp < 0xE0) ? 'Y' : 'y'; break;
			case 0x0152: out += "OE"; break;
			case 0x0153: out += "oe"; break;
			case 0x00DF: out += "ss"; break;
			case 0x00C6: out += "AE"; break;
			case 0x00E6: out += "ae"; break;
			// Ponctuation typographique.
			case 0x2018: case 0x2019: case 0x201A: out += '\''; break;
			case 0x201C: case 0x201D: case 0x201E: out += '"'; break;
			case 0x2013: case 0x2014: out += '-'; break;
			case 0x2026: out += '.'; break;
			case 0x00A0: out += ' '; break;
			case 0x00B0: out += 'o'; break;   // degre
			case 0x20AC: out += 'E'; break;   // euro
			case 0x00AB: case 0x00BB: out += '"'; break;
			// Symboles divers : on garde un equivalent lisible.
			case 0x00BD: out += ' '; break;
			case 0x00B7: out += '.'; break;
			// Tout le reste (CJK, emoji, pictogrammes) est retire : la police
			// de la console ne les a pas et ils encombrent la ligne.
			default: break;
		}
	}
	return out;
}

// Pour les URLs simples on conserve les caractères réservés de chemin.
static inline std::string urlEncodePath(const std::string& in)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : in)
	{
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':' ||
			c == '?' || c == '&' || c == '=' || c == '#' || c == '+' || c == '%' || c == ',')
			out += (char)c;
		else
		{
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Divers
//-----------------------------------------------------------------------------
static inline int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float fclamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Echappement HTML d'une chaine (pour construire des pages internes).
static inline std::string htmlEscape(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	for (char c : in)
	{
		switch (c)
		{
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			default:  out += c; break;
		}
	}
	return out;
}

// Mesure de temps (msec depuis le boot). Utilise en fin periods UI.
#include <3ds.h>
static inline uint64_t timeMs(void) { return (uint64_t)osGetTime(); }

// Resolution d'une URL relative par rapport a une URL de base.
std::string urlJoin(const std::string& base, const std::string& refIn);