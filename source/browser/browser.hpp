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
#include "../cache/page_segment_cache.hpp"
#include "../js/js_runtime.hpp"
#include <string>
#include <vector>
#include <memory>

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
		bool jsRequired = false;
		std::vector<std::string> back;   // historique arriere (URL)
		std::vector<std::string> fwd;    // historique avant
		int focusNode = -1;              // index du champ de formulaire en edition
		uint64_t renderedDomVersion = 0;
		uint64_t pageCacheKey = 0;
		int failedSegmentIndex = -1;
		JSRuntime* jsRuntime = nullptr;
		JSContext* jsContext = nullptr;
		std::shared_ptr<JSPageBinding> jsBinding = std::make_shared<JSPageBinding>();
		std::unique_ptr<Document> readerOriginal;
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
	bool downloadUrl(const std::string& url);

	// --- onglets ---
	void newTab(const std::string& url = "");
	void closeTab(int index);
	void selectTab(int index);
	void nextTab(); void prevTab();

	// --- pipeline page (a appeler a chaque frame) ---
	void pollNetwork();
	bool tickStyles();
	bool tickScripts();
	bool stylesPending() const;
	bool scriptsPending() const;
	bool tickImages();
	bool tickPageResponse();
	bool tickProgressiveLayout();
	bool layoutPending() const { return layoutPending_; }
	const std::string& progressStatus() const { return progressStatus_; }

	// --- pages internes ---
	void showInternalHome();              // page d'accueil interne (start page)
	bool showInternal(const std::string& url); // nw://... -> true si gere
	std::string internalHomeHtml(const std::string& engine);
	void renderInternal(int tabIdx, const std::string& html);
	// --- interaction page ---
	int  hitLink(int x, int y) const;
	void tap(int x, int y);
	void beginEditField(int fieldIndex);
	void endEditField();
	bool setFieldValue(int fieldIndex, const std::string& v);
	void submitFormByNode(int submitNode);
	float pageScale() const;
	int pageXFromView(int x) const;
	int pageYFromView(int y) const;
	bool enqueuePageFetch(JSPageBinding* binding, int tabIndex, uint32_t promiseId, bool xhr,
		const std::string& url, const std::string& method, const std::string& body);
	void scroll(int dy);
	bool syncJavaScriptContexts();

	// Position de defilement courante (pour le geste tactile).
	int  scrollY() const;
	// Place le defilement a une position absolue, bornee a la page.
	void scrollTo(int y);
	void toggleReaderMode();
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
	bool pageCacheUsage(uint64_t& bytes) const;
	bool listCachedPages(std::vector<PageSegmentCache::SiteEntry>& entries) const;
	bool clearCachedPage(uint64_t pageKey);
	bool clearCachedSite(const std::vector<uint64_t>& pageKeys);
	bool clearPageCache();
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
	PageSegmentCache pageSegmentCache_;
	std::vector<Tab> tabs_;
	std::vector<std::string> tlsSessionExceptions_; // autorisations HTTPS explicites, perdues a la fermeture
	std::string pendingTlsHost_; // hote de l'erreur SSL affichee a l'utilisateur
	int current_ = 0;
	History history_;
	Bookmarks bookmarks_;
	std::string internalNav_;   // prochaine action interne (consommee par l'UI)
	NetResponse pendingResponse_;
	bool responsePending_ = false;
	bool layoutPending_ = false;
	bool segmentPending_ = false;
	int segmentTab_ = -1;
	int segmentIndexPending_ = -1;
	int pendingScrollY_ = -1;
	std::string progressStatus_;
	int requestTab_ = -1;
	struct CssRequest { std::string url; std::string referer; int sourceOrder = -1; };
	std::vector<CssRequest> cssQueue_;
	int cssQueueTab_ = -1;
	int cssRequestTab_ = -1;
	int cssRequestOrder_ = -1;
	std::string cssRequestUrl_;
	std::string cssPageUrl_;
	uint64_t cssStartMs_ = 0;
	bool styleRedrawPending_ = false;
	bool cssLayoutPending_ = false;
	int cssLayoutTab_ = -1;
	std::string cssLayoutUrl_;
	struct ScriptRequest { std::string url; std::string referer; std::string source; bool external = false; };
	std::vector<ScriptRequest> scriptQueue_;
	int scriptQueueTab_ = -1;
	int scriptRequestTab_ = -1;
	std::string scriptPageUrl_;
	std::string scriptRequestUrl_;
	bool scriptRedrawPending_ = false;
	std::vector<std::string> pendingImages_;
	std::string pendingImagesReferer_;
	int imageRequestTab_ = -1;
	std::string imageRequestUrl_;
	std::string imageRequestReferer_;
	bool imageRedrawPending_ = false;
	int pendingImagesTab_ = -1;
	struct FetchJob
	{
		int tabIndex = -1;
		uint32_t promiseId = 0;
		uint32_t generation = 0;
		bool xhr = false;
		std::weak_ptr<JSPageBinding> binding;
		std::string url, method, body, referer;
	};
	std::vector<FetchJob> fetchQueue_;
	bool fetchActive_ = false;

	void loadUrlInto(int tabIdx, const std::string& url, bool record, const std::string& method = "GET", const std::string& postData = "", const std::string& referer = "", const std::string& csrfToken = "");
	void resetTab(Tab& t);
	bool initializeJavaScript(Tab& t);
	void releaseJavaScript(Tab& t);
	void cancelTabRequest(int tabIdx);
	void clearStyleQueue();
	void queueStyles(int tabIdx, const Document& doc, const std::string& pageUrl);
	void onStylesheetResponse(NetResponse& resp);
	void onScriptResponse(NetResponse& resp);
	void queueScripts(int tabIdx, const Document& doc, const std::string& pageUrl);
	void clearScriptQueue();
	int layoutWidth() const { return set_.largePage ? 640 : NW_PAGE_WIDTH; }
	void trimInactiveTabs();
	bool verifyTlsFor(const std::string& url) const;
	size_t safePageBodyLimit() const;
	void onResponse(NetResponse& resp);
	void renderInto(int tabIdx, Document&& doc);
	void rerenderTab(int tabIdx);
	void requestSegment(int tabIdx, int segmentIndex, int desiredScrollY);
	void queueImages(int tabIdx, int maximum);
	void clearImageQueue();
	void serviceFetchQueue();
	void onFetchResponse(NetResponse& response);
	void cancelFetchQueue(const char* reason);
	int  submitHit(const PageModel& page, int x, int y) const;
	void submitFormByPage(int submitIndex);
};

// Detection "est-ce une URL ?" (utilise aussi par l'interface)
bool looksLikeUrl(const std::string& input);

// URL du moteur de recherche pour une requete (voir search/)
std::string searchUrl(const std::string& query, const std::string& engine);
