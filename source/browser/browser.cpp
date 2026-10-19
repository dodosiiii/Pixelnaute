#include "browser.hpp"
#include "../search/search.hpp"
#include "../search/search_page.hpp"
#include "../render/text.hpp"
#include "../html/parser.hpp"
#include "../storage/downloads.hpp"
#include "../diag/diag.hpp"
#include "../js/js_runtime.hpp"
#include <3ds/allocator/linear.h>
#include <algorithm>
#include <functional>

static CssEngine g_emptyCss;

static int maxPageScroll(const PageModel& page)
{
	const int contentHeight = std::min(page.height,
		(int)NW_MAX_PAGE_CONTENT_HEIGHT);
	return std::max(0, contentHeight - NW_TOP_HEIGHT);
}

static void fingerprintBytes(uint64_t& hash, const void* data, size_t size)
{
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	for (size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ULL;
	}
}

static void fingerprintString(uint64_t& hash, const std::string& value)
{
	fingerprintBytes(hash, value.data(), value.size());
	const unsigned char separator = 0xFF;
	fingerprintBytes(hash, &separator, sizeof(separator));
}

static uint64_t pageFingerprint(const Browser::Tab& tab)
{
	uint64_t hash = 1469598103934665603ULL;
	fingerprintString(hash, tab.url);
	fingerprintBytes(hash, &tab.doc.hoveredNode, sizeof(tab.doc.hoveredNode));
	fingerprintBytes(hash, &tab.doc.focusedNode, sizeof(tab.doc.focusedNode));
	for (const Node& node : tab.doc.nodes)
	{
		const uint8_t type = (uint8_t)node.type;
		fingerprintBytes(hash, &type, sizeof(type));
		fingerprintString(hash, node.tag.c_str());
		fingerprintString(hash, node.text);
		for (const Attribute& attr : node.attrs)
		{
			fingerprintString(hash, attr.name);
			fingerprintString(hash, attr.value);
		}
	}
	for (const CssRule& rule : tab.css.rules())
	{
		fingerprintBytes(hash, &rule.specificity, sizeof(rule.specificity));
		fingerprintBytes(hash, &rule.order, sizeof(rule.order));
		for (const CssCompoundSelector& selector : rule.selectorChain)
		{
			fingerprintString(hash, selector.tag);
			fingerprintString(hash, selector.id);
			fingerprintString(hash, selector.notSelector);
			fingerprintBytes(hash, &selector.combinator, sizeof(selector.combinator));
			fingerprintBytes(hash, &selector.firstChild, sizeof(selector.firstChild));
			fingerprintBytes(hash, &selector.hover, sizeof(selector.hover));
			fingerprintBytes(hash, &selector.focus, sizeof(selector.focus));
			fingerprintBytes(hash, &selector.checked, sizeof(selector.checked));
			fingerprintBytes(hash, &selector.disabled, sizeof(selector.disabled));
			for (const std::string& cls : selector.classes) fingerprintString(hash, cls);
			for (const CssAttrSelector& attr : selector.attrs)
			{
				fingerprintString(hash, attr.name);
				fingerprintString(hash, attr.value);
			}
		}
		for (const CssDeclaration& declaration : rule.declarations)
		{
			fingerprintString(hash, declaration.property);
			fingerprintString(hash, declaration.value);
		}
		const Style& style = rule.style;
		fingerprintBytes(hash, &style.declared, sizeof(style.declared));
		fingerprintBytes(hash, &style.important, sizeof(style.important));
		fingerprintBytes(hash, &style.colorSet, sizeof(style.colorSet));
		fingerprintBytes(hash, &style.color, sizeof(style.color));
		fingerprintBytes(hash, &style.bgSet, sizeof(style.bgSet));
		fingerprintBytes(hash, &style.bg, sizeof(style.bg));
		fingerprintBytes(hash, &style.fontSize, sizeof(style.fontSize));
		fingerprintBytes(hash, &style.marginTop,
			sizeof(style.marginTop) * 4);
		fingerprintBytes(hash, &style.paddingTop,
			sizeof(style.paddingTop) * 4);
		fingerprintBytes(hash, &style.width, sizeof(style.width) * 3);
		fingerprintBytes(hash, &style.height, sizeof(style.height));
		fingerprintBytes(hash, &style.displayNone, sizeof(style.displayNone));
		fingerprintBytes(hash, &style.block, sizeof(style.block));
		fingerprintBytes(hash, &style.displayInline, sizeof(style.displayInline));
		fingerprintBytes(hash, &style.bold, sizeof(style.bold));
		fingerprintBytes(hash, &style.italic, sizeof(style.italic));
		fingerprintBytes(hash, &style.underline, sizeof(style.underline));
		fingerprintBytes(hash, &style.align, sizeof(style.align));
		fingerprintBytes(hash, &style.mono, sizeof(style.mono));
		fingerprintBytes(hash, &style.listStyleNone, sizeof(style.listStyleNone));
		fingerprintBytes(hash, &style.borderBox, sizeof(style.borderBox));
		fingerprintBytes(hash, &style.flex, sizeof(style.flex));
		fingerprintBytes(hash, &style.flexRow, sizeof(style.flexRow));
		fingerprintBytes(hash, &style.flexWrap, sizeof(style.flexWrap));
		fingerprintBytes(hash, &style.justifyContent, sizeof(style.justifyContent));
		fingerprintBytes(hash, &style.rowGap, sizeof(style.rowGap) * 2);
		fingerprintBytes(hash, &style.borderWidth, sizeof(style.borderWidth));
		fingerprintBytes(hash, &style.borderColor, sizeof(style.borderColor));
		fingerprintBytes(hash, &style.lineHeight, sizeof(style.lineHeight));
		fingerprintBytes(hash, &style.lineHeightRelative,
			sizeof(style.lineHeightRelative));
		fingerprintBytes(hash, &style.lineHeightUnitless,
			sizeof(style.lineHeightUnitless));
		fingerprintBytes(hash, &style.gradientColors, sizeof(style.gradientColors));
		fingerprintBytes(hash, &style.gradientStops, sizeof(style.gradientStops));
		fingerprintBytes(hash, &style.gradientAngle, sizeof(style.gradientAngle));
		fingerprintBytes(hash, &style.gradientRadial, sizeof(style.gradientRadial));
		fingerprintBytes(hash, &style.borderRadius, sizeof(style.borderRadius));
		fingerprintBytes(hash, &style.shadowX, sizeof(style.shadowX) * 4);
		fingerprintBytes(hash, &style.shadowColor, sizeof(style.shadowColor));
		fingerprintBytes(hash, &style.boxShadowSet, sizeof(style.boxShadowSet));
	}
	return hash;
}

static bool enqueueFetchBridge(void* owner, JSPageBinding* binding, int tabIndex,
	uint32_t promiseId, bool xhr, const std::string& url, const std::string& method,
	const std::string& body)
{
	return owner && static_cast<Browser*>(owner)->enqueuePageFetch(binding, tabIndex,
		promiseId, xhr, url, method, body);
}

static std::string cssColorValue(uint32_t color)
{
	char value[8];
	snprintf(value, sizeof(value), "#%02x%02x%02x",
		(unsigned int)(color & 0xff), (unsigned int)((color >> 8) & 0xff),
		(unsigned int)((color >> 16) & 0xff));
	return value;
}

static bool hasRunnableScripts(const Document& document, const std::string& pageUrl)
{
	int externalCount = 0;
	int inlineCount = 0;
	for (const Node& node : document.nodes)
	{
		if (!node.isElement() || node.tag != "script") continue;
		const char* type = node.getAttr("type");
		const std::string scriptType = type ? toLower(strTrim(type)) : "";
		if (!scriptType.empty() && scriptType != "text/javascript" &&
			scriptType != "application/javascript")
			continue;

		const char* src = node.getAttr("src");
		if (src && *src)
		{
			const std::string url = urlJoin(pageUrl, src);
			if ((strStartsWith(url, "http://") ||
				strStartsWith(url, "https://")) && ++externalCount <= 12)
				return true;
		}
		else
		{
			const std::string source = document.textOf(
				(int)(&node - document.nodes.data()));
			if (!source.empty() && source.size() <= 128 * 1024 &&
				++inlineCount <= 32)
				return true;
		}
	}
	return false;
}

static void refreshComputedStyles(Browser::Tab& tab)
{
	if (!tab.jsBinding || (tab.jsBinding->computedDomVersion == tab.doc.mutationVersion &&
		tab.jsBinding->computedCssBytes == tab.css.stylesheetBytes() &&
		tab.jsBinding->computedStyles.size() == tab.doc.nodes.size())) return;
	JSPageBinding& binding = *tab.jsBinding;
	binding.computedStyles.clear(); binding.computedStyles.resize(tab.doc.nodes.size());
	const Style initial = defaultStyle();
	std::function<void(int, const Style&, int)> visit = [&](int index, const Style& parent, int depth)
	{
		if (depth > 64 || index < 0 || (size_t)index >= tab.doc.nodes.size()) return;
		const Node& node = tab.doc.nodes[(size_t)index];
		const std::string tag = node.tag;
		Style style = tab.css.resolve(tab.doc, index, parent);
		if (node.isElement())
		{
			auto& values = binding.computedStyles[(size_t)index];
			values["color"] = cssColorValue(style.color);
			values["backgroundColor"] = style.bgSet ? cssColorValue(style.bg) : "rgba(0, 0, 0, 0)";
			values["fontSize"] = std::to_string(style.fontSize > 0 ? style.fontSize : 16) + "px";
			values["fontWeight"] = style.bold ? "700" : "400";
			values["fontStyle"] = style.italic ? "italic" : "normal";
			values["textAlign"] = style.align == 1 ? "center" : (style.align == 2 ? "right" : "left");
			const bool heading = tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
			const bool uaBlock = tag == "html" || tag == "body" || tag == "div" || tag == "p" ||
				tag == "section" || tag == "article" || tag == "header" || tag == "footer" ||
				tag == "main" || tag == "nav" || tag == "ul" || tag == "ol" || tag == "li" ||
				heading || tag == "form" || tag == "pre";
			values["display"] = style.displayNone ? "none" :
				(style.flex ? "flex" : (style.displayInline ? "inline" :
					((style.block || uaBlock) ? "block" : "inline")));
			values["width"] = style.width > 0 ? std::to_string(style.width) + "px" : "auto";
			values["height"] = style.height > 0 ? std::to_string(style.height) + "px" : "auto";
			values["marginTop"] = std::to_string(style.marginTop) + "px";
			values["marginRight"] = style.marginRightAuto ? "auto" : std::to_string(style.marginRight) + "px";
			values["marginBottom"] = std::to_string(style.marginBottom) + "px";
			values["marginLeft"] = style.marginLeftAuto ? "auto" : std::to_string(style.marginLeft) + "px";
			values["paddingTop"] = std::to_string(style.paddingTop) + "px";
			values["paddingRight"] = std::to_string(style.paddingRight) + "px";
			values["paddingBottom"] = std::to_string(style.paddingBottom) + "px";
			values["paddingLeft"] = std::to_string(style.paddingLeft) + "px";
			values["boxSizing"] = style.borderBox ? "border-box" : "content-box";
			values["textDecoration"] = style.underline ? "underline" : "none";
			values["fontFamily"] = style.mono ? "monospace" : "sans-serif";
		}

		for (int child : node.children) visit(child, node.isElement() ? style : parent, depth + 1);
	};
	if (tab.doc.root >= 0 && (size_t)tab.doc.root < tab.doc.nodes.size())
		for (int child : tab.doc.nodes[(size_t)tab.doc.root].children) visit(child, initial, 0);
	binding.computedDomVersion = tab.doc.mutationVersion;
	binding.computedCssBytes = tab.css.stylesheetBytes();
}

static void bindBrowserPage(Browser& browser, Browser::Tab& tab, int tabIndex)
{
	refreshComputedStyles(tab);
	if (tab.jsBinding)
	{
		tab.jsBinding->fetchOwner = &browser;
		tab.jsBinding->fetchTabIndex = tabIndex;
		tab.jsBinding->enqueueFetch = enqueueFetchBridge;
	}
	jsPageBindDocument(tab.jsContext, &tab.doc, tab.url.c_str(), tab.jsBinding.get());
}

// -------------------------------------------------------------------------
// Detection URL / recherche
//
// La logique vit dans le module search/ (une seule source de verite, testee
// sur PC) : search::resolveInput choisit entre adresse, URL interne et
// recherche sur le moteur configure.
// -------------------------------------------------------------------------
bool looksLikeUrl(const std::string& input)
{
	return looksLikeAddress(strTrim(input));
}

// Contenu interpretable comme du HTML ?
static bool isHtmlBody(const std::string& contentType, const std::string& body)
{
	std::string ct = toLower(contentType);
	if (ct.find("text/html") != std::string::npos) return true;
	if (ct.find("application/xhtml") != std::string::npos) return true;
	if (ct.find("xml") != std::string::npos && ct.find("html") != std::string::npos) return true;
	if (ct.empty() || ct.find("text/plain") != std::string::npos)
	{
		// sniff du debut : ressemble a du HTML ?
		size_t n = body.size() < 16 ? body.size() : 16;
		for (size_t i = 0; i < n; i++)
			if (body[i] == '<') return true;
	}
	return false;
}

// -------------------------------------------------------------------------
Browser::Browser(NetworkManager& net, WebSettings& settings)
	: net_(net), set_(settings)
{
	pageSegmentCache_.prune();
}

Browser::~Browser()
{
	for (Tab& tab : tabs_)
		releaseJavaScript(tab);
}

bool Browser::initializeJavaScript(Tab& tab)
{
	if (!set_.enableJS || tab.jsContext) return true;
	if (!jsPageContextCreate(tab.jsRuntime, tab.jsContext)) return false;
	if (tab.jsBinding) ++tab.jsBinding->contextGeneration;
	return true;
}

bool Browser::enqueuePageFetch(JSPageBinding* binding, int tabIndex, uint32_t promiseId,
	bool xhr, const std::string& url, const std::string& method, const std::string& body)
{
	if (!binding || fetchQueue_.size() >= 16) return false;
	if (tabIndex < 0 || tabIndex >= (int)tabs_.size() ||
		tabs_[(size_t)tabIndex].jsBinding.get() != binding)
	{
		tabIndex = -1;
		for (size_t i = 0; i < tabs_.size(); ++i)
			if (tabs_[i].jsBinding.get() == binding)
			{
				tabIndex = (int)i;
				break;
			}
	}
	if (tabIndex < 0 || !tabs_[(size_t)tabIndex].jsRuntime ||
		!tabs_[(size_t)tabIndex].jsContext)
		return false;
	const size_t scheme = url.find("://"); if (scheme == std::string::npos) return false;
	const size_t end = url.find_first_of("/?#", scheme + 3);
	std::string origin = url.substr(0, end);
	for (char& ch : origin) ch = (char)tolower((unsigned char)ch);
	if (origin != binding->origin) return false; // CORS n'est pas encore implémenté.
	FetchJob job;
	job.tabIndex = tabIndex; job.promiseId = promiseId; job.generation = binding->contextGeneration; job.xhr = xhr;
	job.binding = tabs_[(size_t)tabIndex].jsBinding; job.url = url; job.method = method; job.body = body; job.referer = binding->url;
	fetchQueue_.push_back(std::move(job));
	return true;
}

void Browser::serviceFetchQueue()
{
	if (!fetchActive_)
	{
		while (!fetchQueue_.empty())
		{
			const FetchJob& queued = fetchQueue_.front();
			std::shared_ptr<JSPageBinding> binding = queued.binding.lock();
			const bool valid = binding && queued.tabIndex >= 0 &&
				queued.tabIndex < (int)tabs_.size() &&
				tabs_[(size_t)queued.tabIndex].jsBinding == binding &&
				binding->contextGeneration == queued.generation &&
				tabs_[(size_t)queued.tabIndex].jsRuntime &&
				tabs_[(size_t)queued.tabIndex].jsContext;
			if (valid) break;
			fetchQueue_.erase(fetchQueue_.begin());
		}
	}
	if (fetchActive_ || fetchQueue_.empty() || net_.busy() || net_.state() != NetState::Idle ||
		requestTab_ >= 0 || cssRequestTab_ >= 0 || scriptRequestTab_ >= 0 || imageRequestTab_ >= 0) return;
	FetchJob& job = fetchQueue_.front();
	if (!net_.start(job.url, 256 * 1024, set_.verifyTLS, job.method, job.body,
		NetRequestType::Fetch, 10000, job.referer))
	{
		NetResponse failed;
		onFetchResponse(failed); return;
	}
	fetchActive_ = true;
}

void Browser::cancelFetchQueue(const char* reason)
{
	for (FetchJob& job : fetchQueue_)
	{
		std::shared_ptr<JSPageBinding> binding = job.binding.lock();
		if (!binding || job.tabIndex < 0 || job.tabIndex >= (int)tabs_.size()) continue;
		Tab& tab = tabs_[(size_t)job.tabIndex];
		if (tab.jsBinding == binding && binding->contextGeneration == job.generation && tab.jsRuntime && tab.jsContext)
		{
			if (job.xhr) jsPageResolveXHR(tab.jsRuntime, tab.jsContext, job.promiseId, "", "", 0, "", 0, reason);
			else jsPageResolveFetch(tab.jsRuntime, tab.jsContext, job.promiseId, "", "", 0, "", 0, reason);
		}
	}
	fetchQueue_.clear(); fetchActive_ = false;
}

void Browser::onFetchResponse(NetResponse& response)
{
	if (fetchQueue_.empty()) { fetchActive_ = false; return; }
	FetchJob job = std::move(fetchQueue_.front()); fetchQueue_.erase(fetchQueue_.begin()); fetchActive_ = false;
	std::shared_ptr<JSPageBinding> binding = job.binding.lock();
	if (!binding || job.tabIndex < 0 || job.tabIndex >= (int)tabs_.size()) return;
	Tab& tab = tabs_[(size_t)job.tabIndex];
	if (tab.jsBinding != binding || binding->contextGeneration != job.generation || !tab.jsRuntime || !tab.jsContext) return;
	const char* error = response.ok ? "" : (response.error.empty() ? "Network request failed" : response.error.c_str());
	if (job.xhr)
		jsPageResolveXHR(tab.jsRuntime, tab.jsContext, job.promiseId, response.url.c_str(),
			response.body.data(), response.body.size(), response.contentType.c_str(), response.status, error);
	else
		jsPageResolveFetch(tab.jsRuntime, tab.jsContext, job.promiseId,
			response.url.c_str(), response.body.data(), response.body.size(), response.contentType.c_str(),
			response.status, error);
}

void Browser::releaseJavaScript(Tab& tab)
{
	if (tab.jsBinding)
	{
		if (fetchActive_ && !fetchQueue_.empty() &&
			fetchQueue_.front().binding.lock() == tab.jsBinding)
		{
			net_.cancel();
			fetchActive_ = false;
			fetchQueue_.erase(fetchQueue_.begin());
		}
		++tab.jsBinding->contextGeneration;
	}
	jsPageContextDestroy(tab.jsRuntime, tab.jsContext);
}

bool Browser::syncJavaScriptContexts()
{
	if (!set_.enableJS)
	{
		for (Tab& tab : tabs_) releaseJavaScript(tab);
		return true;
	}

	for (Tab& tab : tabs_)
	{
		if (tab.pageReady && tab.jsRequired && !initializeJavaScript(tab))
		{
			for (Tab& cleanup : tabs_) releaseJavaScript(cleanup);
			return false;
		}
	}
	return true;
}

void Browser::resetTab(Tab& t)
{
	releaseJavaScript(t);
	t.jsRequired = false;
	t.doc.clear();
	t.css.clear();
	t.page.clear();
	t.pageReady = false;
	t.title.clear();
	t.scrollY = 0;
	t.focusNode = -1;
	t.failedSegmentIndex = -1;
	segmentPending_ = false;
	segmentTab_ = -1;
	segmentIndexPending_ = -1;
	pendingScrollY_ = -1;
	layoutPending_ = false;
}

// -------------------------------------------------------------------------
void Browser::openInput(const std::string& input)
{
	std::string s = strTrim(input);
	if (s.empty()) return;
	std::string resolved = resolveInput(s, searchEngineName());
	if (resolved.empty()) return;
	openUrl(resolved);
}

void Browser::openSearch(const std::string& query)
{
	Tab& t = currentTab();
	resetTab(t);
	loadUrlInto(current_, searchUrl(query, searchEngineName()), true);
}

void Browser::openUrl(const std::string& url, bool record)
{
	clearStyleQueue();
	clearImageQueue();
	const std::string targetUrl = url;
	if (targetUrl.empty()) return;
	cancelTabRequest(current_);
	if (strStartsWith(toLower(targetUrl), "nw://"))
	{
		if (toLower(targetUrl) == "nw://home" || toLower(targetUrl) == "nw:/home") { showInternalHome(); return; }
		showInternal(targetUrl);
		return;
	}
	Tab& t = currentTab();
	std::string prevUrl = t.url;
	resetTab(t);
	loadUrlInto(current_, targetUrl, record, "GET", "", prevUrl);
}

void Browser::loadUrlInto(int tabIdx, const std::string& url, bool record, const std::string& method, const std::string& postData, const std::string& referer, const std::string& csrfToken)
{
	clearStyleQueue();
	clearImageQueue();
	clearScriptQueue();
	cancelTabRequest(tabIdx);
	responsePending_ = false;
	layoutPending_ = false;
	segmentPending_ = false;
	segmentTab_ = -1;
	segmentIndexPending_ = -1;
	pendingScrollY_ = -1;
	pendingResponse_ = NetResponse();
	pendingImages_.clear();
	progressStatus_ = "Connexion au site...";
	diagStage("fetch");
	Tab& t = tabs_[tabIdx];
	if (strStartsWith(toLower(url), "nw://"))
	{
		if (toLower(url) == "nw://home" || toLower(url) == "nw:/home")
		{
			t.url = url;
			t.pageReady = false;
			renderInternal(tabIdx, internalHomeHtml(searchEngineName()));
			return;
		}
		showInternal(url);
		return;
	}
	t.url = url;
	t.pageReady = false;
	if (record)
	{
		if (t.back.empty() || t.back.back() != url)
			t.back.push_back(url);
		t.fwd.clear();
	}
	requestTab_ = tabIdx;
	if (!net_.start(url, (size_t)set_.maxHtmlBytes, set_.verifyTLS, method, postData, NetRequestType::Page, 30000, referer, csrfToken))
	{
		pendingResponse_.url = url;
		pendingResponse_.error = "Impossible de demarrer la requete reseau.";
		responsePending_ = true;
		layoutPending_ = true;
	}
}

void Browser::reload()
{
	clearStyleQueue();
	clearImageQueue();
	clearScriptQueue();
	responsePending_ = false;
	layoutPending_ = false;
	pendingResponse_ = NetResponse();
	progressStatus_ = "Connexion au site...";
	Tab& t = currentTab();
	if (t.url.empty()) { goHome(); return; }
	t.pageReady = false;
	diagStage("fetch");
	cancelTabRequest(current_);
	requestTab_ = current_;
	if (!net_.start(t.url, (size_t)set_.maxHtmlBytes, set_.verifyTLS))
	{
		pendingResponse_.url = t.url;
		pendingResponse_.error = "Impossible de demarrer la requete reseau.";
		responsePending_ = true;
		layoutPending_ = true;
	}
}

void Browser::stop()
{
	clearStyleQueue();
	clearImageQueue();
	clearScriptQueue();
	net_.cancel();
	cancelFetchQueue("fetch aborted");
	requestTab_ = -1;
	responsePending_ = false;
	layoutPending_ = false;
	segmentPending_ = false;
	segmentTab_ = -1;
	segmentIndexPending_ = -1;
	pendingScrollY_ = -1;
	progressStatus_.clear();
	pendingResponse_ = NetResponse();
}

void Browser::cancelTabRequest(int tabIdx)
{
	if (requestTab_ != tabIdx) return;
	net_.cancel();
	requestTab_ = -1;
	responsePending_ = false;
	layoutPending_ = false;
	pendingResponse_ = NetResponse();
}

void Browser::back()
{
	Tab& t = currentTab();
	if (t.back.size() <= 1) return;
	t.fwd.push_back(t.back.back());
	t.back.pop_back();
	std::string to = t.back.back();
	resetTab(t);
	loadUrlInto(current_, to, false);
}

void Browser::forward()
{
	Tab& t = currentTab();
	if (t.fwd.empty()) return;
	std::string to = t.fwd.back();
	t.fwd.pop_back();
	t.back.push_back(to);
	resetTab(t);
	loadUrlInto(current_, to, false);
}

void Browser::goHome()
{
	if (tabs_.empty())
	{
		tabs_.push_back(Tab());
		current_ = (int)tabs_.size() - 1;
	}
	if (!set_.homePage.empty())
	{
		openUrl(set_.homePage);
		return;
	}
	showInternalHome();
}

void Browser::showInternalHome()
{
	clearStyleQueue();
	clearImageQueue();
	cancelTabRequest(current_);
	pendingImages_.clear();
	responsePending_ = false;
	layoutPending_ = false;
	pendingResponse_ = NetResponse();
	progressStatus_.clear();
	Tab& t = currentTab();
	resetTab(t);
	t.url = "nw://home";
	t.isSearchPage = false;
	renderInternal(current_, internalHomeHtml(searchEngineName()));
}

// Construit le HTML de la page d'accueil interne (sans reseau).
//
// Mise en page "telephone" : grande barre de recherche, raccourcis en tuiles,
// puis favoris. Tout est en CSS simple (blocs, marges, couleurs) car le moteur
// de rendu ne gere ni flexbox ni grille.
std::string Browser::internalHomeHtml(const std::string& engine)
{
	// Raccourcis proposes en tuiles (touches de l'ecran bas pour y acceder).
	static const struct { const char* label; const char* url; } kShortcuts[] =
	{
		{ "DuckDuckGo", "https://lite.duckduckgo.com/lite/" },
		{ "Wikipedia",  "https://fr.m.wikipedia.org/" },
		{ "Actualites", "https://fr.m.wikipedia.org/wiki/Portail:Actualites" },
		{ "Meteo",      "https://m.wetterdienst.de/" },
		{ "YouTube",    "https://m.youtube.com/" },
		{ "GitHub",     "https://github.com/" },
		{ "Jeux 3DS",   "https://www.gamebrew.org/wiki/List_of_3DS_homebrew_applications" },
		{ "Reglages",   "nw://settings" },
	};

	std::string h;
	h = "<html><head><title>Pixelnaute</title></head><body "
		"style=\"background: #17131A; color: #ECE8E8\">";

	// --- en-tete ---
	h += "<div style=\"text-align: center; margin-top: 26px; margin-bottom: 4px\">"
	     "<span style=\"font-size: 40px; font-weight: bold; color: #F2A25C\">Pixelnaute</span></div>";
	h += "<div style=\"text-align: center; font-size: 13px; color: #9A8A8A; "
	     "margin-bottom: 18px\">Navigateur autonome &middot; New Nintendo 3DS</div>";


	// --- barre de recherche ---
	h += "<form action=\"nw://search\">";
	h += "<div style=\"background: #2A2230; margin-left: 10px; margin-right: 10px; "
	     "padding: 8px\">";
	h += "<input type=\"search\" name=\"q\" size=\"40\" value=\"\" "
	     "style=\"font-size: 19px\">";
	h += "</div>";
	h += "<div style=\"text-align: center; margin-top: 10px\">"
	     "<button type=\"submit\" value=\"Rechercher\">Rechercher</button></div>";
	h += "</form>";

	h += "<div style=\"text-align: center; font-size: 12px; color: #7A6E70; "
	     "margin-top: 6px; margin-bottom: 16px\">"
	     "Moteur : " + std::string(searchEngineDisplayName(engine)) +
	     " &middot; touchez le champ pour taper</div>";

	// --- raccourcis ---
	h += "<div style=\"background: #241D28; padding: 8px; margin-left: 6px; "
	     "margin-right: 6px\">";
	h += "<div style=\"font-size: 13px; font-weight: bold; color: #F2A25C; "
	     "margin-bottom: 4px\">RACCOURCIS</div>";
	for (const auto& s : kShortcuts)
	{
		h += "<a href=\"" + htmlEscape(s.url) + "\" style=\"display: block; "
		     "background: #322839; padding: 9px; margin-bottom: 5px; "
		     "font-size: 17px; color: #ECE8E8\">" +
		     htmlEscape(s.label) + "</a>";
	}
	h += "</div>";

	// --- favoris ---
	h += "<div style=\"background: #241D28; padding: 8px; margin: 10px 6px 6px 6px\">";
	h += "<div style=\"font-size: 13px; font-weight: bold; color: #F2A25C; "
	     "margin-bottom: 4px\">FAVORIS</div>";
	if (bookmarks_.entries.empty())
		h += "<div style=\"font-size: 14px; color: #9A8A8A\">"
		     "Aucun favori. Dans le menu (START), choisissez "
		     "&laquo; Ajouter aux favoris &raquo;.</div>";
	else
	{
		size_t shown = 0;
		for (const auto& e : bookmarks_.entries)
		{
			if (shown++ >= 12) break;
			const std::string label = e.title.empty() ? e.url : e.title;
			h += "<a href=\"" + htmlEscape(e.url) + "\" style=\"display: block; "
			     "background: #322839; padding: 8px; margin-bottom: 5px; "
			     "font-size: 16px; color: #ECE8E8\">" +
			     htmlEscape(label) + "</a>";
		}
	}
	h += "</div>";

	// --- outils internes ---
	h += "<div style=\"text-align: center; margin: 14px 6px 24px 6px\">";
	h += "<a href=\"nw://bookmarks\" style=\"color: #F2A25C; font-size: 15px\">Favoris</a>";
	h += "<span style=\"color: #4A4048\"> &middot; </span>";
	h += "<a href=\"nw://history\" style=\"color: #F2A25C; font-size: 15px\">Historique</a>";
	h += "<span style=\"color: #4A4048\"> &middot; </span>";
	h += "<a href=\"nw://settings\" style=\"color: #F2A25C; font-size: 15px\">Reglages</a>";
	h += "<span style=\"color: #4A4048\"> &middot; </span>";
	h += "<a href=\"nw://about\" style=\"color: #F2A25C; font-size: 15px\">A propos</a>";
	h += "</div>";

	h += "</body></html>";
	return h;
}

// Rendu d'une page locale (sans passer par le reseau).
void Browser::renderInternal(int tabIdx, const std::string& html)
{
	Tab& tab = tabs_[tabIdx];
	Document d;
	if (!htmlParse(html, d))
	{
		tab.pageReady = false;
		return;
	}
	if (d.nodes.empty() && tab.doc.empty()) { tab.pageReady = false; return; }
	tab.css.clear();
	collectStyleBlocks(d, tab.css);
	renderInto(tabIdx, std::move(d));
}

// Intercepte les URL internes "nw://..." (menu / actions).
bool Browser::showInternal(const std::string& url)
{
	std::string u = toLower(url);
	if (u.rfind("nw://search", 0) == 0)
	{
		// nw://search?q=requete  (le formulaire de la page d'accueil)
		size_t q = url.find("q=");
		if (q == std::string::npos) return false;
		std::string v = url.substr(q + 2);
		// le formulaire peut avoir ajoute d'autres champs
		size_t amp = v.find('&');
		if (amp != std::string::npos) v = v.substr(0, amp);
		v = urlDecode(v);
		if (v.empty()) return false;
		openSearch(v);
		return true;
	}
	if (u.rfind("nw://", 0) == 0)
	{
		internalNav_ = url;   // l'interface bascule son mode
		return true;
	}
	return false;
}

std::string Browser::takeInternalNav()
{
	std::string r = internalNav_;
	internalNav_.clear();
	return r;
}

void Browser::openBookmark(const std::string& url)
{
	openUrl(url);
}

// -------------------------------------------------------------------------
void Browser::newTab(const std::string& url)
{
	if ((int)tabs_.size() >= NW_MAX_TABS) return;
	tabs_.push_back(Tab());
	current_ = (int)tabs_.size() - 1;
	if (!url.empty())
		openInput(url);
	else goHome();
}

void Browser::closeTab(int index)
{
	if (index < 0 || index >= (int)tabs_.size()) return;
	if ((int)tabs_.size() <= 1) return;
	clearStyleQueue();
	if (imageRequestTab_ == index || pendingImagesTab_ == index) clearImageQueue();
	else
	{
		if (imageRequestTab_ > index) --imageRequestTab_;
		if (pendingImagesTab_ > index) --pendingImagesTab_;
	}
	if (requestTab_ == index)
	{
		net_.cancel();
		requestTab_ = -1;
		responsePending_ = false;
		layoutPending_ = false;
		pendingResponse_ = NetResponse();
	}
	else if (requestTab_ > index)
		--requestTab_;
	releaseJavaScript(tabs_[index]);
	for (size_t i = 0; i < fetchQueue_.size(); )
	{
		if (fetchQueue_[i].tabIndex == index)
		{
			if (fetchActive_ && i == 0) { fetchQueue_[i].tabIndex = -1; ++i; }
			else fetchQueue_.erase(fetchQueue_.begin() + i);
		}
		else { if (fetchQueue_[i].tabIndex > index) --fetchQueue_[i].tabIndex; ++i; }
	}
	tabs_.erase(tabs_.begin() + index);
	if (current_ >= (int)tabs_.size()) current_ = (int)tabs_.size() - 1;
}

void Browser::selectTab(int index)
{
	if (index >= 0 && index < (int)tabs_.size() && current_ != index)
	{
		clearStyleQueue();
		clearImageQueue();
		clearScriptQueue();
		current_ = index;
	}
}

void Browser::nextTab()
{
	if (tabs_.empty()) return;
	clearStyleQueue();
	clearImageQueue();
	clearScriptQueue();
	current_ = (current_ + 1) % (int)tabs_.size();
}

void Browser::prevTab()
{
	if (tabs_.empty()) return;
	clearStyleQueue();
	clearImageQueue();
	clearScriptQueue();
	current_ = (current_ == 0) ? (int)tabs_.size() - 1 : current_ - 1;
}

Browser::Tab& Browser::currentTab()
{
	static Tab empty;
	return tabs_.empty() ? empty : tabs_[current_];
}

bool Browser::pageCacheUsage(uint64_t& bytes) const
{
	return pageSegmentCache_.usage(bytes);
}

bool Browser::listCachedPages(std::vector<PageSegmentCache::SiteEntry>& entries) const
{
	return pageSegmentCache_.listSites(entries);
}

bool Browser::clearCachedPage(uint64_t pageKey)
{
	return pageSegmentCache_.clearSite(pageKey);
}

bool Browser::clearCachedSite(const std::vector<uint64_t>& pageKeys)
{
	bool ok = true;
	for (uint64_t key : pageKeys)
		if (!pageSegmentCache_.clearSite(key)) ok = false;
	return ok;
}

bool Browser::clearPageCache()
{
	return pageSegmentCache_.clear();
}

// -------------------------------------------------------------------------
void Browser::pollNetwork()
{
	if (responsePending_) return;

	if (net_.state() == NetState::Done)
	{
		if (net_.takeResult(pendingResponse_))
		{
			if (pendingResponse_.requestType == NetRequestType::Fetch)
			{
				onFetchResponse(pendingResponse_);
				pendingResponse_ = NetResponse();
				return;
			}
			if (pendingResponse_.requestType == NetRequestType::Stylesheet)
			{
				onStylesheetResponse(pendingResponse_);
				pendingResponse_ = NetResponse();
				return;
			}
			if (pendingResponse_.requestType == NetRequestType::Script)
			{
				onScriptResponse(pendingResponse_);
				pendingResponse_ = NetResponse();
				return;
			}
			if (pendingResponse_.requestType == NetRequestType::Image)
			{
				const int tabIdx = imageRequestTab_;
				if (tabIdx >= 0 && tabIdx == current_ && tabIdx < (int)tabs_.size() &&
					tabs_[tabIdx].url == imageRequestReferer_ && pendingResponse_.ok)
				{
					if (images_.storeDownloaded(imageRequestUrl_, pendingResponse_.body))
						imageRedrawPending_ = true;
				}
				imageRequestTab_ = -1;
				imageRequestUrl_.clear();
				imageRequestReferer_.clear();
				pendingResponse_ = NetResponse();
				return;
			}
			responsePending_ = true;
			layoutPending_ = true;
			progressStatus_ = "Réponse reçue — analyse de la page...";
			diagStage("response ready");
		}
	}
}

bool Browser::tickPageResponse()
{
	if (!responsePending_) return false;
	responsePending_ = false;
	layoutPending_ = false;
	progressStatus_ = "Analyse HTML et styles...";
	onResponse(pendingResponse_);
	pendingResponse_ = NetResponse();
	return true;
}

bool Browser::stylesPending() const
{
	return cssRequestTab_ == current_ ||
		(cssQueueTab_ == current_ && !cssQueue_.empty());
}

bool Browser::scriptsPending() const
{
	return scriptRequestTab_ == current_ ||
		(scriptQueueTab_ == current_ && !scriptQueue_.empty());
}

bool Browser::tickScripts()
{
	bool redraw = scriptRedrawPending_;
	scriptRedrawPending_ = false;
	if (current_ >= 0 && current_ < (int)tabs_.size())
	{
		Tab& activeTab = tabs_[(size_t)current_];
		if (activeTab.jsRuntime && activeTab.jsContext)
		{
			bindBrowserPage(*this, activeTab, current_);
			jsPageTick(activeTab.jsRuntime, activeTab.jsContext);
		}
		if (activeTab.jsBinding && activeTab.jsBinding->navigationMode != 0)
		{
			const int mode = activeTab.jsBinding->navigationMode;
			const std::string target = activeTab.jsBinding->pendingNavigation;
			activeTab.jsBinding->navigationMode = 0;
			activeTab.jsBinding->pendingNavigation.clear();
			if (mode == 3) reload();
			else if (mode == 4)
			{
				const int delta = activeTab.jsBinding->historyDelta;
				if (delta == 0) reload();
				else if (delta < 0) for (int i = 0; i < -delta; ++i) back();
				else for (int i = 0; i < delta; ++i) forward();
			}
			else openUrl(target, mode == 1);
			return true;
		}
		// Keep the current bitmap while scripts run; paint their DOM changes once
		// the ordered script queue and load events have completed.
		if (activeTab.pageReady &&
			activeTab.renderedDomVersion != activeTab.doc.mutationVersion &&
			scriptQueueTab_ != current_)
		{
			const int savedScroll = activeTab.scrollY;
			rerenderTab(current_);
			activeTab.scrollY = savedScroll;
			redraw = true;
		}
	}
	serviceFetchQueue();
	if (scriptQueueTab_ != current_ || scriptRequestTab_ >= 0) return redraw;
	int inlineBudget = 2; // garder la boucle de rendu fluide
	while (!scriptQueue_.empty() && inlineBudget > 0)
	{
		ScriptRequest& next = scriptQueue_.front();
		if (next.external)
		{
			if (net_.busy() || net_.state() != NetState::Idle) return redraw;
			ScriptRequest request = std::move(next);
			scriptQueue_.erase(scriptQueue_.begin());
			if (!net_.start(request.url, 256 * 1024, set_.verifyTLS, "GET", "",
				NetRequestType::Script, 8000, request.referer))
				continue;
			scriptRequestTab_ = current_;
			scriptRequestUrl_ = request.url;
			return redraw;
		}
		ScriptRequest request = std::move(next);
		scriptQueue_.erase(scriptQueue_.begin());
		--inlineBudget;
		Tab& tab = tabs_[(size_t)current_];
		if (tab.jsRuntime && tab.jsContext && !request.source.empty())
		{
			bindBrowserPage(*this, tab, current_);
			const bool ok = jsPageEval(tab.jsRuntime, tab.jsContext, request.source.c_str(), request.source.size(), "<inline-script>");
			if (!ok) diagBootNote("js_inline_script_failed", 1);
			if (tab.renderedDomVersion != tab.doc.mutationVersion)
				redraw = true;
		}
	}
	if (scriptQueue_.empty())
	{
		const int readyTab = scriptQueueTab_;
		if (readyTab >= 0 && readyTab < (int)tabs_.size() &&
			tabs_[(size_t)readyTab].jsContext)
		{
			Tab& tab = tabs_[(size_t)readyTab];
			bindBrowserPage(*this, tab, readyTab);
			jsPageDispatchEvent(tab.jsContext, "DOMContentLoaded", "document");
			jsPageDispatchEvent(tab.jsContext, "load", "window");
			if (tab.pageReady &&
				tab.renderedDomVersion != tab.doc.mutationVersion)
			{
				const int savedScroll = tab.scrollY;
				rerenderTab(readyTab);
				tab.scrollY = savedScroll;
				redraw = true;
			}
		}
		scriptQueueTab_ = -1;
	}
	serviceFetchQueue();
	return redraw;
}

void Browser::clearScriptQueue()
{
	if (scriptRequestTab_ >= 0) net_.cancel();
	scriptQueue_.clear();
	scriptQueueTab_ = -1;
	scriptRequestTab_ = -1;
	scriptRequestUrl_.clear();
	scriptPageUrl_.clear();
	scriptRedrawPending_ = false;
}

void Browser::queueScripts(int tabIdx, const Document& doc, const std::string& pageUrl)
{
	scriptQueue_.clear();
	scriptQueueTab_ = tabIdx;
	scriptPageUrl_ = pageUrl;
	for (const Node& node : doc.nodes)
	{
		if (!node.isElement() || node.tag != "script") continue;
		const char* src = node.getAttr("src");
		const char* type = node.getAttr("type");
		const std::string scriptType = type ? toLower(strTrim(type)) : "";
		if (!scriptType.empty() && scriptType != "text/javascript" &&
			scriptType != "application/javascript") continue;
		if (src && *src)
		{
			const std::string url = urlJoin(pageUrl, src);
			if (!strStartsWith(url, "http://") && !strStartsWith(url, "https://")) continue;
			if (scriptQueue_.size() >= 12) break;
			scriptQueue_.push_back({ url, pageUrl, std::string(), true });
		}
		else
		{
			const std::string source = doc.textOf((int)(&node - doc.nodes.data()));
			if (source.empty() || source.size() > 128 * 1024) continue;
			if (scriptQueue_.size() >= 32) break;
			scriptQueue_.push_back({ std::string(), pageUrl, source, false });
		}
	}
}

void Browser::onScriptResponse(NetResponse& resp)
{
	const int tabIdx = scriptRequestTab_;
	scriptRequestTab_ = -1;
	const std::string requestedUrl = scriptRequestUrl_;
	scriptRequestUrl_.clear();
	if (tabIdx < 0 || tabIdx >= (int)tabs_.size()) return;
	Tab& tab = tabs_[(size_t)tabIdx];
	if (tabIdx != current_ || tab.url != scriptPageUrl_) { clearScriptQueue(); return; }
	if (resp.ok && !resp.body.empty() && resp.body.size() <= 256 * 1024 &&
		tab.jsRuntime && tab.jsContext)
	{
		bindBrowserPage(*this, tab, current_);
		if (!jsPageEval(tab.jsRuntime, tab.jsContext, resp.body.c_str(), resp.body.size(), requestedUrl.c_str()))
			diagBootNote("js_external_script_failed", 1);
		scriptRedrawPending_ = scriptRedrawPending_ ||
			tab.renderedDomVersion != tab.doc.mutationVersion;
	}
}

bool Browser::tickStyles()
{
	const bool redraw = styleRedrawPending_;
	styleRedrawPending_ = false;
	// Attendre la fin de la file : refaire le layout entre les feuilles
	// applique la CSS en plusieurs etapes et peut bloquer l'interface plusieurs
	// fois pour une meme page. Un seul rendu final incorpore toutes les regles.
	if (cssLayoutPending_ && cssRequestTab_ < 0 && cssQueue_.empty())
	{
		const int tabIdx = cssLayoutTab_;
		const std::string url = cssLayoutUrl_;
		cssLayoutPending_ = false;
		cssLayoutTab_ = -1;
		cssLayoutUrl_.clear();
		layoutPending_ = false;
		if (tabIdx >= 0 && tabIdx == current_ && tabIdx < (int)tabs_.size() && tabs_[tabIdx].url == url)
		{
			const int savedScrollY = tabs_[tabIdx].scrollY;
			rerenderTab(tabIdx);
			tabs_[tabIdx].scrollY = savedScrollY;
			styleRedrawPending_ = true;
		}
	}
	if (cssQueueTab_ != current_ || cssQueue_.empty() || cssRequestTab_ >= 0 ||
		net_.busy() || net_.state() != NetState::Idle)
		return redraw || styleRedrawPending_;

	CssRequest request = std::move(cssQueue_.front());
	cssQueue_.erase(cssQueue_.begin());
	if (!net_.start(request.url, 64 * 1024, set_.verifyTLS, "GET", "",
		NetRequestType::Stylesheet, 5000, request.referer))
	{
		if (cssQueue_.empty()) cssQueueTab_ = -1;
		return redraw;
	}
	cssRequestOrder_ = request.sourceOrder;
	cssRequestTab_ = current_;
	return redraw;
}

void Browser::clearStyleQueue()
{
	if (cssRequestTab_ >= 0) net_.cancel();
	cssQueue_.clear();
	cssQueueTab_ = -1;
	cssRequestTab_ = -1;
	cssRequestOrder_ = -1;
	cssPageUrl_.clear();
	styleRedrawPending_ = false;
	if (cssLayoutPending_) layoutPending_ = false;
	cssLayoutPending_ = false;
	cssLayoutTab_ = -1;
	cssLayoutUrl_.clear();
}

void Browser::queueStyles(int tabIdx, const Document& doc, const std::string& pageUrl)
{
	cssQueue_.clear();
	cssQueueTab_ = tabIdx;
	cssPageUrl_ = pageUrl;
	cssStartMs_ = timeMs();
	for (size_t i = 0; i < doc.nodes.size(); ++i)
	{
		const Node& node = doc.nodes[i];
		if (!node.isElement() || node.tag != "link") continue;
		const char* rel = node.getAttr("rel");
		const char* href = node.getAttr("href");
		const char* media = node.getAttr("media");
		if (!rel || !href || !*href || toLower(rel).find("stylesheet") == std::string::npos) continue;
		if (media)
		{
			const std::string mediaTypes = toLower(media);
			if (mediaTypes.find("print") != std::string::npos &&
				mediaTypes.find("screen") == std::string::npos) continue;
		}
		std::string url = urlJoin(pageUrl, href);
		if (!strStartsWith(url, "http://") && !strStartsWith(url, "https://")) continue;
		CssRequest request;
		request.url = std::move(url);
		request.referer = pageUrl;
		request.sourceOrder = (int)i;
		cssQueue_.push_back(std::move(request));
	}
	if (cssQueue_.empty()) cssQueueTab_ = -1;
}

void Browser::onStylesheetResponse(NetResponse& resp)
{
	const int tabIdx = cssRequestTab_;
	cssRequestTab_ = -1;
	if (tabIdx < 0 || tabIdx >= (int)tabs_.size()) return;
	Tab& tab = tabs_[tabIdx];
	if (tabIdx != current_ || tab.url != cssPageUrl_) { clearStyleQueue(); return; }
	const size_t oldRuleCount = tab.css.rules().size();
	if (resp.ok && !resp.body.empty()) tab.css.parseStylesheet(resp.body, cssRequestOrder_);
	if (tab.css.rules().size() != oldRuleCount)
	{
		cssLayoutPending_ = true;
		cssLayoutTab_ = tabIdx;
		cssLayoutUrl_ = tab.url;
		layoutPending_ = true;
	}
	if (cssQueue_.empty())
	{
		cssQueueTab_ = -1;
		diagBootNote("css_external_ms", (long)(timeMs() - cssStartMs_));
	}
}

bool Browser::tickImages()
{
	bool redraw = imageRedrawPending_;
	imageRedrawPending_ = false;
	if (!set_.loadImages)
	{
		clearImageQueue();
		return redraw;
	}
	if (pendingImagesTab_ != current_ || imageRequestTab_ >= 0 || pendingImages_.empty() || net_.busy() ||
		net_.state() != NetState::Idle) return redraw;
	const std::string url = pendingImages_.back();
	pendingImages_.pop_back();
	if (net_.start(url, 512 * 1024, set_.verifyTLS, "GET", "",
		NetRequestType::Image, 6000, pendingImagesReferer_))
	{
		imageRequestTab_ = current_;
		imageRequestUrl_ = url;
		imageRequestReferer_ = pendingImagesReferer_;
		diagStage("image fetch");
	}
	return redraw;
}

void Browser::clearImageQueue()
{
	if (imageRequestTab_ >= 0) net_.cancel();
	imageRequestTab_ = -1;
	imageRequestUrl_.clear();
	imageRequestReferer_.clear();
	pendingImages_.clear();
	pendingImagesReferer_.clear();
	pendingImagesTab_ = -1;
}

// -------------------------------------------------------------------------
// Traitement d'une reponse reseau.
// -------------------------------------------------------------------------
void Browser::onResponse(NetResponse& resp)
{
	progressStatus_ = "Analyse du document...";
	diagStage("response processing");
	diagBootNote("page_http_status", resp.status);
	diagBootNote("page_response_bytes", (long)resp.body.size());
	diagBootNote("heap_size", (long)envGetHeapSize());
	diagBootNote("linear_heap_size", (long)envGetLinearHeapSize());
	diagBootNote("app_mem_free_response",
		(long)osGetMemRegionFree(MEMREGION_APPLICATION));
	diagBootNote("linear_mem_free_response", (long)linearSpaceFree());
	const int tabIdx = requestTab_;
	requestTab_ = -1;
	if (tabIdx < 0 || tabIdx >= (int)tabs_.size()) return;
	Tab& t = tabs_[tabIdx];

	const bool isChallengeCandidate = (resp.status == 202 || resp.status == 403 || resp.status == 429 || resp.status == 503 || resp.body.size() < 30 * 1024);
	if (resp.ok && isChallengeCandidate && searchPageIsVerificationChallenge(resp.body))
	{
		resp.ok = false;
		resp.error = "Le site demande une verification anti-robot (Cloudflare / captcha), non prise en charge sans JavaScript.";
	}

	if (!resp.ok)
	{
		resetTab(t);
		t.pageReady = true;

		Document d;
		std::string h = "<html><body style=\"background: #17131A; color: #ECE8E8; padding: 12px\">"
		                "<h2 style=\"color: #F2A25C; margin-bottom: 8px\">Impossible d'ouvrir la page</h2>";

		if (resp.sslError)
		{
			h += "<p style=\"margin-bottom: 6px\">Connexion securisee impossible.</p>"
			     "<p style=\"color: #B0A4A6; margin-bottom: 16px\">Ce site utilise un certificat ou une version TLS "
			     "incompatible avec le composant reseau de la New 3DS. La verification du certificat n'a pas "
			     "ete contournee automatiquement. Verifiez la date de la console ou modifiez le reglage TLS "
			     "dans les parametres, en connaissance des risques.</p>";
		}
		else if (resp.tooBig)
		{
			h += "<p style=\"color: #B0A4A6; margin-bottom: 16px\">Cette page est trop lourde pour la memoire de la New 3DS. "
			     "Elle ne peut pas etre affichee.</p>";
		}
		else
		{
			std::string msg = resp.error.empty() ? "Erreur reseau inconnue." : resp.error;
			h += "<p style=\"color: #B0A4A6; margin-bottom: 16px\">" + htmlEscape(msg) + "</p>";
		}

		h += "<p><a href=\"" + htmlEscape(t.url) + "\" style=\"color: #F2A25C\">[ Reessayer ]</a></p>"
		     "</body></html>";
		htmlParse(h, d);
		renderInto(tabIdx, std::move(d));
		return;
	}

	if (!isHtmlBody(resp.contentType, resp.body))
	{
		const std::string ct = toLower(resp.contentType);
		const std::string lowerUrl = toLower(resp.url);
		if (ct.find("text/plain") != std::string::npos ||
			ct.find("application/json") != std::string::npos ||
			ct.find("text/css") != std::string::npos ||
			ct.find("text/javascript") != std::string::npos ||
			ct.find("application/javascript") != std::string::npos ||
			ct.find("text/markdown") != std::string::npos ||
			ct.find("xml") != std::string::npos)
		{
			std::string wrapped = "<html><body style=\"background: #17131A; color: #ECE8E8; padding: 8px\">"
								  "<pre>" + htmlEscape(resp.body) + "</pre></body></html>";
			resp.body = std::move(wrapped);
		}
		else if (ct.find("image/") != std::string::npos ||
				 strEndsWith(lowerUrl, ".png") ||
				 strEndsWith(lowerUrl, ".jpg") ||
				 strEndsWith(lowerUrl, ".jpeg") ||
				 strEndsWith(lowerUrl, ".gif") ||
				 strEndsWith(lowerUrl, ".bmp"))
		{
			std::string wrapped = "<html><body style=\"background: #17131A; margin: 0; text-align: center\">"
								  "<img src=\"" + htmlEscape(resp.url) + "\"></body></html>";
			resp.body = std::move(wrapped);
		}
		else
		{
			// Telechargement direct (image, pdf, zip...)
			std::string fn = "fichier";
			size_t p = resp.url.find_last_of('/');
			if (p != std::string::npos && p + 1 < resp.url.size())
			{
				std::string cand = resp.url.substr(p + 1);
				if (!cand.empty()) fn = cand;
			}
			std::string path;
			if (downloadsSave(resp.body, fn, path))
			{
				const size_t downloadedBytes = resp.body.size();
				std::string().swap(resp.body);
				Document d;
				std::string h = "<html><body><h3>Telechargement termine</h3><p>" +
					htmlEscape(fn) + "</p><p>Enregistre :<br/>" + htmlEscape(path) +
					"</p><p>" + itos((long)(downloadedBytes / 1024)) + " Ko</p></body></html>";
				htmlParse(h, d);
				renderInto(tabIdx, std::move(d));
			}
			else
			{
				Document d;
				std::string h = "<html><body><h3>Echec du telechargement</h3></body></html>";
				htmlParse(h, d);
				renderInto(tabIdx, std::move(d));
			}
			return;
		}
	}

	// Page de resultats d'un moteur "sans JavaScript" (DuckDuckGo Lite, ...) :
	// ses resultats sont dans des tableaux que notre moteur de mise en page ne
	// sait pas presenter. On la reecrit en blocs simples, un par resultat, et
	// on affiche cette version a la place.
	std::string rewritten;
	diagStage("parse html");
	if (searchPageRewrite(resp.body, rewritten, t.url))
	{
		Document dr;
		if (htmlParse(rewritten, dr))
		{
			std::string().swap(resp.body);
			t.url = resp.url;
			t.scrollY = 0;
			t.css.clear();
			diagStage("css parse");
			collectStyleBlocks(dr, t.css);
			diagStage("css ready");
			renderInto(tabIdx, std::move(dr));
			history().add(t.url, t.title);
			return;
		}
	}
	std::string().swap(rewritten);

	Document d;
	diagStage("html parse start");
	diagBootNote("app_mem_free_before_parse",
		(long)osGetMemRegionFree(MEMREGION_APPLICATION));
	diagBootNote("linear_mem_free_before_parse", (long)linearSpaceFree());
	const bool parsed = htmlParse(resp.body, d);
	diagBootNote("page_parse_ok", parsed ? 1 : 0);
	diagBootNote("page_dom_nodes", (long)d.nodes.size());
	diagBootNote("app_mem_free_after_parse",
		(long)osGetMemRegionFree(MEMREGION_APPLICATION));
	diagBootNote("linear_mem_free_after_parse", (long)linearSpaceFree());
	std::string().swap(resp.body);
	if (!parsed)
	{
		Document d2;
		std::string h = "<html><body><h3>Page non affichee</h3><p>Le contenu est invalide ou depasse la limite de complexite de la New 3DS.</p></body></html>";
		htmlParse(h, d2);
		renderInto(tabIdx, std::move(d2));
		return;
	}

	// Suivre <meta http-equiv="refresh" content="...">
	for (size_t i = 0; i < d.nodes.size(); i++)
	{
		const Node& nd = d.nodes[i];
		if (nd.isElement() && nd.tag == "meta")
		{
			const char* he = nd.getAttr("http-equiv");
			const char* cnt = nd.getAttr("content");
			if (he && cnt && toLower(he) == "refresh")
			{
				std::string cstr = cnt;
				size_t uPos = toLower(cstr).find("url=");
				if (uPos != std::string::npos)
				{
					std::string refreshUrl = strTrim(cstr.substr(uPos + 4));
					if (refreshUrl.size() >= 2 && ((refreshUrl.front() == '"' && refreshUrl.back() == '"') ||
					                              (refreshUrl.front() == '\'' && refreshUrl.back() == '\'')))
						refreshUrl = refreshUrl.substr(1, refreshUrl.size() - 2);
					refreshUrl = urlJoin(resp.url, refreshUrl);
					if (refreshUrl != resp.url && (strStartsWith(refreshUrl, "http://") || strStartsWith(refreshUrl, "https://")))
					{
						openUrl(refreshUrl, false);
						return;
					}
				}
			}
		}
	}

	t.url = resp.url;
	t.scrollY = 0;

	// feuilles de style : blocs <style>
	diagStage("css parse");
	diagBootNote("app_mem_free_before_css",
		(long)osGetMemRegionFree(MEMREGION_APPLICATION));
	size_t cssBlockCount = 0;
	size_t cssInputBytes = 0;
	for (const Node& node : d.nodes)
	{
		if (!node.isElement() || node.tag != "style") continue;
		++cssBlockCount;
		for (int child : node.children)
		{
			if (child < 0 || (size_t)child >= d.nodes.size()) continue;
			const Node& text = d.nodes[child];
			if (text.type == NodeType::RawText || text.type == NodeType::Text)
				cssInputBytes += text.text.size();
		}
	}
	diagBootNote("page_css_blocks", (long)cssBlockCount);
	diagBootNote("page_css_input_bytes", (long)cssInputBytes);
	t.css.clear();
	collectStyleBlocks(d, t.css);

	// Les feuilles externes sont chargees en arriere-plan apres le premier rendu.
	if (tabIdx == current_) queueStyles(tabIdx, d, resp.url);

	diagBootNote("page_css_bytes", (long)t.css.stylesheetBytes());
	diagBootNote("page_css_rules", (long)t.css.rules().size());
	diagBootNote("app_mem_free_after_css",
		(long)osGetMemRegionFree(MEMREGION_APPLICATION));
	diagStage("css ready");

	renderInto(tabIdx, std::move(d));
	history().add(t.url, t.title);
}

static std::string extractReaderHtml(const Document& doc, const std::string& title)
{
	std::string h = "<html><head><title>" + htmlEscape(title) + "</title></head>"
	                "<body style=\"background: #FFFFFF; color: #202020; padding: 10px;\">";
	if (!title.empty())
		h += "<h2 style=\"color: #C06010; margin-bottom: 12px;\">" + htmlEscape(title) + "</h2>";

	for (size_t i = 0; i < doc.nodes.size(); i++)
	{
		const Node& n = doc.nodes[i];
		if (!n.isElement()) continue;
		const std::string& t = n.tag;
		if (t == "p" || t == "h1" || t == "h2" || t == "h3" || t == "h4" ||
		    t == "h5" || t == "h6" || t == "li" || t == "pre" || t == "blockquote")
		{
			std::string text = strTrim(doc.textOf((int)i));
			if (!text.empty())
			{
				if (t == "pre")
					h += "<pre style=\"background: #EEEEEE; color: #111111; padding: 6px;\">" + htmlEscape(text) + "</pre>";
				else if (t[0] == 'h')
					h += "<" + t + " style=\"color: #B05010; margin-top: 10px;\">" + htmlEscape(text) + "</" + t + ">";
				else if (t == "li")
					h += "<div style=\"margin-left: 12px; margin-bottom: 4px;\">&bull; " + htmlEscape(text) + "</div>";
				else
					h += "<p style=\"margin-bottom: 8px; line-height: 1.3;\">" + htmlEscape(text) + "</p>";
			}
		}
	}
	h += "</body></html>";
	return h;
}

// -------------------------------------------------------------------------
void Browser::renderInto(int tabIdx, Document&& doc)
{
	Tab& tab = tabs_[tabIdx];
	releaseJavaScript(tab);
	tab.doc = std::move(doc);
	tab.jsRequired = hasRunnableScripts(tab.doc, tab.url);
	if (set_.enableJS && tab.jsRequired && !initializeJavaScript(tab))
		diagBootNote("js_context_init_failed", 1);
	if (set_.enableJS && tab.jsRequired && tab.jsRuntime && tab.jsContext)
	{
			bindBrowserPage(*this, tab, tabIdx);
		queueScripts(tabIdx, tab.doc, tab.url);
	}
	rerenderTab(tabIdx);
}

void Browser::rerenderTab(int tabIdx)
{
	diagStage("layout");
	pendingImages_.clear();
	Tab& tab = tabs_[tabIdx];
	tab.pageCacheKey = pageFingerprint(tab);
	tab.failedSegmentIndex = -1;
	int focusedDomNode = -1;
	if (tab.focusNode >= 0 && tab.focusNode < (int)tab.page.fields.size())
		focusedDomNode = tab.page.fields[(size_t)tab.focusNode].node;
	tab.title = tab.doc.title();
	tab.pageReady = false;

	PageLayout lay(tab.doc, tab.css, set_);
	diagStage("layout start");
	diagBootNote("page_layout_cap", set_.maxPageHeight);
	diagBootNote("app_mem_free_before_layout",
		(long)osGetMemRegionFree(MEMREGION_APPLICATION));
	diagBootNote("linear_mem_free_before_layout", (long)linearSpaceFree());
	const int segmentIndex = clamp((tab.scrollY + NW_TOP_HEIGHT - 1) /
		NW_PAGE_SEGMENT_HEIGHT, 0, NW_MAX_PAGE_SEGMENTS - 1);
	const int segmentHeight = segmentIndex == 0
		? NW_PAGE_SEGMENT_HEIGHT
		: NW_PAGE_SEGMENT_HEIGHT + NW_PAGE_SEGMENT_OVERLAP;
	if (!lay.renderSegment(tab.page, tab.url, segmentIndex, segmentHeight))
	{
		diagStage("layout failed; rendering fallback");
		tab.page.clear();
		std::string rHtml = extractReaderHtml(tab.doc, tab.title);
		Document rDoc;
		if (htmlParse(rHtml, rDoc))
		{
			PageLayout lay2(rDoc, g_emptyCss, set_);
			lay2.renderSegment(tab.page, tab.url, 0, NW_PAGE_SEGMENT_HEIGHT);
		}
		else
		{
			Document errDoc;
			std::string h = "<html><body><h3>Memoire insuffisante</h3><p>"
				"La page est trop lourde pour la New 3DS.</p></body></html>";
			htmlParse(h, errDoc);
			PageLayout lay2(errDoc, g_emptyCss, set_);
			lay2.renderSegment(tab.page, tab.url, 0, NW_PAGE_SEGMENT_HEIGHT);
		}
		diagStage("layout fallback complete");
	}
	else
	{
		const bool isInternal = strStartsWith(toLower(tab.url), "nw://") || strStartsWith(toLower(tab.url), "nw:/");
		if (!isInternal && tab.page.height < 60 && tab.doc.nodes.size() > 25)
		{
			std::string rHtml = extractReaderHtml(tab.doc, tab.title);
			Document rDoc;
			if (htmlParse(rHtml, rDoc))
			{
				tab.page.clear();
				PageLayout layReader(rDoc, g_emptyCss, set_);
				layReader.render(tab.page, tab.url);
			}
		}
		diagBootNote("page_render_height", tab.page.height);
		diagBootNote("app_mem_free_after_layout",
			(long)osGetMemRegionFree(MEMREGION_APPLICATION));
		diagBootNote("linear_mem_free_after_layout", (long)linearSpaceFree());
		diagStage("layout complete");
	}

	tab.pageReady = true;
	layoutPending_ = false;
	progressStatus_ = tab.page.truncated
		? "Page longue — rendu par étapes, limite 4 000 px"
		: std::string();
	if (tab.page.bmp.px &&
		!pageSegmentCache_.store(tab.pageCacheKey, tab.page.segmentIndex,
			tab.page.bmp, tab.url))
		diagBootNote("segment_cache_write_failed", 1);
	if (!tab.page.bmp.px)
	{
		tab.page.clear();
		if (tab.page.bmp.create(NW_PAGE_WIDTH, NW_TOP_HEIGHT))
		{
			tab.page.bmp.clear(0xFF17131A);
			TextEngine::instance().drawLine(tab.page.bmp,
				"Mémoire insuffisante pour afficher cette page",
				16, 96, 0.58f, 0xFFF2A25C, true, false);
			tab.page.height = NW_TOP_HEIGHT;
			tab.page.errorCode = 2;
			tab.page.errorText = "Memoire insuffisante pour le rendu";
			progressStatus_ = "Memoire insuffisante pour afficher la page";
		}
	}
	if (focusedDomNode >= 0)
	{
		tab.focusNode = -1;
		for (size_t i = 0; i < tab.page.fields.size(); ++i)
			if (tab.page.fields[i].node == focusedDomNode) { tab.focusNode = (int)i; break; }
	}
	if (tab.page.jsUsed && !set_.enableJS)
		tab.page.errorCode = 4; // avertissement JavaScript (affiche par l'UI)
	tab.renderedDomVersion = tab.doc.mutationVersion;

	// Vignettes : telechargees apres la mise en page, une fois qu'on connait
	// les adresses reellement presentes sur la page.
	if (set_.loadImages)
		queueImages(tabIdx, 6);
}

void Browser::queueImages(int tabIdx, int maximum)
{
	pendingImages_.clear();
	pendingImagesTab_ = tabIdx;
	if (maximum <= 0) return;

	const Tab& tab = tabs_[tabIdx];
	pendingImagesReferer_ = tab.url;
	for (const auto& image : tab.page.images)
	{
		if ((int)pendingImages_.size() >= maximum) break;
		if (image.src.empty() || images_.find(image.src)) continue;
		if (std::find(pendingImages_.begin(), pendingImages_.end(), image.src) ==
			pendingImages_.end())
			pendingImages_.push_back(image.src);
	}
}

// -------------------------------------------------------------------------
// Interactions avec la page
// -------------------------------------------------------------------------
int Browser::hitLink(int x, int y) const
{
	const Tab& t = tabs_[current_];
	if (!t.pageReady) return -1;
	int sy = t.scrollY;
	for (size_t i = 0; i < t.page.links.size(); i++)
	{
		const LinkBox& lb = t.page.links[i];
		if (x >= lb.x && x < lb.x + lb.w && y + sy >= lb.y && y + sy < lb.y + lb.h)
			return (int)i;
	}
	return -1;
}

int Browser::submitHit(const PageModel& page, int x, int y) const
{
	for (size_t i = 0; i < page.submits.size(); i++)
	{
		const FormSubmit& fs = page.submits[i];
		if (x >= fs.x && x < fs.x + fs.w && y >= fs.y && y < fs.y + fs.h)
			return (int)i;
	}
	return -1;
}

void Browser::tap(int x, int y)
{
	Tab& tab = currentTab();
	if (!tab.pageReady) return;

	// Le champ en cours d'edition est oublie a chaque appui : il n'est
	// renseigne que si l'appui tombe sur un champ. Sans cette remise a zero,
	// focusNode restait positionne apres le premier champ touche, et tout
	// appui suivant (meme sur un lien) rouvrait le clavier : impossible de
	// continuer a se servir de la page.
	endEditField();

	// lien ?
	int idx = hitLink(x, y);
	if (idx >= 0)
	{
		const LinkBox& link = tab.page.links[idx];
		if (tab.jsContext && link.node >= 0)
		{
			tab.doc.hoveredNode = link.node; ++tab.doc.mutationVersion; refreshComputedStyles(tab);
			const std::string target = "node:" + std::to_string(link.node);
			const bool pointerAllowed = jsPageDispatchEvent(tab.jsContext, "pointerdown", target.c_str(), nullptr, x, y + tab.scrollY);
			jsPageDispatchEvent(tab.jsContext, "pointerup", target.c_str(), nullptr, x, y + tab.scrollY);
			const bool clickAllowed = pointerAllowed && jsPageDispatchEvent(tab.jsContext, "click", target.c_str());
			tab.doc.hoveredNode = -1; ++tab.doc.mutationVersion;
			if (!clickAllowed) return;
		}
		const std::string& url = link.url;
		if (strStartsWith(url, "javascript:")) return;
		if (strStartsWith(url, "mailto:")) return;
		openUrl(url);
		return;
	}

	// champ de formulaire (coordonnees de page : on ajoute le defilement)
	const int pageY = y + tab.scrollY;
	for (size_t i = 0; i < tab.page.fields.size(); i++)
	{
		FormField& f = tab.page.fields[i];
		if (x >= f.x && x < f.x + f.w && pageY >= f.y && pageY < f.y + f.h)
		{
			if (f.node >= 0 && tab.doc.nodes[(size_t)f.node].hasAttr("disabled")) return;
			const std::string target = "node:" + std::to_string(f.node);
			if (tab.jsContext && f.node >= 0)
			{
				tab.doc.hoveredNode = f.node; ++tab.doc.mutationVersion; refreshComputedStyles(tab);
				const bool pointerAllowed = jsPageDispatchEvent(tab.jsContext, "pointerdown", target.c_str(), nullptr, x, pageY);
				jsPageDispatchEvent(tab.jsContext, "pointerup", target.c_str(), nullptr, x, pageY);
				const bool clickAllowed = pointerAllowed && jsPageDispatchEvent(tab.jsContext, "click", target.c_str());
				tab.doc.hoveredNode = -1; ++tab.doc.mutationVersion;
				if (!clickAllowed) return;
			}
			if (f.kind == FieldCheckbox)
			{
				const bool checked = f.checked;
				f.checked = !checked;
				if (f.node >= 0)
				{
					if (checked) tab.doc.removeAttr(f.node, "checked");
					else tab.doc.setAttr(f.node, "checked", "");
				}
				if (tab.page.bmp.px && f.w > 4 && f.h > 4)
				{
					tab.page.bmp.fillRect(f.x + 1, f.y + 3, f.w - 2, f.h - 2, 0xFFFFFFFF);
					if (!checked)
						tab.page.bmp.fillRect(f.x + 3, f.y + 5, f.w - 6, f.h - 6, 0xFF9E5A2E);
				}
				if (tab.jsContext && f.node >= 0)
				{
					jsPageDispatchEvent(tab.jsContext, "input", target.c_str());
					jsPageDispatchEvent(tab.jsContext, "change", target.c_str());
					f.checked = tab.doc.nodes[(size_t)f.node].hasAttr("checked");
				}
				return;
			}
			else if (f.kind == FieldRadio)
			{
				f.checked = true;
				if (f.node >= 0) tab.doc.setAttr(f.node, "checked", "");
				for (size_t j = 0; j < tab.page.fields.size(); j++)
				{
					FormField& other = tab.page.fields[j];
					if (j != i && other.kind == FieldRadio && other.formId == f.formId && other.name == f.name)
					{
						other.checked = false;
						if (other.node >= 0) tab.doc.removeAttr(other.node, "checked");
						if (tab.page.bmp.px && other.w > 4 && other.h > 4)
							tab.page.bmp.fillRect(other.x + 1, other.y + 3, other.w - 2, other.h - 2, 0xFFFFFFFF);
					}
				}
				if (tab.page.bmp.px && f.w > 4 && f.h > 4)
					tab.page.bmp.fillRect(f.x + 3, f.y + 5, f.w - 6, f.h - 6, 0xFF9E5A2E);
				if (tab.jsContext && f.node >= 0)
				{
					jsPageDispatchEvent(tab.jsContext, "input", target.c_str());
					jsPageDispatchEvent(tab.jsContext, "change", target.c_str());
					f.checked = tab.doc.nodes[(size_t)f.node].hasAttr("checked");
				}
				return;
			}
			beginEditField((int)i);
			return;
		}
	}

	// bouton submit ou action (coordonnees de page)
	for (size_t i = 0; i < tab.page.submits.size(); i++)
	{
		const FormSubmit& fs = tab.page.submits[i];
		if (x >= fs.x && x < fs.x + fs.w &&
			pageY >= fs.y && pageY < fs.y + fs.h)
		{
			if (fs.node >= 0 && tab.doc.nodes[(size_t)fs.node].hasAttr("disabled")) return;
			if (tab.jsContext && fs.node >= 0)
			{
				tab.doc.hoveredNode = fs.node; ++tab.doc.mutationVersion; refreshComputedStyles(tab);
				const std::string target = "node:" + std::to_string(fs.node);
				const bool pointerAllowed = jsPageDispatchEvent(tab.jsContext, "pointerdown", target.c_str(), nullptr, x, pageY);
				jsPageDispatchEvent(tab.jsContext, "pointerup", target.c_str(), nullptr, x, pageY);
				const bool clickAllowed = pointerAllowed && jsPageDispatchEvent(tab.jsContext, "click", target.c_str());
				tab.doc.hoveredNode = -1; ++tab.doc.mutationVersion;
				if (!clickAllowed) return;
			}
			submitFormByPage((int)i);
			return;
		}
	}
}

void Browser::beginEditField(int fieldIndex)
{
	Tab& tab = currentTab();
	if (fieldIndex < 0 || fieldIndex >= (int)tab.page.fields.size()) return;
	if (tab.focusNode >= 0 && tab.focusNode != fieldIndex) endEditField();
	const FormField& field = tab.page.fields[(size_t)fieldIndex];
	if (field.node >= 0 && tab.doc.nodes[(size_t)field.node].hasAttr("readonly")) return;
	tab.focusNode = fieldIndex;
	if (field.node >= 0 && tab.doc.focusedNode != field.node)
	{
		tab.doc.focusedNode = field.node; ++tab.doc.mutationVersion; refreshComputedStyles(tab);
	}
	if (tab.jsContext && field.node >= 0)
	{
		const std::string target = "node:" + std::to_string(field.node);
		jsPageDispatchEvent(tab.jsContext, "focus", target.c_str());
	}
}

void Browser::endEditField()
{
	Tab& tab = currentTab();
	const int fieldIndex = tab.focusNode;
	if (fieldIndex >= 0 && fieldIndex < (int)tab.page.fields.size())
	{
		const FormField& field = tab.page.fields[(size_t)fieldIndex];
		if (tab.jsContext && field.node >= 0)
		{
			const std::string target = "node:" + std::to_string(field.node);
			jsPageDispatchEvent(tab.jsContext, "blur", target.c_str());
			jsPageDispatchEvent(tab.jsContext, "change", target.c_str());
		}
	}
	tab.focusNode = -1;
	if (tab.doc.focusedNode >= 0) { tab.doc.focusedNode = -1; ++tab.doc.mutationVersion; refreshComputedStyles(tab); }
}

bool Browser::setFieldValue(int fieldIndex, const std::string& v)
{
	Tab& tab = currentTab();
	if (fieldIndex < 0 || fieldIndex >= (int)tab.page.fields.size()) return false;
	FormField& field = tab.page.fields[fieldIndex];
	const std::string previousValue = field.value;
	if (tab.jsContext && field.node >= 0)
	{
		std::string key = "Unidentified";
		if (v.size() < previousValue.size()) key = "Backspace";
		else if (v.size() > previousValue.size()) key = v.substr(previousValue.size());
		const std::string target = "node:" + std::to_string(field.node);
		if (!jsPageDispatchEvent(tab.jsContext, "keydown", target.c_str(), key.c_str())) return false;
	}
	tab.page.fields[fieldIndex].value = v;
	if (field.node >= 0) tab.doc.setAttr(field.node, "value", v);
	if (tab.jsContext && tab.page.fields[fieldIndex].node >= 0)
	{
		const std::string target = "node:" + std::to_string(field.node);
		jsPageDispatchEvent(tab.jsContext, "input", target.c_str());
		std::string key = v.size() < previousValue.size() ? "Backspace" :
			(v.size() > previousValue.size() ? v.substr(previousValue.size()) : "Unidentified");
		jsPageDispatchEvent(tab.jsContext, "keyup", target.c_str(), key.c_str());
		if (field.node >= 0)
		{
			const char* domValue = tab.doc.nodes[(size_t)field.node].getAttr("value");
			if (domValue) field.value = domValue;
		}
	}

	// Rafraichir le rendu du champ modifie sur le bitmap de page
	const FormField& f = tab.page.fields[fieldIndex];
	if (tab.page.bmp.px && f.w > 4 && f.h > 4)
	{
		tab.page.bmp.fillRect(f.x + 1, f.y + 1, f.w - 2, f.h - 2, 0xFFF4F4F4);
		tab.page.bmp.addRect(f.x, f.y, f.w, f.h, 0xFF968A8A, 1);
		if (!v.empty())
		{
			std::string shown = v;
			if (f.kind == FieldPassword)
				shown = std::string(v.size(), '*');
			if (shown.size() > 18) shown = shown.substr(0, 17) + "...";
			TextEngine::instance().drawLine(tab.page.bmp, shown, f.x + 3, f.y + 3, 0.7f, 0xFF2C2222, false, false);
		}
	}
	return true;
}

void Browser::submitFormByNode(int submitNode)
{
	Tab& tab = currentTab();
	for (size_t i = 0; i < tab.page.submits.size(); i++)
		if (tab.page.submits[i].node == submitNode)
		{
			submitFormByPage((int)i);
			return;
		}
}

void Browser::submitFormByPage(int submitIndex)
{
	Tab& tab = currentTab();
	if (submitIndex < 0 || submitIndex >= (int)tab.page.submits.size()) return;
	const FormSubmit& fs = tab.page.submits[submitIndex];
	auto syncFields = [&]() {
		for (int fieldIndex : fs.fields)
		{
			if (fieldIndex < 0 || fieldIndex >= (int)tab.page.fields.size()) continue;
			FormField& field = tab.page.fields[(size_t)fieldIndex];
			if (field.node < 0 || (size_t)field.node >= tab.doc.nodes.size()) continue;
			const Node& node = tab.doc.nodes[(size_t)field.node];
			field.checked = node.hasAttr("checked");
			if (const char* value = node.getAttr("value")) field.value = value;
			field.disabled = node.hasAttr("disabled");
		}
	};
	syncFields();
	for (int fieldIndex : fs.fields)
	{
		if (fieldIndex < 0 || fieldIndex >= (int)tab.page.fields.size()) continue;
		const FormField& field = tab.page.fields[(size_t)fieldIndex];
		if (field.disabled || field.node < 0 || (size_t)field.node >= tab.doc.nodes.size()) continue;
		const Node& node = tab.doc.nodes[(size_t)field.node];
		if (node.hasAttr("readonly")) continue;
		const bool required = node.hasAttr("required");
		bool invalid = false;
		if (field.kind == FieldCheckbox) invalid = required && !field.checked;
		else if (field.kind == FieldRadio)
		{
			bool anyChecked = false;
			for (int otherIndex : fs.fields)
				if (otherIndex >= 0 && otherIndex < (int)tab.page.fields.size())
				{
					const FormField& other = tab.page.fields[(size_t)otherIndex];
					if (other.kind == FieldRadio && other.formId == field.formId &&
						other.name == field.name && other.checked) anyChecked = true;
				}
			invalid = required && !anyChecked;
		}
		else
		{
			invalid = required && strTrim(field.value).empty();
			const char* type = node.getAttr("type");
			if (!invalid && !strTrim(field.value).empty() && type && toLower(type) == "email")
			{
				const size_t at = field.value.find('@');
				invalid = at == std::string::npos || at == 0 ||
					field.value.find('.', at == std::string::npos ? 0 : at + 1) == std::string::npos;
			}
			if (!invalid && !strTrim(field.value).empty() && type && toLower(type) == "number")
			{
				char* end = nullptr;
				std::strtod(field.value.c_str(), &end);
				invalid = end == field.value.c_str() || !end || *end != '\0';
			}
		}
		const char* minLength = node.getAttr("minlength");
		if (!invalid && minLength && field.value.size() < (size_t)std::max(0, atoi(minLength))) invalid = true;
		const char* maxLength = node.getAttr("maxlength");
		if (!invalid && maxLength && field.value.size() > (size_t)std::max(0, atoi(maxLength))) invalid = true;
		if (invalid)
		{
			if (tab.jsContext)
			{
				const std::string invalidTarget = "node:" + std::to_string(field.node);
				jsPageDispatchEvent(tab.jsContext, "invalid", invalidTarget.c_str());
			}
			if (field.kind != FieldCheckbox && field.kind != FieldRadio)
				beginEditField(fieldIndex);
			return;
		}
	}
	int formNode = fs.node;
	while (formNode >= 0 && (size_t)formNode < tab.doc.nodes.size() &&
		tab.doc.nodes[(size_t)formNode].tag != "form")
		formNode = tab.doc.nodes[(size_t)formNode].parent;
	if (formNode >= 0 && tab.jsContext)
	{
		const std::string formTarget = "node:" + std::to_string(formNode);
		if (!jsPageDispatchEvent(tab.jsContext, "submit", formTarget.c_str())) return;
	}
	syncFields();

	// Actions speciales
	if (fs.action == "nw://back") { back(); return; }
	if (fs.action == "nw://reload") { reload(); return; }

	// Bouton d'action / navigation simple (hors formulaire complet)
	if (fs.fields.empty() && toLower(fs.method) != "post" && !fs.action.empty() && fs.action != tab.url)
	{
		openUrl(urlJoin(tab.url, fs.action));
		return;
	}

	const bool isPost = (toLower(fs.method) == "post");

	std::string target = fs.action;
	if (target.empty()) target = tab.url;
	else target = urlJoin(tab.url, target);

	if (strStartsWith(toLower(target), "javascript:")) return;

	std::string q;
	bool hasCsrf = false;
	for (int fi : fs.fields)
	{
		if (fi < 0 || fi >= (int)tab.page.fields.size()) continue;
		const FormField& f = tab.page.fields[fi];
		if (f.name.empty() || f.disabled) continue;
		// Ne pas soumettre les cases non cochees
		if ((f.kind == FieldCheckbox || f.kind == FieldRadio) && !f.checked)
			continue;
		std::string nLow = toLower(f.name);
		if (nLow.find("csrf") != std::string::npos ||
		    nLow.find("token") != std::string::npos ||
		    nLow.find("authenticity") != std::string::npos)
		{
			hasCsrf = true;
		}
		if (!q.empty()) q += "&";
		q += urlEncode(f.name) + "=" + urlEncode(f.value);
	}

	// Ajouter le bouton submit clique s'il a un name/value
	if (!fs.name.empty())
	{
		if (!q.empty()) q += "&";
		q += urlEncode(fs.name) + "=" + urlEncode(fs.value);
	}

	// Si aucun champ CSRF trouve mais que la page a un jeton CSRF detecte (meta tag)
	std::string pageCsrf = tab.page.csrfToken;
	if (!hasCsrf && !pageCsrf.empty())
	{
		std::string paramName = !tab.page.csrfParam.empty() ? tab.page.csrfParam : "csrf_token";
		if (!q.empty()) q += "&";
		q += urlEncode(paramName) + "=" + urlEncode(pageCsrf);
	}

	std::string prevUrl = tab.url;
	if (isPost)
	{
		cancelTabRequest(current_);
		resetTab(tab);
		loadUrlInto(current_, target, true, "POST", q, prevUrl, pageCsrf);
	}
	else
	{
		if (!q.empty())
		{
			target += (target.find('?') != std::string::npos) ? "&" : "?";
			target += q;
		}
		openUrl(target);
	}
}

void Browser::scroll(int dy)
{
	Tab& tab = currentTab();
	if (!tab.pageReady || tab.page.height <= NW_TOP_HEIGHT) return;
	const int desired = std::max(0, tab.scrollY + dy);
	const int max = maxPageScroll(tab.page);
	if (desired > max && tab.page.truncated)
	{
		const int requestedSegment = (desired + NW_TOP_HEIGHT - 1) /
			NW_PAGE_SEGMENT_HEIGHT;
		const int index = std::min(requestedSegment, tab.page.segmentIndex + 1);
		requestSegment(current_, index, desired);
		tab.scrollY = max;
		return;
	}
	tab.scrollY = std::min(desired, max);
	const int visibleSegment = (tab.scrollY + NW_TOP_HEIGHT - 1) /
		NW_PAGE_SEGMENT_HEIGHT;
	if (visibleSegment != tab.page.segmentIndex)
		requestSegment(current_, visibleSegment, tab.scrollY);
}

int Browser::scrollY() const
{
	if (tabs_.empty()) return 0;
	return tabs_[current_].scrollY;
}

void Browser::scrollTo(int y)
{
	Tab& tab = currentTab();
	if (!tab.pageReady) return;
	const int desired = std::max(0, y);
	const int max = maxPageScroll(tab.page);
	if (desired > max && tab.page.truncated)
	{
		const int requestedSegment = (desired + NW_TOP_HEIGHT - 1) /
			NW_PAGE_SEGMENT_HEIGHT;
		const int index = std::min(requestedSegment, tab.page.segmentIndex + 1);
		requestSegment(current_, index, desired);
		tab.scrollY = max;
		return;
	}
	tab.scrollY = std::min(desired, max);
	const int visibleSegment = (tab.scrollY + NW_TOP_HEIGHT - 1) /
		NW_PAGE_SEGMENT_HEIGHT;
	if (visibleSegment != tab.page.segmentIndex)
		requestSegment(current_, visibleSegment, tab.scrollY);
}

void Browser::requestSegment(int tabIdx, int segmentIndex, int desiredScrollY)
{
	if (tabIdx < 0 || tabIdx >= (int)tabs_.size() || segmentIndex < 0) return;
	if (segmentIndex >= NW_MAX_PAGE_SEGMENTS)
	{
		progressStatus_ = "Limite de longueur atteinte (4 000 px)";
		return;
	}
	Tab& tab = tabs_[(size_t)tabIdx];
	if (tab.failedSegmentIndex == segmentIndex)
	{
		progressStatus_ = "Mémoire insuffisante pour ce segment";
		return;
	}
	if (segmentIndex == tab.page.segmentIndex && !segmentPending_) return;
	segmentPending_ = true;
	segmentTab_ = tabIdx;
	segmentIndexPending_ = segmentIndex;
	pendingScrollY_ = desiredScrollY;
	layoutPending_ = true;
	progressStatus_ = "Mise en page du segment " +
		itos((long)(segmentIndex + 1)) + "...";
}

bool Browser::tickProgressiveLayout()
{
	if (!segmentPending_) return false;
	const int tabIdx = segmentTab_;
	const int segmentIndex = segmentIndexPending_;
	const int desiredScroll = pendingScrollY_;
	segmentPending_ = false;
	segmentTab_ = -1;
	segmentIndexPending_ = -1;
	if (tabIdx < 0 || tabIdx != current_ || tabIdx >= (int)tabs_.size())
	{
		pendingScrollY_ = -1;
		layoutPending_ = false;
		return false;
	}

	Tab& tab = tabs_[(size_t)tabIdx];
	Bitmap cached;
	const bool needsDomLayout = segmentIndex > tab.page.furthestSegmentIndex;
	const bool cacheHit = !needsDomLayout &&
		pageSegmentCache_.load(tab.pageCacheKey, segmentIndex, cached);
	if (cacheHit && !needsDomLayout)
	{
		tab.page.bmp = std::move(cached);
		tab.page.segmentIndex = segmentIndex;
	}
	else
	{
		const int segmentHeight = segmentIndex == 0
			? NW_PAGE_SEGMENT_HEIGHT
			: NW_PAGE_SEGMENT_HEIGHT + NW_PAGE_SEGMENT_OVERLAP;
		PageModel rendered;
		PageLayout layout(tab.doc, tab.css, set_);
		if (!layout.renderSegment(rendered, tab.url, segmentIndex, segmentHeight))
		{
			layoutPending_ = false;
			pendingScrollY_ = -1;
			tab.failedSegmentIndex = segmentIndex;
			progressStatus_ = "Mémoire insuffisante pour le segment suivant";
			diagBootNote("progressive_segment_render_failed", segmentIndex);
			return true;
		}
		if (cacheHit)
			rendered.bmp = std::move(cached);
		if (segmentIndex < tab.page.furthestSegmentIndex)
		{
			tab.page.bmp = std::move(rendered.bmp);
			tab.page.segmentIndex = segmentIndex;
		}
		else
		{
			tab.page = std::move(rendered);
		}
		if (tab.page.bmp.px &&
			!pageSegmentCache_.store(tab.pageCacheKey, segmentIndex,
				tab.page.bmp, tab.url))
			diagBootNote("segment_cache_write_failed", 1);
		tab.pageReady = true;
		tab.renderedDomVersion = tab.doc.mutationVersion;
	}

	const int max = maxPageScroll(tab.page);
	tab.scrollY = std::min(std::max(0, desiredScroll), max);
	pendingScrollY_ = -1;
	layoutPending_ = false;
	progressStatus_ = tab.page.truncated
		? "Page longue — rendu par étapes, limite 4 000 px"
		: std::string();

	if (tab.page.truncated && desiredScroll > max)
	{
		const int nextIndex = (desiredScroll + NW_TOP_HEIGHT - 1) /
			NW_PAGE_SEGMENT_HEIGHT;
		requestSegment(tabIdx, nextIndex, desiredScroll);
	}
	return true;
}

// Sert au defilement gestuel : on veut savoir si la page peut encore bouger
// dans une direction, afin de ne pas lancer d'inertie contre un bord.
bool Browser::canScrollBy(int dy) const
{
	if (tabs_.empty()) return false;
	const Tab& tab = tabs_[current_];
	if (!tab.pageReady || tab.page.height <= NW_TOP_HEIGHT) return false;
	const int max = maxPageScroll(tab.page);
	if (dy < 0) return tab.scrollY > 0;
	if (dy > 0)
		return tab.scrollY < max ||
			(tab.page.truncated && tab.page.segmentIndex + 1 < NW_MAX_PAGE_SEGMENTS);
	return false;
}

// -------------------------------------------------------------------------
std::string Browser::searchEngineName() const
{
	return set_.searchEngine;
}
