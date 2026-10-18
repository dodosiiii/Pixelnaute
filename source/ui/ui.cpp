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
	listSel_ = 0;
	pendingBookmarkDelete_.clear();
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

	// Petite icone : loupe en saisie, pastille verte (HTTPS), orange (HTTP).
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
		const bool isHttps = strStartsWith(toLower(u), "https://");
		const bool isHttp = strStartsWith(toLower(u), "http://");
		const uint32_t dotCol = isHttps ? C_GREEN : (isHttp ? C_ORANGE : C_TEXT_DD);
		roundRect(bmp_, iconX, 40, 7, 7, 3, dotCol);
	}

	if (editing)
	{
		std::string schema;
		std::string rest = buf_;
		if (strStartsWith(toLower(rest), "https://"))
		{
			schema = rest.substr(0, 8);
			rest = rest.substr(8);
		}
		else if (strStartsWith(toLower(rest), "http://"))
		{
			schema = rest.substr(0, 7);
			rest = rest.substr(7);
		}

		int curX = 30;
		if (!schema.empty())
		{
			text(bmp_, schema, curX, 36, 0.82f, C_TEXT_DD);
			curX += textW(schema, 0.82f);
		}
		const int restAvailW = 274 - (curX - 30);
		const std::string fittedRest = fitStr(rest, restAvailW > 20 ? restAvailW : 20, 0.82f);
		text(bmp_, fittedRest, curX, 36, 0.82f, C_TEXT);

		if ((timeMs() / 450) & 1)
		{
			const int cx = 30 + textW(buf_, 0.82f);
			if (cx < 304) bmp_.fillRect(cx, 36, 2, 15, C_ACCENT);
		}
	}
	else
	{
		Browser::Tab& t = b.currentTab();
		const std::string u = t.url;
		const bool isHome = u.empty() || toLower(u) == "nw://home" || toLower(u) == "nw:/home";
		if (isHome)
		{
			text(bmp_, fitStr("Rechercher ou saisir une adresse", 274, 0.82f), 30, 36, 0.82f, C_TEXT_DD);
		}
		else
		{
			std::string schema;
			std::string rest = u;
			if (strStartsWith(toLower(rest), "https://"))
			{
				schema = rest.substr(0, 8);
				rest = rest.substr(8);
			}
			else if (strStartsWith(toLower(rest), "http://"))
			{
				schema = rest.substr(0, 7);
				rest = rest.substr(7);
			}

			const uint32_t restColor = net.busy() ? C_ACCENT : C_TEXT;
			int curX = 30;
			if (!schema.empty())
			{
				text(bmp_, schema, curX, 36, 0.82f, C_TEXT_DD);
				curX += textW(schema, 0.82f);
			}
			const int restAvailW = 274 - (curX - 30);
			const std::string fittedRest = fitStr(rest, restAvailW > 20 ? restAvailW : 20, 0.82f);
			text(bmp_, fittedRest, curX, 36, 0.82f, restColor);
		}
	}
}

void BottomUi::drawNavRow(Browser& b, NetworkManager& net)
{
	const int w = 50;
	const int gap = 2;

	const bool canBack    = b.canGoBack();
	const bool canForward = b.canGoForward();
	const bool loading    = net.busy();

	for (int i = 0; i < 6; i++)
	{
		const int x  = 4 + i * (w + gap);
		const int cx = x + w / 2;
		const int cy = 73;

		// Bouton appuye (feedback visuel)
		const bool pressed = (touchNav_ == i);
		const uint32_t bg = pressed ? C_SURFACE2 : C_SURFACE;
		roundRect(bmp_, x, 61, w, 24, 8, bg);

		// Couleur du glyph : grise si action impossible
		uint32_t col;
		switch (i)
		{
			case 0: col = canBack    ? C_TEXT : C_TEXT_DD; break; // retour
			case 1: col = canForward ? C_TEXT : C_TEXT_DD; break; // avant
			case 2: col = C_TEXT; break;                          // accueil
			case 3: col = loading    ? C_ACCENT : C_TEXT; break;  // reload/stop
			case 4: col = b.count() > 1 ? C_ACCENT : C_TEXT; break; // onglets
			case 5: col = C_TEXT; break;                          // menu
		}

		switch (i)
		{
			case 0: navGlyph(bmp_, 0, cx, cy, col); break;   // retour
			case 1: navGlyph(bmp_, 1, cx, cy, col); break;   // avant
			case 2: navGlyph(bmp_, 2, cx, cy, col); break;   // accueil
			case 3:                                           // recharger / arreter
				navGlyph(bmp_, loading ? 4 : 3, cx, cy, col);
				break;
			case 4:                                           // onglets
				textCentered(bmp_, itos(b.count()), x, w, 66, 0.9f, col, true);
				break;
			case 5:                                           // menu
				for (int k = -1; k <= 1; k++)
					bmp_.fillRect(cx - 7, cy + k * 4 - 1, 15, 2, col);
				break;
		}
	}
}

void BottomUi::drawStatusRow(Browser& b, NetworkManager& net)
{
	roundRect(bmp_, 0, 220, 320, 20, 0, C_BG_2);

	// Version en bas a droite (discret)
	const std::string versionStr = "v" NW_VERSION_STRING;
	const int vw = textW(versionStr, 0.48f);
	text(bmp_, versionStr, 318 - vw, 224, 0.48f, C_TEXT_DD);

	const int maxStatusW = 318 - vw - 8;

	if (timeMs() < toastUntil_)
	{
		text(bmp_, fitStr(toastMsg_, maxStatusW, 0.52f), 6, 223, 0.52f, C_ACCENT);
	}
	else if (b.layoutPending())
	{
		text(bmp_, fitStr("Mise en page...", maxStatusW, 0.52f), 6, 223, 0.52f, C_ACCENT);
	}
	else
	{
		Browser::Tab& t = b.currentTab();
		if (b.stylesPending())
		{
			text(bmp_, fitStr("Styles...", maxStatusW, 0.52f), 6, 223, 0.52f, C_ACCENT);
		}
		else if (net.busy())
		{
			const int pct = (int)(net.progress() * 100.0f);
			const std::string msg = pct > 0
				? "Chargement " + itos(pct) + " %"
				: "Chargement... " + itos((long)(net.bytesReceived() / 1024)) + " Ko";
			text(bmp_, fitStr(msg, maxStatusW, 0.52f), 6, 223, 0.52f, C_ACCENT);
		}
		else if (mode_ == UiMode::Settings)
		{
			text(bmp_, fitStr("B : retour | touchez une ligne", maxStatusW, 0.52f), 6, 223, 0.52f, C_TEXT_DD);
		}
		else if (!t.title.empty())
		{
			text(bmp_, fitStr(t.title, maxStatusW, 0.52f), 6, 223, 0.52f, C_TEXT_DD);
		}
		else
		{
			text(bmp_, fitStr("New 3DS uniquement", maxStatusW, 0.52f), 6, 223, 0.52f, C_TEXT_DD);
		}
	}

	// Progression : fine ligne collee en haut de l'ecran, plus epaisse et visible.
	if (net.busy())
	{
		const float prog = net.progress();
		// Fond semi-transparent de la barre
		bmp_.fillRect(0, 0, 320, 3, C_BG_2);
		const int w = prog > 0.0f ? (int)(prog * 320.0f) : 0;
		// Barre de progression animee (pulse si 0%)
		if (w > 0)
			bmp_.fillRect(0, 0, w, 3, C_ACCENT);
		else
		{
			// Pas encore de progression : barre pulsante courte
			const int pos = (int)((timeMs() / 10) % 320);
			bmp_.fillRect(pos, 0, 40 < 320 - pos ? 40 : 320 - pos, 3, C_ACCENT);
		}
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
	auto addRow = [&](const std::string& chars, int y)
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
		addRow(shift_ ? "AZERTYUIOP" : "azertyuiop", rowY[0]);
		addRow(shift_ ? "QSDFGHJKLM" : "qsdfghjklm", rowY[1]);

		// 3e rangee : Maj + 8 lettres + effacer
		const int margin = 3;
		const int kShiftW = 34;
		const int kBsW = 34;
		const int gap = 2;
		int x = margin;
		keys_.push_back({ x, rowY[2], kShiftW, rowH, shift_ ? "MAJ" : "maj", "", 5 });
		x += kShiftW + gap;
		const int kw = (320 - margin * 2 - kShiftW - kBsW - 9 * gap) / 8;
		const std::string r3 = shift_ ? "WXCVBN?." : "wxcvbn,;";
		for (int i = 0; i < 8; i++)
		{
			std::string c(1, r3[(size_t)i]);
			keys_.push_back({ x, rowY[2], kw, rowH, c, c, 0 });
			x += kw + gap;
		}
		keys_.push_back({ x, rowY[2], 320 - margin - x, rowH, "<--", "", 1 });

		// 4e rangee : bascule, .com, espace, .fr, OK
		const int kSym = 40;
		const int kCom = 48;
		const int kFr  = 38;
		const int kOk  = 50;
		const int gap4 = 2;
		int x4 = margin;
		keys_.push_back({ x4, rowY[3], kSym, rowH, "123", "", 4 }); x4 += kSym + gap4;
		keys_.push_back({ x4, rowY[3], kCom, rowH, ".com", ".com", 0 }); x4 += kCom + gap4;
		const int kSpace = 320 - margin - x4 - gap4 - kFr - gap4 - kOk - margin;
		keys_.push_back({ x4, rowY[3], kSpace, rowH, "espace", "", 2 }); x4 += kSpace + gap4;
		keys_.push_back({ x4, rowY[3], kFr,  rowH, ".fr", ".fr", 0 });  x4 += kFr + gap4;
		keys_.push_back({ x4, rowY[3], kOk,  rowH, "OK", "", 3 });
	}
	else if (kbdPage_ == 1)
	{
		addRow("1234567890", rowY[0]);
		addRow("-_/:.,?!@=", rowY[1]);

		// 3e rangee : symboles + effacer
		const int margin = 3;
		const int kw = (320 - margin * 2 - 8 * 2) / 9;
		const std::string r3 = "()[]{}#%*";
		for (int i = 0; i < 8; i++)
		{
			std::string c(1, r3[(size_t)i]);
			const int x = margin + i * (kw + 2);
			keys_.push_back({ x, rowY[2], kw, rowH, c, c, 0 });
		}
		keys_.push_back({ margin + 8 * (kw + 2), rowY[2], 320 - margin - (margin + 8 * (kw + 2)), rowH, "<--", "", 1 });

		// 4e rangee
		const int kSym = 40;
		const int kCom = 48;
		const int kFr  = 38;
		const int kOk  = 50;
		const int gap4 = 2;
		int x4 = margin;
		keys_.push_back({ x4, rowY[3], kSym, rowH, "\xC3\xA9\xC3\xA0\xC3\xA7", "", 4 }); x4 += kSym + gap4; // "éàç"
		keys_.push_back({ x4, rowY[3], kCom, rowH, ".com", ".com", 0 }); x4 += kCom + gap4;
		const int kSpace = 320 - margin - x4 - gap4 - kFr - gap4 - kOk - margin;
		keys_.push_back({ x4, rowY[3], kSpace, rowH, "espace", "", 2 }); x4 += kSpace + gap4;
		keys_.push_back({ x4, rowY[3], kFr,  rowH, ".fr", ".fr", 0 });  x4 += kFr + gap4;
		keys_.push_back({ x4, rowY[3], kOk,  rowH, "OK", "", 3 });
	}
	else // kbdPage_ == 2 (accents)
	{
		static const char* r0[10] = { "é", "è", "ê", "ë", "à", "â", "ä", "ù", "û", "ü" };
		static const char* r0Maj[10] = { "É", "È", "Ê", "Ë", "À", "Â", "Ä", "Ù", "Û", "Ü" };
		static const char* r1[10] = { "ç", "î", "ï", "ô", "ö", "œ", "æ", "'", "\"", "-" };
		static const char* r1Maj[10] = { "Ç", "Î", "Ï", "Ô", "Ö", "Œ", "Æ", "'", "\"", "-" };
		static const char* r2[8] = { "ñ", "ÿ", "ŷ", "«", "»", "…", "€", "°" };
		static const char* r2Maj[8] = { "Ñ", "Ÿ", "Ŷ", "«", "»", "…", "€", "°" };

		const int margin = 3;
		const int kw10 = (320 - margin * 2 - 9 * 2) / 10;
		for (int i = 0; i < 10; i++)
		{
			const char* s = shift_ ? r0Maj[i] : r0[i];
			keys_.push_back({ margin + i * (kw10 + 2), rowY[0], kw10, rowH, s, s, 0 });
		}
		for (int i = 0; i < 10; i++)
		{
			const char* s = shift_ ? r1Maj[i] : r1[i];
			keys_.push_back({ margin + i * (kw10 + 2), rowY[1], kw10, rowH, s, s, 0 });
		}

		// 3e rangee : Maj + 8 accents + effacer
		const int kShiftW = 34;
		const int kBsW = 34;
		const int gap = 2;
		int x = margin;
		keys_.push_back({ x, rowY[2], kShiftW, rowH, shift_ ? "MAJ" : "maj", "", 5 });
		x += kShiftW + gap;
		const int kw = (320 - margin * 2 - kShiftW - kBsW - 9 * gap) / 8;
		for (int i = 0; i < 8; i++)
		{
			const char* s = shift_ ? r2Maj[i] : r2[i];
			keys_.push_back({ x, rowY[2], kw, rowH, s, s, 0 });
			x += kw + gap;
		}
		keys_.push_back({ x, rowY[2], 320 - margin - x, rowH, "<--", "", 1 });

		// 4e rangee
		const int kSym = 40;
		const int kCom = 48;
		const int kFr  = 38;
		const int kOk  = 50;
		const int gap4 = 2;
		int x4 = margin;
		keys_.push_back({ x4, rowY[3], kSym, rowH, "abc", "", 4 }); x4 += kSym + gap4;
		keys_.push_back({ x4, rowY[3], kCom, rowH, ".com", ".com", 0 }); x4 += kCom + gap4;
		const int kSpace = 320 - margin - x4 - gap4 - kFr - gap4 - kOk - margin;
		keys_.push_back({ x4, rowY[3], kSpace, rowH, "espace", "", 2 }); x4 += kSpace + gap4;
		keys_.push_back({ x4, rowY[3], kFr,  rowH, ".fr", ".fr", 0 });  x4 += kFr + gap4;
		keys_.push_back({ x4, rowY[3], kOk,  rowH, "OK", "", 3 });
	}
}

void BottomUi::drawKeyboard(Bitmap& b)
{
	for (const auto& k : keys_)
	{
		const bool isOk = (k.action == 3);
		const bool isSpecial = (k.action == 1 || k.action == 2 || k.action == 4 || k.action == 5);
		const bool isShiftActive = (k.action == 5 && shift_);

		roundRect(b, k.x, k.y, k.w, k.h, 6,
			isOk ? C_KEY_OK : (isShiftActive ? C_ACCENT : (isSpecial ? C_SURFACE2 : C_KEY)));

		const uint32_t fg = (isOk || isShiftActive) ? C_BG : C_TEXT;
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
	(void)b;
	static const char* items[8] =
	{
		"Nouvel onglet", "Fermer l'onglet",
		"Ajouter aux favoris", "Favoris",
		"Historique", "Reglages",
		"A propos", "Recharger"
	};
	const int colW = 152;
	const int rowH = 29;
	const int xLeft = 6;
	const int xRight = 162;
	for (int i = 0; i < 8; i++)
	{
		const int col = i % 2;
		const int row = i / 2;
		const int x = (col == 0) ? xLeft : xRight;
		const int y = 92 + row * 31;
		roundRect(bmp_, x, y, colW, rowH, 6, (row & 1) ? C_SURFACE : C_BG_2);
		textCentered(bmp_, items[i], x, colW, y + 6, 0.72f, C_TEXT);
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
	if (listSel_ >= (int)entries.size()) listSel_ = (int)entries.size() - 1;
	for (int i = 0; i < visible; i++)
	{
		int idx = listOffset_ + i;
		if (idx >= (int)entries.size()) break;
		int y = 116 + i * 28;
		bmp_.fillRect(0, y, 320, 26, idx == listSel_ ? C_SURFACE2 : ((i & 1) ? C_NAVY : C_NAVY_2));
		if (idx == listSel_) bmp_.fillRect(0, y, 2, 26, C_ACCENT);
		text(bmp_, fitStr(entries[idx].title, 180, 0.8f), 6, y + 3, 0.8f, C_TEXT, true);
		text(bmp_, fitStr(entries[idx].url, 90, 0.6f), 196, y + 4, 0.6f, C_TEXT_D);
		text(bmp_, "X", 304, y + 4, 0.8f, C_RED, true);
	}
}

void BottomUi::drawSettingsList(Browser& b, WebSettings& set,
	const ReleaseUpdater& updater)
{
	(void)b;
	text(bmp_, "Reglages (haut/bas pour defiler)", 10, 92, 0.72f, C_TEXT_D);

	const std::string engine = searchEngineDisplayName(set.searchEngine);
	const std::string tls = set.verifyTLS ? "Oui" : "Non";
	const std::string img = set.loadImages ? "Oui" : "Non";
	const std::string js = set.enableJS ? "Oui" : "Non";
	std::string update = updater.message();
	if (updater.state() == UpdateState::Downloading)
		update = "Telechargement " + itos((long)(updater.progress() * 100.0f)) + "%";
	else if (updater.state() == UpdateState::Checking)
		update = "Recherche...";

	struct Row { const char* label; std::string value; bool accent; const char* badge; uint32_t badgeColor; };
	const Row rows[7] =
	{
		{ "Moteur de recherche", engine, true, nullptr, 0 },
		{ "Page d'accueil", fitStr(set.homePage.empty() ? "(par defaut)" : set.homePage, 110, 0.7f), false, nullptr, 0 },
		{ "Verifier les certificats", tls, false, nullptr, 0 },
		{ "Charger les images", img, false, "Dev", C_ORANGE },
		{ "JavaScript", js, false, "Beta", C_ORANGE },
		{ "Effacer l'historique", "", false, nullptr, 0 },
		{ "Mises a jour", update, true, nullptr, 0 },
	};

	const int totalRows = 7;
	const int visible = 5;
	const int maxOff = totalRows - visible; // 2
	if (listOffset_ < 0) listOffset_ = 0;
	if (listOffset_ > maxOff) listOffset_ = maxOff;

	for (int i = 0; i < visible; i++)
	{
		const int idx = listOffset_ + i;
		if (idx >= totalRows) break;
		const int y = 110 + i * 21;
		roundRect(bmp_, 4, y, 302, 18, 4, (idx & 1) ? C_SURFACE : C_BG_2);

		int lx = 10;
		text(bmp_, rows[idx].label, lx, y + 2, 0.62f, C_TEXT);
		if (rows[idx].badge)
		{
			lx += textW(rows[idx].label, 0.62f) + 6;
			const int bw = textW(rows[idx].badge, 0.48f) + 6;
			roundRect(bmp_, lx, y + 3, bw, 12, 3, rows[idx].badgeColor);
			text(bmp_, rows[idx].badge, lx + 3, y + 3, 0.48f, C_BLACK);
		}

		if (!rows[idx].value.empty())
		{
			const std::string valStr = fitStr(rows[idx].value, 130, 0.52f);
			const uint32_t valCol = rows[idx].accent ? C_ACCENT : (idx == 4 ? C_TEXT_DD : C_TEXT_D);
			text(bmp_, valStr,
				300 - textW(valStr, 0.52f),
				y + 2, 0.52f,
				valCol);
		}
	}

	// Indicateur de defilement a droite
	const int trackY = 110;
	const int trackH = 105;
	const int thumbH = (visible * trackH) / totalRows;
	const int thumbY = trackY + (listOffset_ * (trackH - thumbH)) / maxOff;
	roundRect(bmp_, 310, trackY, 4, trackH, 2, C_BG_2);
	roundRect(bmp_, 310, thumbY, 4, thumbH, 2, C_SURFACE2);
}

void BottomUi::drawAbout()
{
	text(bmp_, "Pixelnaute", 12, 92, 1.15f, C_TEXT, true);
	text(bmp_, "Version " NW_VERSION_STRING " (" __DATE__ ")", 12, 122, 0.75f, C_TEXT_DD);
	text(bmp_, "Navigateur autonome pour New Nintendo 3DS", 12, 144, 0.75f, C_TEXT);

	text(bmp_, "Aucun PC, aucun proxy : connexion directe.", 12, 166, 0.72f, C_TEXT_D);
	text(bmp_, "Moteur de recherche reglable dans Reglages.", 12, 188, 0.72f, C_TEXT_D);
	text(bmp_, "Projet original : @dodosiiii", 12, 205, 0.65f, C_TEXT_DD);
	text(bmp_, "github.com/dodosiiii/Pixelnaute", 12, 218, 0.6f, C_TEXT_DD);
	text(bmp_, "Touchez l'ecran pour revenir.", 12, 231, 0.55f, C_TEXT_DD);
}

//--------------------------------------------------------------------------
// Frame principale de l'ecran bas
//--------------------------------------------------------------------------
void BottomUi::frame(Browser& b, NetworkManager& net, WebSettings& set,
	ReleaseUpdater& updater)
{
	const UpdateState updateState = updater.state();
	const int updateProgress = (int)(updater.progress() * 100.0f);
	if (updateState != lastUpdateState_ ||
		(updateState == UpdateState::Downloading &&
		 updateProgress != lastUpdateProgress_))
		dirty_ = true;
	lastUpdateState_ = updateState;
	lastUpdateProgress_ = updateProgress;

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
			drawSettingsList(b, set, updater);
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

int BottomUi::tabHit(int x, int y, int count) const
{
	if (y < 4 || y >= 26) return -1;
	if (count <= 0) return -1;
	return x / (320 / count);
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

		case 4: // onglet suivant ou nouvel onglet
			if (b.count() > 1)
			{
				b.nextTab();
				mode_ = UiMode::Browsing;
			}
			else if (b.count() >= NW_MAX_TABS)
			{
				toast("Maximum d'onglets atteint");
			}
			else
			{
				b.newTab();
				mode_ = UiMode::Browsing;
			}
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
		case 2:
			buf_ += ' ';
			if (shift_) { shift_ = false; buildKeys(); }
			break;
		case 3: commitBuf(b, set); break;
		case 4:
			kbdPage_ = (kbdPage_ + 1) % 3;
			buildKeys();
			break;
		case 5:
			shift_ = !shift_;
			buildKeys();
			break;
		default:
			buf_ += k.value;
			if (shift_)
			{
				shift_ = false;
				buildKeys();
			}
			break;
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

void BottomUi::handleTouch(Browser& b, NetworkManager& net, WebSettings& set,
	ReleaseUpdater& updater, const InputState& in)
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

	if (mode_ == UiMode::Settings && in.touch && !in.touchDown)
	{
		if (in.dragY < -6 && listOffset_ < 2)
		{
			listOffset_++;
			dirty_ = true;
		}
		else if (in.dragY > 6 && listOffset_ > 0)
		{
			listOffset_--;
			dirty_ = true;
		}
	}

	if (!in.touchDown)
	{
		// Relachement : effacer l'etat "appuye"
		if (touchNav_ >= 0)
		{
			touchNav_ = -1;
			dirty_ = true;
		}
		return;
	}
	int x = in.tp.px;
	int y = in.tp.py;
	if (x >= 320) return; // ecran haut gere ailleurs

	// boutons de navigation (toujours cliquables)
	int ni = navHit(x, y);
	if (ni >= 0)
	{
		if (touchNav_ != ni)
		{
			touchNav_ = ni;
			dirty_ = true;
		}
		navAction(b, net, set, ni);
		return;
	}
	// hors nav : on efface l'etat presse
	if (touchNav_ >= 0) { touchNav_ = -1; dirty_ = true; }
	// onglets
	int ti = tabHit(x, y, b.count());
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
			if (y < 92 || y >= 216) break;
			int row = (y - 92) / 31;
			if (row < 0 || row >= 4) break;
			int col = (x < 160) ? 0 : 1;
			int idx = row * 2 + col;
			switch (idx)
			{
				case 0:
					if (b.count() >= NW_MAX_TABS) toast("Maximum d'onglets atteint");
					else b.newTab();
					mode_ = UiMode::Browsing;
					dirty_ = true;
					break;
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
				case 3:
					mode_ = UiMode::Bookmarks;
					listOffset_ = 0;
					listSel_ = 0;
					pendingBookmarkDelete_.clear();
					dirty_ = true;
					break;
				case 4: mode_ = UiMode::History; listOffset_ = 0; dirty_ = true; break;
				case 5: mode_ = UiMode::Settings; dirty_ = true; break;
				case 6: mode_ = UiMode::About; dirty_ = true; break;
				case 7: b.reload(); mode_ = UiMode::Browsing; dirty_ = true; break;
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
				listSel_ = idx;
				if (x >= 296)
				{
					const std::string url = b.bookmarks().entries[idx].url;
					if (pendingBookmarkDelete_ == url && timeMs() <= bookmarkDeleteUntil_)
					{
						b.bookmarks().remove(url);
						pendingBookmarkDelete_.clear();
						const int count = (int)b.bookmarks().entries.size();
						if (listSel_ >= count) listSel_ = count > 0 ? count - 1 : 0;
						toast("Favori supprime");
					}
					else
					{
						pendingBookmarkDelete_ = url;
						bookmarkDeleteUntil_ = timeMs() + 2000;
						toast("Touchez encore pour supprimer");
					}
				}
				else
				{
					pendingBookmarkDelete_.clear();
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
				if (updater.active()) break;
				mode_ = UiMode::Browsing;
				dirty_ = true;
			}
			else if (x >= 306 && y >= 110 && y < 215)
			{
				if (y < 162 && listOffset_ > 0) { listOffset_--; dirty_ = true; }
				else if (y >= 162 && listOffset_ < 2) { listOffset_++; dirty_ = true; }
			}
			else if (y >= 110 && y < 215)
			{
				int slot = (y - 110) / 21;
				if (slot < 0) slot = 0;
				if (slot >= 5) slot = 4;
				int idx = listOffset_ + slot;
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
					case 3:
						set.loadImages = !set.loadImages;
						settingsSave(set);
						toast(set.loadImages ? "Images activees (en developpement)" : "Images desactivees");
						dirty_ = true;
						break;
					case 4:
						set.enableJS = !set.enableJS;
						if (!b.syncJavaScriptContexts())
						{
							set.enableJS = false;
							b.syncJavaScriptContexts();
							toast("Memoire insuffisante pour JavaScript");
						}
						else
						{
							settingsSave(set);
							toast(set.enableJS ? "JavaScript active (scripts a venir)" : "JavaScript desactive");
						}
						dirty_ = true;
						break;
					case 5: b.history().clear(); toast("Historique efface"); dirty_ = true; break;
					case 6:
						if (updater.active())
							toast("Mise a jour deja en cours");
						else if (net.busy())
							toast("Attendez la fin du chargement");
						else if (updater.startCheck())
							toast("Recherche de mise a jour...");
						else
							toast(updater.message());
						dirty_ = true;
						break;
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

void BottomUi::handleKeys(Browser& b, NetworkManager& net, WebSettings& set,
	ReleaseUpdater& updater, u32 kDown)
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
			if (mode_ == UiMode::Bookmarks)
			{
				const int count = (int)b.bookmarks().entries.size();
				const int visible = (222 - 116) / 28;
				if (kDown & KEY_DUP)
				{
					if (listSel_ > 0) listSel_--;
					if (listSel_ < listOffset_) listOffset_ = listSel_;
					dirty_ = true;
					return;
				}
				if (kDown & KEY_DDOWN)
				{
					if (listSel_ + 1 < count) listSel_++;
					if (listSel_ >= listOffset_ + visible) listOffset_ = listSel_ - visible + 1;
					dirty_ = true;
					return;
				}
				if (kDown & KEY_X)
				{
					if (listSel_ >= 0 && listSel_ < count)
					{
						b.bookmarks().remove(b.bookmarks().entries[listSel_].url);
						const int remaining = (int)b.bookmarks().entries.size();
						if (listSel_ >= remaining) listSel_ = remaining > 0 ? remaining - 1 : 0;
						pendingBookmarkDelete_.clear();
						toast("Favori supprime");
					}
					dirty_ = true;
					return;
				}
			}
			else
			{
				if (kDown & KEY_DUP) { if (listOffset_ > 0) listOffset_--; dirty_ = true; return; }
				if (kDown & KEY_DDOWN) { listOffset_++; dirty_ = true; return; }
			}
			break;

		case UiMode::Settings:
			if (kDown & KEY_B)
			{
				if (updater.active())
					return;
				mode_ = UiMode::Browsing;
				dirty_ = true;
				return;
			}
			if (kDown & (KEY_DUP | KEY_CPAD_UP))
			{
				if (listOffset_ > 0)
				{
					listOffset_--;
					dirty_ = true;
				}
				return;
			}
			if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN))
			{
				if (listOffset_ < 2)
				{
					listOffset_++;
					dirty_ = true;
				}
				return;
			}
			break;

		case UiMode::Menu:
		case UiMode::About:
			if (kDown & KEY_B)
			{
				mode_ = UiMode::Browsing;
				dirty_ = true;
				return;
			}
			break;

		default:
			break;
	}
}
