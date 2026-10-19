#pragma once
//=============================================================================
// ui.hpp - Interface de l'ecran bas : onglets, navigation, clavier, menus
//
// Toute l'interface est dessinee en logiciel dans un Bitmap 320x240 puis
// affichee via une texture C3D (aucune ressource externe necessaire).
//=============================================================================
#include "../common.hpp"
#include "../input/input.hpp"
#include "../browser/browser.hpp"
#include "../render/bitmap.hpp"
#include "../settings/settings.hpp"
#include "../network/network.hpp"
#include "../updater/updater.hpp"
#include <citro2d.h>
#include <string>
#include <vector>

enum class UiMode
{
	Browsing = 0,  // page web + barre de navigation
	Address,       // saisie d'adresse / de recherche (clavier)
	FieldEdit,     // edition d'un champ de formulaire
	Menu,          // menu principal
	History,       // liste de l'historique
	Bookmarks,     // liste des favoris
	Settings,      // reglages
	Cache,         // cache des pages
	About,         // a propos
};

// Touche du clavier logiciel.
struct UiKey
{
	int x = 0, y = 0, w = 0, h = 0;
	std::string label;
	std::string value;  // texte insere (clavier)
	int action = 0;     // 0=texte, 1=effacer, 2=espace, 3=valider, 4=page chiffres/lettres
};

class BottomUi
{
public:
	bool init();
	void shutdown();

	// Dessine l'ecran bas (appeler apres C2D_SceneBegin de l'ecran bas).
	void frame(Browser& b, NetworkManager& net, WebSettings& set,
		ReleaseUpdater& updater);

	// Touches / toucher (appeler une fois par frame).
	void handleKeys(Browser& b, NetworkManager& net, WebSettings& set,
		ReleaseUpdater& updater, u32 kDown);
	void handleAnalog(Browser& b, NetworkManager& net, WebSettings& set,
		const InputState& in);
	void handleTouch(Browser& b, NetworkManager& net, WebSettings& set,
		ReleaseUpdater& updater, const InputState& in);

	UiMode mode() const { return mode_; }

	// Ouvre la barre d'adresse (initial = texte de depart).
	void openAddress(const std::string& initial);

	// Passe en mode edition du champ de formulaire focusNode.
	void startFieldEdit(Browser& b);

	// Action interne demandee par une page (nw://bookmarks, ...).
	void openInternal(const std::string& url);

	// Message temporaire (ex. telechargement termine).
	void toast(const std::string& msg);

private:
	// --- etat ---
	UiMode mode_ = UiMode::Browsing;
	std::string buf_;            // tampon clavier (adresse / champ)
	size_t cursorPos_ = 0;
	int focusIdx_ = -1;          // index du champ edite
	bool homeTarget_ = false;    // le clavier edite la page d'accueil
	std::string toastMsg_;
	uint64_t toastUntil_ = 0;
	int listOffset_ = 0;         // defilement des listes
	int listSel_ = 0;            // favori selectionne au clavier
	std::string pendingBookmarkDelete_;
	uint64_t bookmarkDeleteUntil_ = 0;
	int kbdPage_ = 0;            // 0 = lettres, 1 = chiffres/symboles, 2 = accents
	int keyboardFocus_ = 0;
	int menuSel_ = 0;
	int analogDirection_ = -1;
	uint64_t analogRepeatAt_ = 0;
	int caretDirection_ = 0;
	uint64_t caretRepeatAt_ = 0;
	bool shift_ = false;         // majuscule temporaire
	uint64_t lastUpload_ = 0;
	bool dirty_ = true;
	int repeatKey_ = -1;
	uint64_t repeatNext_ = 0;
	int repeatCount_ = 0;
	int touchNav_ = -1;          // index bouton nav enfonce
	std::string language_ = "en";
	int uiTheme_ = 0;
	std::vector<PageSegmentCache::SiteEntry> cachedSites_;
	bool cacheScanOk_ = true;
	bool cacheInfoReady_ = false;
	uint64_t cachedPageBytes_ = 0;
	std::string pendingCacheDelete_;
	uint64_t cacheDeleteUntil_ = 0;
	UpdateState lastUpdateState_ = UpdateState::Idle;
	int lastUpdateProgress_ = -1;

	// --- rendu ---
	Bitmap bmp_;                 // 320x240 (echelle 1 pixel = 1 px ecran)
	C3D_Tex tex_{};
	void* texData_ = nullptr;
	bool texReady_ = false;
	Tex3DS_SubTexture subtex_{};
	C2D_Image image_{};
	std::vector<UiKey> keys_;    // clavier construit pour le mode courant

	// --- helpers graphiques ---
	void text(Bitmap& b, const std::string& t, int x, int y, float s,
		uint32_t c, bool bold = false);
	int  textW(const std::string& t, float s) const;
	std::string fitStr(const std::string& t, int maxW, float s) const;

	// Rectangles a coins arrondis (style "telephone").
	void roundRect(Bitmap& b, int x, int y, int w, int h, int radius, uint32_t color);
	void roundRectOutline(Bitmap& b, int x, int y, int w, int h, int radius,
		uint32_t edge, int thickness);
	// Texte centre horizontalement dans une zone.
	void textCentered(Bitmap& b, const std::string& t, int x, int w, int y,
		float s, uint32_t c, bool bold = false);
	// Icone de glyphe dessinee en pixels (fleches de navigation).
	void navGlyph(Bitmap& b, int kind, int cx, int cy, uint32_t color);

	void drawChrome(Browser& b, NetworkManager& net, WebSettings& set);
	void drawTabs(Browser& b);
	void drawAddressBar(Browser& b, NetworkManager& net);
	void drawNavRow(Browser& b, NetworkManager& net);
	void drawStatusRow(Browser& b, NetworkManager& net);
	void drawKeyboard(Bitmap& b);
	void buildKeys();
	void drawMenu(Browser& b);
	void drawHistoryList(Browser& b);
	void drawBookmarksList(Browser& b);
	void drawSettingsList(Browser& b, WebSettings& set,
		const ReleaseUpdater& updater);
	void drawCacheList(Browser& b);
	void drawAbout();

	// --- interactions ---
	void applyKey(Browser& b, WebSettings& set, const UiKey& k);
	void navAction(Browser& b, NetworkManager& net, WebSettings& set, int idx);
	int  navHit(int x, int y) const;          // -1 si hors des boutons
	int  tabHit(int x, int y, int count) const; // index onglet ou -1
	void keyHit(Browser& b, WebSettings& set, int x, int y);
	void moveKeyboardFocus(int dx, int dy);
	void moveCaret(int direction);
	void activateMenuItem(Browser& b, int index);

	void uploadIfNeeded();
	void commitBuf(Browser& b, WebSettings& set);
};
