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
#include "../js/js_runtime.hpp"
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
		JSRuntime* jsRuntime = nullptr;
		JSContext* jsContext = nullptr;
	};

	Browser(NetworkManager& net, WebSettings& settings);
	~Browser();

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
	bool tickStyles();
	bool stylesPending() const;
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
	bool syncJavaScriptContexts();

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
	bool    canGoBack() const { return current_ >= 0 && current_ < (int)tabs_.size() && !tabs_[current_].back.empty(); }
	bool    canGoForward() const { return current_ >= 0 && current_ < (int)tabs_.size() && !tabs_[current_].fwd.empty(); }

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
	struct CssRequest { std::string url; std::string referer; };
	std::vector<CssRequest> cssQueue_;
	int cssQueueTab_ = -1;
	int cssRequestTab_ = -1;
	std::string cssPageUrl_;
	uint64_t cssStartMs_ = 0;
	int cssRelayoutCount_ = 0;
	bool styleRedrawPending_ = false;
	bool cssLayoutPending_ = false;
	int cssLayoutTab_ = -1;
	std::string cssLayoutUrl_;
	std::vector<std::string> pendingImages_;
	std::string pendingImagesReferer_;
	int imageRequestTab_ = -1;
	std::string imageRequestUrl_;
	std::string imageRequestReferer_;
	bool imageRedrawPending_ = false;
	int pendingImagesTab_ = -1;

	void loadUrlInto(int tabIdx, const std::string& url, bool record, const std::string& method = "GET", const std::string& postData = "");
	void resetTab(Tab& t);
	bool initializeJavaScript(Tab& t);
	void releaseJavaScript(Tab& t);
	void cancelTabRequest(int tabIdx);
	void clearStyleQueue();
	void queueStyles(int tabIdx, const Document& doc, const std::string& pageUrl);
	void onStylesheetResponse(NetResponse& resp);
	void onResponse(NetResponse& resp);
	void renderInto(int tabIdx, Document&& doc);
	void rerenderTab(int tabIdx);
	void queueImages(int tabIdx, int maximum);
	void clearImageQueue();
	int  submitHit(const PageModel& page, int x, int y) const;
	void submitFormByPage(int submitIndex);
};

// Detection "est-ce une URL ?" (utilise aussi par l'interface)
bool looksLikeUrl(const std::string& input);

// URL du moteur de recherche pour une requete (voir search/)
std::string searchUrl(const std::string& query, const std::string& engine);
