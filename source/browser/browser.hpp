#pragma once
//=============================================================================
// browser.hpp - Cœur du navigateur : onglets, navigation, pipeline page
//=============================================================================
#include "../common.hpp"
#include "../html/dom.hpp"
#include "../css/css.hpp"
#include "../render/layout.hpp"
#include "../image/image_store.hpp"
#include "../settings/settings.hpp"
#include "../network/network.hpp"
#include "../storage/storage.hpp"
#include <string>
#include <vector>

class Browser
{
public:
	struct Tab
	{
		std::string url;
		std::string title;
		Document doc;             // DOM
		CssEngine css;            // feuilles de style
		PageModel page;           // rendu
		int scrollY = 0;
		bool pageReady = false;
		bool isSearchPage = false;
		std::vector<std::string> back;   // historique arriere (URL)
		std::vector<std::string> fwd;    // historique avant
		int focusNode = -1;              // index du champ de formulaire en edition
	};

	Browser(NetworkManager& net, WebSettings& settings);

	// --- navigation ---
	void openInput(const std::string& input);        // URL intelligente ou recherche
	void openUrl(const std::string& url, bool record = true);
	void openSearch(const std::string& query);
	void back();
	void forward();
	void reload();
	void stop();
	void goHome();
	void openBookmark(const std::string& url);

	// --- onglets ---
	void newTab(const std::string& url = "");
	void closeTab(int index);
	void selectTab(int index);
	void nextTab(); void prevTab();

	// --- pipeline page (a appeler a chaque frame) ---
	void pollNetwork();
	bool tickImages();
	bool layoutPending() const { return layoutPending_; }

	// --- pages internes ---
	void showInternalHome();              // page d'accueil interne (start page)
	bool showInternal(const std::string& url); // nw://... -> true si gere
	std::string internalHomeHtml(const std::string& engine);
	void renderInternal(int tabIdx, const std::string& html);
	// --- interaction page ---
	int  hitLink(int x, int y) const;
	void tap(int x, int y);
	void beginEditField(int fieldIndex);
	bool setFieldValue(int fieldIndex, const std::string& v);
	void submitFormByNode(int submitNode);
	void scroll(int dy);

	// Position de defilement courante (pour le geste tactile).
	int  scrollY() const;
	// Place le defilement a une position absolue, bornee a la page.
	void scrollTo(int y);
	// Vrai si la page peut encore defiler dans cette direction.
	bool canScrollBy(int dy) const;

	Tab&    currentTab();
	const Tab& tab(int index) const { return tabs_[index]; }
	int     count() const { return (int)tabs_.size(); }
	int     index() const { return current_; }

	// --- divers ---
	std::string searchEngineName() const;
	History&  history() { return history_; }
	Bookmarks& bookmarks() { return bookmarks_; }
	// Vignettes des pages (logos, illustrations).
	ImageStore& images() { return images_; }

	// Action interne demandee par la page (nw://bookmarks, etc.),
	// consommee par l'interface (ui.openInternal).
	std::string takeInternalNav();
	bool internalNavPending() const { return !internalNav_.empty(); }

private:
	NetworkManager& net_;
	WebSettings& set_;
	ImageStore images_;
	std::vector<Tab> tabs_;
	int current_ = 0;
	History history_;
	Bookmarks bookmarks_;
	std::string internalNav_;   // prochaine action interne (consommee par l'UI)
	NetResponse pendingResponse_;
	bool responsePending_ = false;
	bool layoutPending_ = false;
	int requestTab_ = -1;
	std::vector<std::string> pendingImages_;
	std::string pendingImagesReferer_;

	void loadUrlInto(int tabIdx, const std::string& url, bool record);
	void resetTab(Tab& t);
	void cancelTabRequest(int tabIdx);
	void onResponse(NetResponse& resp);
	void renderInto(int tabIdx, Document&& doc);
	void queueImages(int tabIdx, int maximum);
	int  submitHit(const PageModel& page, int x, int y) const;
	void submitFormByPage(int submitIndex);
};

// Detection "est-ce une URL ?" (utilise aussi par l'interface)
bool looksLikeUrl(const std::string& input);

// URL du moteur de recherche pour une requete (voir search/)
std::string searchUrl(const std::string& query, const std::string& engine);