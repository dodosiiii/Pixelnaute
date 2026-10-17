#include "browser.hpp"
#include "../search/search.hpp"
#include "../search/search_page.hpp"
#include "../render/text.hpp"
#include "../html/parser.hpp"
#include "../storage/downloads.hpp"
#include "../diag/diag.hpp"
#include <3ds/allocator/linear.h>
#include <algorithm>

static CssEngine g_emptyCss;

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
}

void Browser::resetTab(Tab& t)
{
	t.doc.clear();
	t.css.clear();
	t.page.clear();
	t.pageReady = false;
	t.title.clear();
	t.scrollY = 0;
	t.focusNode = -1;
}

// -------------------------------------------------------------------------
void Browser::openInput(const std::string& input)
{
	std::string s = strTrim(input);
	if (s.empty()) return;
	std::string resolved = resolveInput(s, searchEngineName());
	if (resolved.empty()) return;
	if (strStartsWith(toLower(resolved), "nw://"))
	{
		openUrl(resolved);
		return;
	}
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
	resetTab(t);
	loadUrlInto(current_, targetUrl, record);
}

void Browser::loadUrlInto(int tabIdx, const std::string& url, bool record, const std::string& method, const std::string& postData)
{
	cancelTabRequest(tabIdx);
	responsePending_ = false;
	layoutPending_ = false;
	pendingResponse_ = NetResponse();
	pendingImages_.clear();
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
	if (!net_.start(url, (size_t)set_.maxHtmlBytes, set_.verifyTLS, method, postData))
	{
		pendingResponse_.url = url;
		pendingResponse_.error = "Impossible de demarrer la requete reseau.";
		responsePending_ = true;
		layoutPending_ = true;
	}
}

void Browser::reload()
{
	pendingImages_.clear();
	responsePending_ = false;
	layoutPending_ = false;
	pendingResponse_ = NetResponse();
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
	net_.cancel();
	requestTab_ = -1;
	responsePending_ = false;
	layoutPending_ = false;
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
	cancelTabRequest(current_);
	pendingImages_.clear();
	responsePending_ = false;
	layoutPending_ = false;
	pendingResponse_ = NetResponse();
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
	     "<span style=\"font-size: 40px; font-weight: bold; color: #F2A25C\">Pixel</span>"
	     "<span style=\"font-size: 40px; font-weight: bold; color: #ECE8E8\">naute</span></div>";
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
	tabs_.erase(tabs_.begin() + index);
	if (current_ >= (int)tabs_.size()) current_ = (int)tabs_.size() - 1;
}

void Browser::selectTab(int index)
{
	if (index >= 0 && index < (int)tabs_.size()) current_ = index;
}

void Browser::nextTab()
{
	if (tabs_.empty()) return;
	current_ = (current_ + 1) % (int)tabs_.size();
}

void Browser::prevTab()
{
	if (tabs_.empty()) return;
	current_ = (current_ == 0) ? (int)tabs_.size() - 1 : current_ - 1;
}

Browser::Tab& Browser::currentTab()
{
	static Tab empty;
	return tabs_.empty() ? empty : tabs_[current_];
}

// -------------------------------------------------------------------------
void Browser::pollNetwork()
{
	if (responsePending_)
	{
		responsePending_ = false;
		layoutPending_ = false;
		onResponse(pendingResponse_);
		pendingResponse_ = NetResponse();
		return;
	}

	if (net_.state() == NetState::Done)
	{
		if (net_.takeResult(pendingResponse_))
		{
			responsePending_ = true;
			layoutPending_ = true;
			diagStage("response ready");
		}
	}
}

bool Browser::tickImages()
{
	if (!set_.loadImages)
	{
		pendingImages_.clear();
		return false;
	}
	if (pendingImages_.empty()) return false;
	const std::string url = pendingImages_.back();
	pendingImages_.pop_back();
	diagStage("image fetch");
	const bool fetched = images_.fetch(url, pendingImagesReferer_) != nullptr;
	if (fetched) diagStage("image decoded");
	return fetched;
}

// -------------------------------------------------------------------------
// Traitement d'une reponse reseau.
// -------------------------------------------------------------------------
void Browser::onResponse(NetResponse& resp)
{
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
			     "incompatible avec le composant reseau de la New 3DS.</p>";

			std::string httpUrl = t.url;
			if (strStartsWith(httpUrl, "https://"))
				httpUrl = "http://" + httpUrl.substr(8);

			h += "<p style=\"margin-bottom: 12px\"><a href=\"" + htmlEscape(httpUrl) +
			     "\" style=\"color: #5CC4F2; font-weight: bold\">[ Continuer en HTTP direct ]</a></p>";
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
	if (searchPageRewrite(resp.body, rewritten))
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

	// Feuilles de style externes (<link rel="stylesheet">)
	size_t loadedSheets = 0;
	for (size_t i = 0; i < d.nodes.size() && loadedSheets < 3; i++)
	{
		const Node& nd = d.nodes[i];
		if (nd.isElement() && nd.tag == "link")
		{
			const char* rel = nd.getAttr("rel");
			const char* href = nd.getAttr("href");
			if (rel && href && *href && toLower(rel).find("stylesheet") != std::string::npos)
			{
				std::string cssUrl = urlJoin(resp.url, href);
				if (strStartsWith(cssUrl, "http://") || strStartsWith(cssUrl, "https://"))
				{
					HttpOptions cssOpt;
					cssOpt.accept = "text/css,*/*;q=0.1";
					cssOpt.referer = resp.url;
					cssOpt.maxBytes = 150 * 1024;
					cssOpt.timeoutMs = 4000;
					NetResponse cssResp;
					if (httpGet(cssUrl, cssOpt, cssResp, nullptr, nullptr) && cssResp.ok && !cssResp.body.empty())
					{
						t.css.parseStylesheet(cssResp.body);
						loadedSheets++;
					}
				}
			}
		}
	}

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
	diagStage("layout");
	pendingImages_.clear();
	Tab& tab = tabs_[tabIdx];
	tab.doc = std::move(doc);
	tab.title = tab.doc.title();
	tab.pageReady = false;

	PageLayout lay(tab.doc, tab.css, set_);
	diagStage("layout start");
	diagBootNote("page_layout_cap", set_.maxPageHeight);
	diagBootNote("app_mem_free_before_layout",
		(long)osGetMemRegionFree(MEMREGION_APPLICATION));
	diagBootNote("linear_mem_free_before_layout", (long)linearSpaceFree());
	if (!lay.render(tab.page, tab.url))
	{
		diagStage("layout failed; rendering fallback");
		tab.page.clear();
		std::string rHtml = extractReaderHtml(tab.doc, tab.title);
		Document rDoc;
		if (htmlParse(rHtml, rDoc))
		{
			PageLayout lay2(rDoc, g_emptyCss, set_);
			lay2.render(tab.page, tab.url);
		}
		else
		{
			Document errDoc;
			std::string h = "<html><body><h3>Memoire insuffisante</h3><p>"
				"La page est trop lourde pour la New 3DS.</p></body></html>";
			htmlParse(h, errDoc);
			PageLayout lay2(errDoc, g_emptyCss, set_);
			lay2.render(tab.page, tab.url);
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
	if (tab.page.jsUsed && !set_.enableJS)
		tab.page.errorCode = 4; // avertissement JavaScript (affiche par l'UI)

	// Vignettes : telechargees apres la mise en page, une fois qu'on connait
	// les adresses reellement presentes sur la page.
	if (set_.loadImages)
		queueImages(tabIdx, 6);
}

void Browser::queueImages(int tabIdx, int maximum)
{
	pendingImages_.clear();
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
	tab.focusNode = -1;

	// lien ?
	int idx = hitLink(x, y);
	if (idx >= 0)
	{
		const std::string& url = tab.page.links[idx].url;
		if (strStartsWith(url, "javascript:")) return;
		if (strStartsWith(url, "mailto:")) return;
		openUrl(url);
		return;
	}

	// champ de texte (coordonnees de page : on ajoute le defilement)
	const int pageY = y + tab.scrollY;
	for (size_t i = 0; i < tab.page.fields.size(); i++)
	{
		const FormField& f = tab.page.fields[i];
		if (x >= f.x && x < f.x + f.w && pageY >= f.y && pageY < f.y + f.h)
		{
			beginEditField((int)i);
			return;
		}
	}

	// bouton submit (coordonnees de page)
	for (size_t i = 0; i < tab.page.submits.size(); i++)
	{
		const FormSubmit& fs = tab.page.submits[i];
		if (x >= fs.x && x < fs.x + fs.w &&
			pageY >= fs.y && pageY < fs.y + fs.h)
		{
			submitFormByPage((int)i);
			return;
		}
	}
}

void Browser::beginEditField(int fieldIndex)
{
	Tab& tab = currentTab();
	if (fieldIndex < 0 || fieldIndex >= (int)tab.page.fields.size()) return;
	tab.focusNode = fieldIndex;
}

bool Browser::setFieldValue(int fieldIndex, const std::string& v)
{
	Tab& tab = currentTab();
	if (fieldIndex < 0 || fieldIndex >= (int)tab.page.fields.size()) return false;
	tab.page.fields[fieldIndex].value = v;
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
	const bool isPost = (toLower(fs.method) == "post");

	std::string target = fs.action;
	if (target.empty()) target = tab.url;
	else target = urlJoin(tab.url, target);

	if (strStartsWith(toLower(target), "javascript:")) return;

	std::string q;
	for (int fi : fs.fields)
	{
		if (fi < 0 || fi >= (int)tab.page.fields.size()) continue;
		const FormField& f = tab.page.fields[fi];
		if (f.name.empty()) continue;
		if (!q.empty()) q += "&";
		q += urlEncode(f.name) + "=" + urlEncode(f.value);
	}

	if (isPost)
	{
		cancelTabRequest(current_);
		resetTab(tab);
		loadUrlInto(current_, target, true, "POST", q);
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
	int max = tab.page.height - NW_TOP_HEIGHT;
	tab.scrollY = clamp(tab.scrollY + dy, 0, max);
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
	int max = tab.page.height - NW_TOP_HEIGHT;
	if (max < 0) max = 0;
	tab.scrollY = clamp(y, 0, max);
}

// Sert au defilement gestuel : on veut savoir si la page peut encore bouger
// dans une direction, afin de ne pas lancer d'inertie contre un bord.
bool Browser::canScrollBy(int dy) const
{
	if (tabs_.empty()) return false;
	const Tab& tab = tabs_[current_];
	if (!tab.pageReady || tab.page.height <= NW_TOP_HEIGHT) return false;
	const int max = tab.page.height - NW_TOP_HEIGHT;
	if (dy < 0) return tab.scrollY > 0;
	if (dy > 0) return tab.scrollY < max;
	return false;
}

// -------------------------------------------------------------------------
std::string Browser::searchEngineName() const
{
	return set_.searchEngine;
}