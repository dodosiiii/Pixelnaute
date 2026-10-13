#include "ui.hpp"
#include "../render/text.hpp"
#include "../render/morton.hpp"
#include "../search/search.hpp"
#include <string.h>
#include <malloc.h>

#define TEX_W 512
#define TEX_H 256

//--------------------------------------------------------------------------
// Petits helpers
//--------------------------------------------------------------------------
static void popLastUtf8(std::string& s)
{
	if (s.empty()) return;
	size_t i = s.size() - 1;
	while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
	s.resize(i);
}

//--------------------------------------------------------------------------
bool BottomUi::init()
{
	if (!bmp_.create(320, 240)) return false;
	if (!C3D_TexInit(&tex_, TEX_W, TEX_H, GPU_RGB565)) return false;
	texData_ = linearAlloc(TEX_W * TEX_H * 2);
	if (!texData_)
	{
		C3D_TexDelete(&tex_);
		return false;
	}
	memset(texData_, 0, TEX_W * TEX_H * 2);
	C3D_TexUpload(&tex_, texData_);
	C3D_TexSetFilter(&tex_, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&tex_, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	// Region 320x240 dans une texture 512x256.
	// Convention PICA200 : v = 1.0 = premiere ligne de la memoire texture
	// (haut de l'image), et v decroit vers le bas.
	subtex_.width = 320;
	subtex_.height = 240;
	subtex_.left = 0.0f;
	subtex_.right = (float)(320) / TEX_W;
	subtex_.top = 1.0f;
	subtex_.bottom = 1.0f - (float)(240) / TEX_H;

	image_.tex = &tex_;
	image_.subtex = &subtex_;
	texReady_ = true;
	return true;
}

void BottomUi::shutdown()
{
	if (texData_) { linearFree(texData_); texData_ = nullptr; }
	if (texReady_) { C3D_TexDelete(&tex_); texReady_ = false; }
	bmp_.destroy();
}

//--------------------------------------------------------------------------
void BottomUi::text(Bitmap& b, const std::string& t, int x, int y, float s,
	uint32_t c, bool bold)
{
	TextEngine& te = TextEngine::instance();
	if (!te.ready()) return;
	te.drawLine(b, t, x, y, s, c, bold, false);
}

int BottomUi::textW(const std::string& t, float s) const
{
	TextEngine& te = TextEngine::instance();
	if (!te.ready()) return 0;
	return (int)te.measure(t, s);
}

std::string BottomUi::fitStr(const std::string& t, int maxW, float s) const
{
	if (textW(t, s) <= maxW) return t;
	const std::string ellipsis = "\xE2\x80\xA6";
	const int textLimit = maxW - textW(ellipsis, s);
	std::string out;
	size_t i = 0;
	while (i < t.size())
	{
		uint32_t cp; int n = utf8Decode(t.c_str() + i, cp);
		if (n == 0) break;
		if (textW(out + t.substr(i, n), s) > textLimit) break;
		out += t.substr(i, n);
		i += n;
	}
	out += ellipsis;
	return out;
}

//--------------------------------------------------------------------------
// Primitives graphiques "telephone"
//
// Le rendu est logiciel : on dessine chaque pixel avec une couverture
// anti-aliasee afin que les coins arrondis et les glyphes restent nets.
//--------------------------------------------------------------------------
void BottomUi::roundRect(Bitmap& b, int x, int y, int w, int h, int radius,
	uint32_t color)
{
	if (w <= 0 || h <= 0) return;
	if (radius > w / 2) radius = w / 2;
	if (radius > h / 2) radius = h / 2;
	if (radius < 0) radius = 0;

	uint8_t r = (color >> 0) & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t bl = (color >> 16) & 0xFF;
	uint32_t opaque = 0xFF000000u | (bl << 16) | (g << 8) | r;
	uint16_t base = Bitmap::rgb32To565(opaque);
	const float rr = (float)radius;

	for (int py = 0; py < h; py++)
	{
		int sy = y + py;
		if (sy < 0 || sy >= b.h) continue;
		for (int px = 0; px < w; px++)
		{
			int sx = x + px;
			if (sx < 0 || sx >= b.w) continue;

			// distance aux centres des coins (uniquement dans les coins)
			float cx = -1.0f, cy = -1.0f;
			if (px < radius) cx = rr - 0.5f;
			else if (px >= w - radius) cx = (float)(w - radius) - 0.5f;
			if (py < radius) cy = rr - 0.5f;
			else if (py >= h - radius) cy = (float)(h - radius) - 0.5f;

			if (cx >= 0.0f && cy >= 0.0f)
			{
				float dx = (float)px + 0.5f - (cx + 0.5f);
				float dy = (float)py + 0.5f - (cy + 0.5f);
				float dist = sqrtf(dx * dx + dy * dy);
				float coverage = rr - dist + 0.5f;
				if (coverage <= 0.0f) continue;
				if (coverage < 1.0f)
				{
					b.blendPixel16(sx, sy, opaque, (u8)(coverage * 255.0f));
					continue;
				}
			}
			b.px[(size_t)sy * b.w + sx] = base;
		}
	}
}

void BottomUi::roundRectOutline(Bitmap& b, int x, int y, int w, int h,
	int radius, uint32_t edge, int thickness)
{
	// Une bordure se compose de deux rectangles arrondis : l'exterieur en
	// couleur de bord, puis l'interieur par-dessus. `fill` doit etre la
	// couleur de fond reelle, car le rendu logiciel ne gere pas de masque.
	roundRect(b, x, y, w, h, radius, edge);
	(void)thickness; // conserve pour lisibilite des appels
}

void BottomUi::textCentered(Bitmap& b, const std::string& t, int x, int w,
	int y, float s, uint32_t c, bool bold)
{
	const int tw = textW(t, s);
	text(b, t, x + (w - tw) / 2, y, s, c, bold);
}

// kind : 0 = retour, 1 = avant, 2 = accueil, 3 = recharger, 4 = arreter
void BottomUi::navGlyph(Bitmap& b, int kind, int cx, int cy, uint32_t color)
{
	uint8_t r = (color >> 0) & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t bl = (color >> 16) & 0xFF;
	uint32_t opaque = 0xFF000000u | (bl << 16) | (g << 8) | r;

	switch (kind)
	{
	case 0: // chevron gauche : deux traits diagonaux epais
		for (int i = 0; i < 5; i++)
		{
			b.fillRect(cx - 3 + i, cy - 4 + i, 2, 2, opaque);
			b.fillRect(cx - 3 + i, cy + 4 - i, 2, 2, opaque);
		}
		break;
	case 1: // chevron droit
		for (int i = 0; i < 5; i++)
		{
			b.fillRect(cx + 2 - i, cy - 4 + i, 2, 2, opaque);
			b.fillRect(cx + 2 - i, cy + 4 - i, 2, 2, opaque);
		}
		break;
	case 2: // maison : toit en triangle plein + corps
		for (int i = 0; i < 6; i++)
			b.fillRect(cx - i, cy - 5 + i, 1 + 2 * i, 1, opaque);
		b.fillRect(cx - 4, cy, 9, 6, opaque);
		break;
	case 3: // arc de cercle + pointe (recharger)
		{
			static const int arc[][2] = {
				{ 0, -4 }, { 2, -4 }, { 4, -2 }, { 4, 0 }, { 4, 2 },
				{ 2, 4 }, { 0, 4 }, { -2, 4 }, { -4, 2 }, { -4, 0 },
				{ -4, -2 }, { -2, -4 }
			};
			for (const auto& p : arc)
				b.fillRect(cx + p[0], cy + p[1], 1, 1, opaque);
			b.fillRect(cx - 2, cy - 6, 5, 1, opaque);
			b.fillRect(cx - 2, cy - 6, 1, 3, opaque);
		}
		break;
	case 4: // croix (arreter)
		for (int i = -4; i <= 4; i++)
		{
			b.fillRect(cx + i, cy + i, 1, 1, opaque);
			b.fillRect(cx + i, cy - i, 1, 1, opaque);
		}
		break;
	default:
		break;
	}
}

//--------------------------------------------------------------------------
void BottomUi::openAddress(const std::string& initial)
{
	mode_ = UiMode::Address;
	buf_ = initial;
	homeTarget_ = false;
	kbdPage_ = 0;
	buildKeys();
	dirty_ = true;
}

void BottomUi::startFieldEdit(Browser& b)
{
	Browser::Tab& t = b.currentTab();
	if (t.focusNode < 0 || t.focusNode >= (int)t.page.fields.size()) return;
	mode_ = UiMode::FieldEdit;
	focusIdx_ = t.focusNode;
	buf_ = t.page.fields[focusIdx_].value;
	kbdPage_ = 0;
	buildKeys();
	dirty_ = true;
}

void BottomUi::openInternal(const std::string& url)
{
	std::string u = toLower(url);
	if (u == "nw://bookmarks") mode_ = UiMode::Bookmarks;
	else if (u == "nw://history") mode_ = UiMode::History;
	else if (u == "nw://settings") mode_ = UiMode::Settings;
	else if (u == "nw://about") mode_ = UiMode::About;
	else if (u == "nw://home" || u == "nw:/home") mode_ = UiMode::Browsing;
	else return;
	listOffset_ = 0;
	dirty_ = true;
}

void BottomUi::toast(const std::string& msg)
{
	toastMsg_ = msg;
	toastUntil_ = timeMs() + 2500;
	dirty_ = true;
}

//--------------------------------------------------------------------------
// Chrome de l'ecran bas
//
// Disposition verticale (ecran 320x240) :
//   0..28    onglets
//   28..58   barre d'adresse
//   58..88   rangee de navigation (retour, avant, accueil, recharger, onglets, menu)
//   88..222  contenu du mode (clavier, listes, menu)
//   220..240 barre d'etat + progression
//--------------------------------------------------------------------------
void BottomUi::drawTabs(Browser& b)
{
	const int n = b.count();
	if (n <= 0) return;

	// Un seul onglet : on affiche le titre courant, sans bouton.
	if (n == 1)
	{
		const Browser::Tab& t = b.tab(0);
		std::string label = t.title.empty() ? "Nouvel onglet" : t.title;
		text(bmp_, fitStr(label, 250, 0.72f), 8, 7, 0.72f, C_TEXT_D);
		return;
	}

	const int w = 320 / n;
	for (int i = 0; i < n; i++)
	{
		const Browser::Tab& t = b.tab(i);
		const int x = i * w;
		const bool active = (i == b.index());
		roundRect(bmp_, x + 2, 4, w - 4, 21, 6,
			active ? C_SURFACE2 : C_SURFACE);
		if (active)
			bmp_.fillRect(x + 8, 24, w - 16, 2, C_ACCENT);
		std::string label = itos(i + 1) + " " + fitStr(t.title, w - 26, 0.68f);
		text(bmp_, label, x + 6, 7, 0.68f, active ? C_TEXT : C_TEXT_D);
	}
}

void BottomUi::drawAddressBar(Browser& b, NetworkManager& net)
{
	const bool editing = (mode_ == UiMode::Address || mode_ == UiMode::FieldEdit);

	// Champ arrondi facon barre d'adresse de telephone.
	roundRect(bmp_, 6, 31, 308, 25, 12, C_SURFACE);
	if (editing)
		roundRectOutline(bmp_, 6, 31, 308, 25, 12, C_ACCENT, 1);

	// Petite icone : cadenas si HTTPS, loupe en saisie.
	const int iconX = 16;
	if (editing)
	{
		bmp_.addRect(iconX - 2, 40, 1, 1, C_ACCENT, 1);
		bmp_.fillRect(iconX, 38, 7, 1, C_ACCENT);
		bmp_.fillRect(iconX - 1, 39, 1, 5, C_ACCENT);
		bmp_.fillRect(iconX + 6, 39, 1, 5, C_ACCENT);
		bmp_.fillRect(iconX - 1, 44, 8, 1, C_ACCENT);
		bmp_.fillRect(iconX + 1, 41, 4, 2, C_ACCENT);
	}
	else
	{
		const std::string u = b.currentTab().url;
		const bool secure = strStartsWith(u, "https://");
		const uint32_t c = secure ? C_GREEN : C_TEXT_DD;
		// corps du cadenas
		roundRect(bmp_, iconX - 1, 41, 9, 7, 2, c);
		// anse
		bmp_.fillRect(iconX, 38, 1, 3, c);
		bmp_.fillRect(iconX + 6, 38, 1, 3, c);
		bmp_.fillRect(iconX + 1, 37, 5, 1, c);
	}

	std::string shown;
	uint32_t color = C_TEXT;
	if (editing)
	{
		shown = buf_;
		if ((timeMs() / 450) & 1)
		{
			const int cx = 30 + textW(shown, 0.82f);
			bmp_.fillRect(cx, 36, 2, 15, C_ACCENT);
		}
	}
	else
	{
		Browser::Tab& t = b.currentTab();
		shown = t.url.empty() ? "Rechercher ou saisir une adresse" : t.url;
		if (t.url.empty()) color = C_TEXT_DD;
		else if (net.busy()) color = C_ACCENT;
	}
	const std::string fitted = fitStr(shown, 274, 0.82f);
	text(bmp_, fitted, 30, 36, 0.82f, color);
}

void BottomUi::drawNavRow(Browser& b, NetworkManager& net)
{
	const int w = 50;
	const int gap = 2;

	for (int i = 0; i < 6; i++)
	{
		const int x = 4 + i * (w + gap);
		const int cx = x + w / 2;
		const int cy = 73;

		roundRect(bmp_, x, 61, w, 24, 8, C_SURFACE);

		switch (i)
		{
			case 0: navGlyph(bmp_, 0, cx, cy, C_TEXT); break;   // retour
			case 1: navGlyph(bmp_, 1, cx, cy, C_TEXT); break;   // avant
			case 2: navGlyph(bmp_, 2, cx, cy, C_TEXT); break;   // accueil
			case 3:                                             // recharger / arreter
				navGlyph(bmp_, net.busy() ? 4 : 3, cx, cy,
					net.busy() ? C_ACCENT : C_TEXT);
				break;
			case 4:                                             // onglets
				textCentered(bmp_, itos(b.count()), x, w, 66, 0.9f,
					b.count() > 1 ? C_ACCENT : C_TEXT, true);
				break;
			case 5:                                             // menu
				for (int k = -1; k <= 1; k++)
					bmp_.fillRect(cx - 7, cy + k * 4 - 1, 15, 2, C_TEXT);
				break;
		}
	}
}

void BottomUi::drawStatusRow(Browser& b, NetworkManager& net)
{
	roundRect(bmp_, 0, 220, 320, 20, 0, C_BG_2);

	if (timeMs() < toastUntil_)
	{
		text(bmp_, fitStr(toastMsg_, 308, 0.52f), 6, 223, 0.52f, C_ACCENT);
	}
	else if (b.layoutPending())
	{
		text(bmp_, "Mise en page...", 6, 223, 0.52f, C_ACCENT);
	}
	else
	{
		Browser::Tab& t = b.currentTab();
		if (net.busy())
		{
			const int pct = (int)(net.progress() * 100.0f);
			const std::string msg = pct > 0
				? "Chargement " + itos(pct) + " %"
				: "Chargement... " + itos((long)(net.bytesReceived() / 1024)) + " Ko";
			text(bmp_, fitStr(msg, 308, 0.52f), 6, 223, 0.52f, C_ACCENT);
		}
		else if (mode_ == UiMode::Settings)
		{
			text(bmp_, "B : retour | touchez une ligne", 6, 223, 0.52f, C_TEXT_DD);
		}
		else if (!t.title.empty())
		{
			text(bmp_, fitStr(t.title, 308, 0.52f), 6, 223, 0.52f, C_TEXT_DD);
		}
		else
		{
			text(bmp_, "New 3DS uniquement", 6, 223, 0.52f, C_TEXT_DD);
		}
	}

	// Progression : fine ligne collee en haut de l'ecran.
	if (net.busy())
	{
		const int w = (int)(net.progress() * 320.0f);
		if (w > 0) bmp_.fillRect(0, 0, w, 2, C_ACCENT);
	}
}

void BottomUi::drawChrome(Browser& b, NetworkManager& net, WebSettings& set)
{
	(void)set;
	drawTabs(b);
	drawAddressBar(b, net);
	drawNavRow(b, net);
	drawStatusRow(b, net);
}

//--------------------------------------------------------------------------
// Clavier tactile
//
// Quatre rangees de 24 px, espacees de 3 px, de y=92 a y=200. Les touches
// sont plus larges que dans la version precedente (marges reduites) car le
// clavier logiciel est le principal point de friction sur une console.
//--------------------------------------------------------------------------
void BottomUi::buildKeys()
{
	keys_.clear();
	const int rowY[4] = { 92, 119, 146, 173 };
	const int rowH = 24;

	// Construit une rangee de touches de largeur egale.
	auto addRow = [&](const std::string& chars, int row, int y)
	{
		const int margin = 3;
		const int n = (int)chars.size();
		if (n <= 0) return;
		const int kw = (320 - margin * 2 - (n - 1) * 2) / n;
		for (int i = 0; i < n; i++)
		{
			std::string c(1, chars[(size_t)i]);
			const int x = margin + i * (kw + 2);
			keys_.push_back({ x, y, kw, rowH, c, c, 0 });
		}
	};

	if (kbdPage_ == 0)
	{
		addRow("azertyuiop", 0, rowY[0]);
		addRow("qsdfghjklm", 1, rowY[1]);

		// 3e rangee : 8 lettres + effacer
		const std::string r3 = "wxcvbn,;";
		const int margin = 3;
		const int kw = (320 - margin * 2 - 8 * 2) / 9;
		for (int i = 0; i < (int)r3.size(); i++)
		{
			std::string c(1, r3[(size_t)i]);
			const int x = margin + i * (kw + 2);
			keys_.push_back({ x, rowY[2], kw, rowH, c, c, 0 });
		}
		keys_.push_back({ margin + 8 * (kw + 2), rowY[2], kw, rowH, "<--", "", 1 });

		// 4e rangee : symboles, espace, valider
		keys_.push_back({ margin, rowY[3], 46, rowH, "123", "", 4 });
		keys_.push_back({ margin + 48, rowY[3], 174, rowH, "espace", "", 2 });
		keys_.push_back({ margin + 224, rowY[3], 90, rowH, "OK", "", 3 });
	}
	else
	{
		addRow("1234567890", 0, rowY[0]);
		addRow("-_/:.,?!@", 1, rowY[1]);
		addRow("()[]{}#%*", 2, rowY[2]);

		const int margin = 3;
		keys_.push_back({ margin, rowY[3], 46, rowH, "abc", "", 4 });
		keys_.push_back({ margin + 48, rowY[3], 174, rowH, "espace", "", 2 });
		keys_.push_back({ margin + 224, rowY[3], 90, rowH, "OK", "", 3 });
	}
}

void BottomUi::drawKeyboard(Bitmap& b)
{
	for (const auto& k : keys_)
	{
		const bool isOk = (k.action == 3);
		const bool isSpecial = (k.action == 1 || k.action == 2 || k.action == 4);

		roundRect(b, k.x, k.y, k.w, k.h, 6,
			isOk ? C_KEY_OK : (isSpecial ? C_SURFACE2 : C_KEY));

		const uint32_t fg = isOk ? C_BG : C_TEXT;
		if (k.label.size() > 2)
			textCentered(b, k.label, k.x, k.w, k.y + 5, 0.7f, fg, isOk);
		else
			textCentered(b, k.label, k.x, k.w, k.y + 3, 0.88f, fg, isOk);
	}
}

//--------------------------------------------------------------------------
// Menus et listes
//--------------------------------------------------------------------------
void BottomUi::drawMenu(Browser& b)
{
	static const char* rows[] =
	{
		"Nouvel onglet", "Fermer l'onglet", "Ajouter aux favoris",
		"Favoris", "Historique", "Reglages", "A propos", nullptr
	};
	for (int i = 0; rows[i]; i++)
	{
		const int y = 92 + i * 25;
		roundRect(bmp_, 5, y, 310, 23, 6, (i & 1) ? C_SURFACE : C_BG_2);
		text(bmp_, rows[i], 14, y + 4, 0.82f, C_TEXT);
	}
}

void BottomUi::drawHistoryList(Browser& b)
{
	text(bmp_, "Historique (B : retour)", 10, 92, 0.7f, C_TEXT_D);
	const auto& entries = b.history().entries;
	if (entries.empty())
	{
		text(bmp_, "(vide)", 10, 120, 0.8f, C_TEXT_D);
		return;
	}
	int visible = (222 - 116) / 28;
	int maxOff = (int)entries.size() - visible;
	if (maxOff < 0) maxOff = 0;
	if (listOffset_ > maxOff) listOffset_ = maxOff;
	for (int i = 0; i < visible; i++)
	{
		int idx = listOffset_ + i;
		if (idx >= (int)entries.size()) break;
		int y = 116 + i * 28;
		bmp_.fillRect(0, y, 320, 26, (i & 1) ? C_NAVY : C_NAVY_2);
		text(bmp_, fitStr(entries[idx].title, 190, 0.8f), 6, y + 3, 0.8f, C_TEXT, true);
		text(bmp_, fitStr(entries[idx].url, 110, 0.6f), 200, y + 4, 0.6f, C_TEXT_D);
	}
}

void BottomUi::drawBookmarksList(Browser& b)
{
	text(bmp_, "Favoris (B : retour, X : effacer)", 10, 92, 0.7f, C_TEXT_D);
	const auto& entries = b.bookmarks().entries;
	if (entries.empty())
	{
		text(bmp_, "(aucun favori)", 10, 120, 0.8f, C_TEXT_D);
		return;
	}
	int visible = (222 - 116) / 28;
	int maxOff = (int)entries.size() - visible;
	if (maxOff < 0) maxOff = 0;
	if (listOffset_ > maxOff) listOffset_ = maxOff;
	for (int i = 0; i < visible; i++)
	{
		int idx = listOffset_ + i;
		if (idx >= (int)entries.size()) break;
		int y = 116 + i * 28;
		bmp_.fillRect(0, y, 320, 26, (i & 1) ? C_NAVY : C_NAVY_2);
		text(bmp_, fitStr(entries[idx].title, 180, 0.8f), 6, y + 3, 0.8f, C_TEXT, true);
		text(bmp_, fitStr(entries[idx].url, 90, 0.6f), 196, y + 4, 0.6f, C_TEXT_D);
		text(bmp_, "X", 308, y + 4, 0.8f, C_RED, true);
	}
}

void BottomUi::drawSettingsList(Browser& b, WebSettings& set)
{
	text(bmp_, "Reglages", 10, 92, 0.75f, C_TEXT_D);

	const std::string engine = searchEngineDisplayName(set.searchEngine);
	const std::string tls = set.verifyTLS ? "Oui" : "Non";
	const std::string img = set.loadImages ? "Oui" : "Non";

	struct Row { const char* label; std::string value; bool accent; };
	Row rows[5] =
	{
		{ "Moteur de recherche", engine, true },
		{ "Page d'accueil", fitStr(set.homePage.empty() ? "(par defaut)" : set.homePage, 120, 0.7f), false },
		{ "Verifier les certificats", tls, false },
		{ "Charger les images", img, false },
		{ "Effacer l'historique", "", false },
	};
	for (int i = 0; i < 5; i++)
	{
		const int y = 110 + i * 21;
		roundRect(bmp_, 4, y, 312, 20, 5, (i & 1) ? C_SURFACE : C_BG_2);
		text(bmp_, rows[i].label, 10, y + 2, 0.75f, C_TEXT);
		if (!rows[i].value.empty())
			text(bmp_, rows[i].value,
				316 - 6 - textW(rows[i].value, 0.68f), y + 3, 0.68f,
				rows[i].accent ? C_ACCENT : C_TEXT_D);
	}
}

void BottomUi::drawAbout()
{
	text(bmp_, "Pixelnaute", 12, 92, 1.15f, C_TEXT, true);
	text(bmp_, "Version " NW_VERSION_STRING, 12, 126, 0.75f, C_TEXT_DD);
	text(bmp_, "Navigateur autonome pour New Nintendo 3DS", 12, 150, 0.78f, C_TEXT);

	text(bmp_, "Aucun PC, aucun proxy : la console se connecte seule.", 12, 172, 0.72f, C_TEXT_D);
	text(bmp_, "Moteur de recherche reglable dans Reglages.", 12, 194, 0.72f, C_TEXT_D);
	text(bmp_, "Touchez l'ecran pour revenir.", 12, 198, 0.7f, C_TEXT_DD);
}

//--------------------------------------------------------------------------
// Frame principale de l'ecran bas
//--------------------------------------------------------------------------
void BottomUi::frame(Browser& b, NetworkManager& net, WebSettings& set)
{
	bmp_.clear(C_NAVY);
	drawChrome(b, net, set);

	switch (mode_)
	{
		case UiMode::Browsing:
		{
			if (!b.currentTab().pageReady && net.state() == NetState::Idle)
				text(bmp_, "Accueil : tapez l'adresse, ou X pour un onglet", 8, 120, 0.7f, C_TEXT_D);
			break;
		}
		case UiMode::Address:
		case UiMode::FieldEdit:
			drawKeyboard(bmp_);
			break;
		case UiMode::Menu:
			drawMenu(b);
			break;
		case UiMode::History:
			drawHistoryList(b);
			break;
		case UiMode::Bookmarks:
			drawBookmarksList(b);
			break;
		case UiMode::Settings:
			drawSettingsList(b, set);
			break;
		case UiMode::About:
			drawAbout();
			break;
	}

	uploadIfNeeded();

	C2D_DrawParams params;
	memset(&params, 0, sizeof(params));
	params.pos.x = 0;
	params.pos.y = 0;
	params.pos.w = 320;
	params.pos.h = 240;
	params.depth = 0.0f;
	C2D_DrawImage(image_, &params, NULL);
}

void BottomUi::uploadIfNeeded()
{
	uint64_t now = timeMs();
	if (!texReady_ || (!dirty_ && now - lastUpload_ < 66)) return;
	uint16_t* dst = (uint16_t*)texData_;
	const uint16_t* src = bmp_.px;

	memset(texData_, 0, TEX_W * TEX_H * 2);
	int tilesX = TEX_W / 8;
	for (int r = 0; r < 240; r++)
	{
		const uint16_t* srow = src + (size_t)r * 320;
		for (int c = 0; c < 320; c++)
		{
			// Meme entrelacement que la page : voir source/render/morton.hpp
			dst[mortonTileIndex(c, r, tilesX)] = srow[c];
		}
	}
	C3D_TexUpload(&tex_, texData_);
	lastUpload_ = now;
	dirty_ = false;
}

//--------------------------------------------------------------------------
// Interactions
//--------------------------------------------------------------------------
int BottomUi::navHit(int x, int y) const
{
	// Rangee de navigation : y 61..85, 6 boutons de 50 px, ecart 2, marge 4.
	if (y < 61 || y >= 85) return -1;
	const int rel = x - 4;
	if (rel < 0) return -1;
	const int idx = rel / 52;
	if (idx < 0 || idx > 5) return -1;
	if (rel - idx * 52 >= 50) return -1;
	return idx;
}

int BottomUi::tabHit(int x, int y) const
{
	if (y < 4 || y >= 26) return -1;
	return x / (320 / 4);
}

void BottomUi::navAction(Browser& b, NetworkManager& net, WebSettings& set, int idx)
{
	dirty_ = true;
	switch (idx)
	{
		case 0: // retour
			if (mode_ == UiMode::Browsing) b.back();
			else
			{
				if (mode_ == UiMode::FieldEdit) b.currentTab().focusNode = -1;
				mode_ = UiMode::Browsing;
			}
			break;
		case 1: // avant
			b.forward();
			break;
		case 2: // accueil
			b.goHome();
			break;
		case 3: // recharger / arreter
			if (net.busy()) b.stop();
			else b.reload();
			break;

		case 4: // nouvel onglet
			b.newTab();
			mode_ = UiMode::Browsing;
			break;
		case 5: // menu
			mode_ = (mode_ == UiMode::Menu) ? UiMode::Browsing : UiMode::Menu;
			break;
	}
}

void BottomUi::keyHit(Browser& b, WebSettings& set, int x, int y)
{
	for (size_t i = 0; i < keys_.size(); i++)
	{
		const UiKey k = keys_[i];
		if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h)
		{
			applyKey(b, set, k);
			if (k.action == 0 || k.action == 1 || k.action == 2)
			{
				repeatKey_ = (int)i;
				repeatNext_ = timeMs() + 400;
				repeatCount_ = 0;
			}
			else
			{
				repeatKey_ = -1;
			}
			return;
		}
	}
}

void BottomUi::applyKey(Browser& b, WebSettings& set, const UiKey& k)
{
	dirty_ = true;
	switch (k.action)
	{
		case 1: popLastUtf8(buf_); break;
		case 2: buf_ += ' '; break;
		case 3: commitBuf(b, set); break;
		case 4:
			kbdPage_ ^= 1;
			buildKeys();
			break;
		default: buf_ += k.value; break;
	}
}

void BottomUi::commitBuf(Browser& b, WebSettings& set)
{
	if (homeTarget_)
	{
		set.homePage = strTrim(buf_);
		settingsSave(set);
		mode_ = UiMode::Settings;
	}
	else if (mode_ == UiMode::FieldEdit)
	{
		b.setFieldValue(focusIdx_, buf_);
		b.currentTab().focusNode = -1;
		mode_ = UiMode::Browsing;
	}
	else
	{
		b.openInput(buf_);
		mode_ = UiMode::Browsing;
	}
	buf_.clear();
	homeTarget_ = false;
	focusIdx_ = -1;
	dirty_ = true;
}

void BottomUi::handleTouch(Browser& b, NetworkManager& net, WebSettings& set, const InputState& in)
{
	const bool keyboardMode = mode_ == UiMode::Address || mode_ == UiMode::FieldEdit;
	if (keyboardMode && in.touch && !in.touchDown &&
		repeatKey_ >= 0 && repeatKey_ < (int)keys_.size())
	{
		const UiKey k = keys_[(size_t)repeatKey_];
		const bool still = in.tp.px >= k.x && in.tp.px < k.x + k.w &&
			in.tp.py >= k.y && in.tp.py < k.y + k.h;
		const uint64_t now = timeMs();
		if (!still)
			repeatKey_ = -1;
		else if (now >= repeatNext_)
		{
			applyKey(b, set, k);
			repeatNext_ = now + (repeatCount_++ < 10 ? 70 : 35);
		}
	}
	else if (!in.touch || !keyboardMode || in.touchDown)
	{
		repeatKey_ = -1;
	}

	if (!in.touchDown) return;
	int x = in.tp.px;
	int y = in.tp.py;
	if (x >= 320) return; // ecran haut gere ailleurs

	// boutons de navigation (toujours cliquables)
	int ni = navHit(x, y);
	if (ni >= 0)
	{
		navAction(b, net, set, ni);
		return;
	}
	// onglets
	int ti = tabHit(x, y);
	if (ti >= 0 && ti < b.count())
	{
		b.selectTab(ti);
		mode_ = UiMode::Browsing;
		dirty_ = true;
		return;
	}
	// barre d'adresse (zone arrondie 31..56)
	if (y >= 29 && y < 58 && mode_ == UiMode::Browsing)
	{
		openAddress(b.currentTab().url);
		return;
	}

	switch (mode_)
	{
		case UiMode::Address:
		case UiMode::FieldEdit:
			keyHit(b, set, x, y);
			break;
		case UiMode::Menu:
		{
			if (y < 92) break;
			int idx = (y - 92) / 25;
			switch (idx)
			{
				case 0: b.newTab(); mode_ = UiMode::Browsing; dirty_ = true; break;
				case 1:
					if (b.count() > 1) b.closeTab(b.index());
					else toast("Dernier onglet");
					mode_ = UiMode::Browsing;
					dirty_ = true;
					break;
				case 2:
				{
					Browser::Tab& t = b.currentTab();
					if (t.url.empty() || strStartsWith(t.url, "nw://"))
						toast("Rien a mettre en favori");
					else
					{
						b.bookmarks().add(t.url, t.title);
						toast("Ajoute aux favoris");
					}
					mode_ = UiMode::Browsing;
					dirty_ = true;
					break;
				}
				case 3: mode_ = UiMode::Bookmarks; listOffset_ = 0; dirty_ = true; break;
				case 4: mode_ = UiMode::History; listOffset_ = 0; dirty_ = true; break;
				case 5: mode_ = UiMode::Settings; dirty_ = true; break;
				case 6: mode_ = UiMode::About; dirty_ = true; break;
			}
			break;
		}
		case UiMode::History:
		{
			if (y < 116) break;
			int idx = listOffset_ + (y - 116) / 28;
			if (idx >= 0 && idx < (int)b.history().entries.size())
			{
				b.openUrl(b.history().entries[idx].url);
				mode_ = UiMode::Browsing;
				dirty_ = true;
			}
			break;
		}
		case UiMode::Bookmarks:
		{
			if (y < 116) break;
			int idx = listOffset_ + (y - 116) / 28;
			if (idx >= 0 && idx < (int)b.bookmarks().entries.size())
			{
				if (x >= 300)
					b.bookmarks().remove(b.bookmarks().entries[idx].url);
				else
				{
					b.openUrl(b.bookmarks().entries[idx].url);
					mode_ = UiMode::Browsing;
				}
				dirty_ = true;
			}
			break;
		}
		case UiMode::Settings:
		{
			if (y >= 220)
			{
				mode_ = UiMode::Browsing;
				dirty_ = true;
			}
			else if (y >= 110 && y < 215)
			{
				int idx = (y - 110) / 21;
				switch (idx)
				{
					case 0:
						// Passe au moteur suivant de la liste.
						set.searchEngine = searchEngineNext(set.searchEngine);
						settingsSave(set);
						toast(std::string("Moteur : ") +
							searchEngineDisplayName(set.searchEngine));
						dirty_ = true;
						break;
					case 1:
						homeTarget_ = true;
						mode_ = UiMode::Address;
						buf_ = set.homePage;
						kbdPage_ = 0;
						buildKeys();
						dirty_ = true;
						break;
					case 2: set.verifyTLS = !set.verifyTLS; settingsSave(set); dirty_ = true; break;
					case 3: set.loadImages = !set.loadImages; settingsSave(set); dirty_ = true; break;
					case 4: b.history().clear(); toast("Historique efface"); dirty_ = true; break;
				}
			}
			break;
		}
		case UiMode::About:
			mode_ = UiMode::Browsing;
			dirty_ = true;
			break;
		default:
			break;
	}
}

void BottomUi::handleKeys(Browser& b, NetworkManager& net, WebSettings& set, u32 kDown)
{
	if (!kDown) return;

	switch (mode_)
	{
		case UiMode::Browsing:
			if (kDown & KEY_B) { b.back(); return; }
			if (kDown & KEY_X) { b.newTab(); return; }
			if (kDown & KEY_Y)
			{
				if (net.busy()) b.stop();
				else b.reload();
				return;
			}
			if (kDown & KEY_START) { mode_ = UiMode::Menu; dirty_ = true; return; }
			if (kDown & KEY_SELECT) { openAddress(b.currentTab().url); return; }
			if (kDown & KEY_L) { b.prevTab(); return; }
			if (kDown & KEY_R) { b.nextTab(); return; }
			break;

		case UiMode::Address:
		case UiMode::FieldEdit:
			// Le defilement doit rester disponible pendant la saisie.
			//
			// Sans cela, l'application restait en mode clavier apres une
			// recherche et les fleches ne faisaient plus rien : impossible de
			// parcourir les resultats sans sortir du clavier d'abord.
			if (kDown & KEY_DUP) { b.scroll(-48); dirty_ = true; return; }
			if (kDown & KEY_DDOWN) { b.scroll(48); dirty_ = true; return; }
			if (kDown & KEY_B)
			{
				if (mode_ == UiMode::FieldEdit) b.currentTab().focusNode = -1;
				mode_ = UiMode::Browsing;
				buf_.clear();
				homeTarget_ = false;
				focusIdx_ = -1;
				dirty_ = true;
				return;
			}
			if (kDown & KEY_A) { commitBuf(b, set); return; }
			break;

		case UiMode::History:
		case UiMode::Bookmarks:
			if (kDown & KEY_B) { mode_ = UiMode::Browsing; dirty_ = true; return; }
			if (kDown & KEY_DUP) { listOffset_--; dirty_ = true; return; }
			if (kDown & KEY_DDOWN) { listOffset_++; dirty_ = true; return; }
			if (kDown & KEY_X && mode_ == UiMode::Bookmarks)
			{
				if (!b.bookmarks().entries.empty())
					b.bookmarks().remove(b.bookmarks().entries[0].url);
				dirty_ = true;
				return;
			}
			break;

		case UiMode::Menu:
		case UiMode::Settings:
		case UiMode::About:
			if (kDown & KEY_B) { mode_ = UiMode::Browsing; dirty_ = true; return; }
			break;

		default:
			break;
	}
}