#include "search_page.hpp"
#include "../common.hpp"
#include "../render/text.hpp"

#include <vector>
#include <string>

//=============================================================================
// Detection et reecriture des pages de resultats
//
// DuckDuckGo Lite (et les moteurs du meme genre) renvoient leurs resultats
// dans des tableaux. Notre moteur de mise en page ne gere les tableaux que de
// facon rudimentaire : on obtient un mur de texte illisible. On relit donc le
// HTML pour en extraire les resultats, puis on reconstruit une page simple.
//=============================================================================

static int g_lastResultCount = 0;

int searchPageLastResultCount()
{
	return g_lastResultCount;
}

//--------------------------------------------------------------------------
// Vrai si la page ressemble a une page de resultats reconnue.
//--------------------------------------------------------------------------
bool searchPageDetected(const std::string& html)
{
	const bool hasLink = html.find("result-link") != std::string::npos ||
		html.find("result__a") != std::string::npos;
	if (hasLink)
		return true;
	return false;
}

bool searchPageIsVerificationChallenge(const std::string& html)
{
	const size_t length = html.size() < 65536 ? html.size() : 65536;
	const std::string body = toLower(html.substr(0, length));
	static const char* const markers[] =
	{
		"verify you are human",
		"verify you are a human",
		"confirm you are not a robot",
		"checking your browser",
		"checking if the site connection is secure",
		"performing security verification",
		"complete the security check",
		"bots use duckduckgo too",
		"enable javascript and cookies to continue",
		"cf-challenge"
	};
	for (const char* marker : markers)
		if (body.find(marker) != std::string::npos)
			return true;
	return false;
}

int searchPageSelfTest()
{
	int fails = 0;
	if (searchPageIsVerificationChallenge(
			"<p>This article explains what a CAPTCHA is.</p>"))
		fails++;
	if (searchPageIsVerificationChallenge(
			"<p>Automated queries are discussed in this documentation.</p>"))
		fails++;
	if (searchPageIsVerificationChallenge(
			"<html><body>Accepted</body></html>"))
		fails++;
	if (!searchPageIsVerificationChallenge(
			"<html><title>Just a moment</title><p>Checking your browser before accessing</p></html>"))
		fails++;
	if (!searchPageIsVerificationChallenge(
			"<p>Unfortunately, bots use DuckDuckGo too.</p>"))
		fails++;
	if (!searchPageIsVerificationChallenge(
			"<p>Enable JavaScript and cookies to continue.</p>"))
		fails++;

	std::string rewritten;
	const std::string html =
		"<html><head><title>Results</title></head><body>"
		"<a class='result-link'>Google Search</a>"
		"<td class='result-snippet'>Navigation entry</td>"
		"<a class='result-link' href = 'https://example.org/bonjour'>Valid result</a>"
		"<td class='result-snippet'>A valid result</td></body></html>";
	if (!searchPageRewrite(html, rewritten) ||
		searchPageLastResultCount() != 1 ||
		rewritten.find("Google Search") != std::string::npos ||
		rewritten.find("https://example.org/bonjour") == std::string::npos ||
		rewritten.find("<body style=\"background: #FFFFFF") == std::string::npos ||
		rewritten.find("background: #000000") != std::string::npos)
		fails++;

	const std::string sponsoredHtml =
		"<table>"
		"<tr><td class='result-sponsored'>Sponsored</td>"
		"<td><a class='result-link' href='https://ads.example/ad'>Ad result</a></td></tr>"
		"<tr><td><a class='result-link' href='https://real.example/page'>Real result</a></td>"
		"<td class='result-snippet'>Useful result</td></tr>"
		"</table>";
	if (!searchPageRewrite(sponsoredHtml, rewritten) ||
		searchPageLastResultCount() != 1 ||
		rewritten.find("Ad result") != std::string::npos ||
		rewritten.find("https://real.example/page") == std::string::npos)
		fails++;
	return fails;
}

//--------------------------------------------------------------------------
// Decodage des entites HTML.
//
// Outre les entites nommees, il faut gerer les entites numeriques : les pages
// de resultats en contiennent beaucoup (`&#x27;` pour l'apostrophe, `&#233;`
// pour un accent). Les ignorer laissait des suites illisibles du genre
// "d&#x27;occasion".
//--------------------------------------------------------------------------
static void decodeEntities(std::string& text)
{
	struct Ent { const char* code; char value; };
	static const Ent kEntities[] = {
		{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
		{ "&quot;", '"' }, { "&apos;", '\'' }, { "&nbsp;", ' ' },
		{ "&hellip;", '.' }, { "&mdash;", '-' }, { "&ndash;", '-' },
		{ "&rsquo;", '\'' }, { "&lsquo;", '\'' }, { "&ldquo;", '"' },
		{ "&rdquo;", '"' }, { "&eacute;", 'e' }, { "&egrave;", 'e' },
		{ "&agrave;", 'a' }, { "&ccedil;", 'c' }, { nullptr, 0 }
	};
	for (const Ent* e = kEntities; e->code; e++)
	{
		size_t pos = 0;
		const size_t len = strlen(e->code);
		while ((pos = text.find(e->code, pos)) != std::string::npos)
		{
			text.replace(pos, len, 1, e->value);
			pos += 1;
		}
	}

	// Entites numeriques : &#123; ou &#x1F;
	size_t amp = 0;
	while ((amp = text.find("&#", amp)) != std::string::npos)
	{
		size_t semi = text.find(';', amp);
		if (semi == std::string::npos || semi - amp > 10) { amp += 2; continue; }
		const std::string body = text.substr(amp + 2, semi - amp - 2);
		unsigned long value = 0;
		bool ok = !body.empty();
		if (!body.empty() && (body[0] == 'x' || body[0] == 'X'))
		{
			for (size_t i = 1; i < body.size() && ok; i++)
			{
				const char c = body[i];
				if (c >= '0' && c <= '9') value = value * 16 + (unsigned)(c - '0');
				else if (c >= 'a' && c <= 'f') value = value * 16 + (unsigned)(c - 'a' + 10);
				else if (c >= 'A' && c <= 'F') value = value * 16 + (unsigned)(c - 'A' + 10);
				else ok = false;
			}
		}
		else
		{
			for (size_t i = 0; i < body.size() && ok; i++)
			{
				const char c = body[i];
				if (c >= '0' && c <= '9') value = value * 10 + (unsigned)(c - '0');
				else ok = false;
			}
		}

		if (!ok || value > 0xFFFF)
		{
			amp += 2;
			continue;
		}
		// Les points de code au-dela de l'ASCII sont ramenes a un equivalent
		// simple : la police de la console couvre l'essentiel du latin, mais
		// reste sobre.
		char replacement = '?';
		if (value < 0x80) replacement = (char)value;
		else
		{
			switch (value)
			{
				case 0xE9: case 0xE8: case 0xEA: case 0xEB: replacement = 'e'; break;
				case 0xE0: case 0xE2: case 0xE4: replacement = 'a'; break;
				case 0xE7: replacement = 'c'; break;
				case 0xEE: case 0xEF: replacement = 'i'; break;
				case 0xF4: replacement = 'o'; break;
				case 0xF9: case 0xFB: replacement = 'u'; break;
				case 0x2019: replacement = '\''; break;
				case 0x201C: case 0x201D: replacement = '"'; break;
				case 0x2013: case 0x2014: replacement = '-'; break;
				case 0x2026: replacement = '.'; break;
				case 0xA0: replacement = ' '; break;
				default: replacement = '?'; break;
			}
		}
		text.replace(amp, semi - amp + 1, 1, replacement);
		amp += 1;
	}
}

//--------------------------------------------------------------------------
// Texte lisible d'un fragment de HTML : balises retirees, entites decodees,
// espaces normalises.
//--------------------------------------------------------------------------
static std::string textOf(const std::string& html)
{
	std::string out;
	out.reserve(html.size());
	bool inTag = false;
	for (size_t i = 0; i < html.size(); i++)
	{
		const char c = html[i];
		if (c == '<') { inTag = true; continue; }
		if (c == '>') { inTag = false; out += ' '; continue; }
		if (!inTag) out += c;
	}
	decodeEntities(out);

	// Le texte des pages est en UTF-8 : on ne le simplifie que si la
	// police TrueType (qui gere l'UTF-8 complet) est absente.
	if (!TextEngine::instance().isTrueType())
		out = utf8ToSimplified(out);

	// Normalise les espaces.
	std::string clean;
	clean.reserve(out.size());
	bool lastSpace = true;
	for (char c : out)
	{
		const bool isSpace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
		if (isSpace)
		{
			if (!lastSpace) clean += ' ';
			lastSpace = true;
		}
		else
		{
			clean += c;
			lastSpace = false;
		}
	}
	while (!clean.empty() && clean.back() == ' ') clean.pop_back();
	return clean;
}

//--------------------------------------------------------------------------
// Contenu d'une balise ouvrante trouvee a `from`, jusqu'a `</tag>`.
//--------------------------------------------------------------------------
static bool extractElement(const std::string& html, size_t from,
	const char* tag, std::string& inner)
{
	const std::string open = std::string("<") + tag;
	size_t start = html.find(open, from);
	if (start == std::string::npos) return false;
	size_t gt = html.find('>', start);
	if (gt == std::string::npos) return false;
	const std::string close = std::string("</") + tag + ">";
	size_t end = html.find(close, gt);
	if (end == std::string::npos) return false;
	inner = html.substr(gt + 1, end - gt - 1);
	return true;
}

//--------------------------------------------------------------------------
// Attribut d'une balise ouvrante.
//--------------------------------------------------------------------------
static std::string attrOf(const std::string& tagText, const char* name)
{
	size_t p = tagText.find_first_of(" \t\r\n");
	if (p == std::string::npos)
		return "";

	while (p < tagText.size())
	{
		while (p < tagText.size() &&
			(tagText[p] == ' ' || tagText[p] == '\t' ||
			 tagText[p] == '\r' || tagText[p] == '\n' || tagText[p] == '/'))
			p++;
		if (p >= tagText.size() || tagText[p] == '>') break;

		const size_t nameStart = p;
		while (p < tagText.size() && tagText[p] != '=' && tagText[p] != '>' &&
			tagText[p] != ' ' && tagText[p] != '\t' &&
			tagText[p] != '\r' && tagText[p] != '\n' && tagText[p] != '/')
			p++;
		if (p == nameStart) { p++; continue; }
		const std::string attrName = toLower(tagText.substr(nameStart, p - nameStart));

		while (p < tagText.size() &&
			(tagText[p] == ' ' || tagText[p] == '\t' ||
			 tagText[p] == '\r' || tagText[p] == '\n'))
			p++;
		if (p >= tagText.size() || tagText[p] != '=')
		{
			if (attrName == toLower(name)) return "";
			continue;
		}
		p++;
		while (p < tagText.size() &&
			(tagText[p] == ' ' || tagText[p] == '\t' ||
			 tagText[p] == '\r' || tagText[p] == '\n'))
			p++;
		if (p >= tagText.size()) return "";

		std::string value;
		if (tagText[p] == '"' || tagText[p] == '\'')
		{
			const char quote = tagText[p++];
			const size_t end = tagText.find(quote, p);
			if (end == std::string::npos) return "";
			value = tagText.substr(p, end - p);
			p = end + 1;
		}
		else
		{
			const size_t start = p;
			while (p < tagText.size() && tagText[p] != '>' &&
				tagText[p] != ' ' && tagText[p] != '\t' &&
				tagText[p] != '\r' && tagText[p] != '\n')
				p++;
			value = tagText.substr(start, p - start);
		}
		if (attrName == toLower(name)) return value;
	}
	return "";
}

static bool isWebResultUrl(const std::string& url)
{
	const std::string lower = toLower(url);
	size_t authorityStart = 0;
	if (strStartsWith(lower, "http://")) authorityStart = 7;
	else if (strStartsWith(lower, "https://")) authorityStart = 8;
	else if (strStartsWith(url, "//")) authorityStart = 2;
	else if (url.size() > 1 && url[0] == '/') return true;
	else return false;

	size_t authorityEnd = url.find_first_of("/?#", authorityStart);
	if (authorityEnd == std::string::npos) authorityEnd = url.size();
	if (authorityEnd == authorityStart) return false;
	for (char c : url)
		if ((unsigned char)c <= 0x20 || c == '\\' || c == '<' || c == '>' ||
			c == '"' || c == '\'')
			return false;
	return true;
}

//--------------------------------------------------------------------------
// Extrait les resultats d'une page de moteur "sans JavaScript".
//
// Les resultats de DuckDuckGo Lite ont cette forme :
//   <a ... class='result-link' href="...">Titre</a>
//   ...
//   <td class='result-snippet'>Resume</td>
//   ...
//   <span class='link-text'>domaine.com</span>
//--------------------------------------------------------------------------
struct SearchResult
{
	std::string title;
	std::string url;
	std::string snippet;
	std::string site;
};

static void extractResults(const std::string& html, std::vector<SearchResult>& out)
{
	size_t pos = 0;
	while (out.size() < 10)
	{
		size_t at = html.find("result-link", pos);
		size_t modernAt = html.find("result__a", pos);
		const bool modern = modernAt != std::string::npos &&
			(at == std::string::npos || modernAt < at);
		if (modern) at = modernAt;
		if (at == std::string::npos) break;

		// Remonte au debut de la balise <a ...>
		size_t tagStart = html.rfind('<', at);
		if (tagStart == std::string::npos) { pos = at + 9; continue; }
		size_t tagEnd = html.find('>', at);
		if (tagEnd == std::string::npos) break;

		const std::string tagText = html.substr(tagStart, tagEnd - tagStart);
		std::string href = attrOf(tagText, "href");
		decodeEntities(href);
		size_t u = href.find("uddg=");
		if (u != std::string::npos)
		{
			size_t e = href.find('&', u);
			std::string dec = urlDecode(href.substr(u + 5, e == std::string::npos ? e : e - (u + 5)));
			if (!dec.empty()) href = dec; // lien direct vers le site
		}

		// Titre : contenu de l'ancre.
		std::string closeA = "</a>";
		size_t aEnd = html.find(closeA, tagEnd);
		std::string title;
		if (aEnd != std::string::npos)
			title = textOf(html.substr(tagEnd + 1, aEnd - tagEnd - 1));

		pos = (aEnd == std::string::npos) ? tagEnd + 1 : aEnd + closeA.size();
		if (title.empty() || !isWebResultUrl(href)) continue;

		// Les annonces sont ecartees : elles encombrent l'ecran sans etre des
		// resultats de recherche. Dans DuckDuckGo Lite elles portent la classe
		// "result-sponsored" et sont suivies d'un lien "more info".
		{
			const size_t rowStart = html.rfind("<tr", tagStart);
			const size_t annonce = html.rfind("result-sponsored", tagStart);
			const bool inSponsoredBlock =
				(annonce != std::string::npos &&
				 (rowStart == std::string::npos || annonce >= rowStart));
			const bool isMoreInfo =
				(title.size() <= 12 &&
				 (title.find("more info") != std::string::npos ||
				  title.find("Plus d") != std::string::npos));
			if (inSponsoredBlock || isMoreInfo) continue;
		}

		SearchResult r;
		r.title = title;
		r.url = href;

		// Resume : variantes Lite (td.result-snippet) et HTML (a.result__snippet).
		size_t snipAt = html.find("result-snippet", pos);
		size_t modernSnipAt = html.find("result__snippet", pos);
		if (modernSnipAt != std::string::npos &&
			(snipAt == std::string::npos || modernSnipAt < snipAt))
			snipAt = modernSnipAt;
		if (snipAt != std::string::npos && snipAt - pos < 4000)
		{
			size_t snipTagStart = html.rfind('<', snipAt);
			size_t snipTagEnd = html.find('>', snipAt);
			if (snipTagStart != std::string::npos && snipTagEnd != std::string::npos)
			{
				size_t nameEnd = html.find_first_of(" />", snipTagStart + 1);
				const std::string tagName = nameEnd == std::string::npos ? "a" :
					toLower(html.substr(snipTagStart + 1, nameEnd - snipTagStart - 1));
				const std::string closing = "</" + tagName + ">";
				size_t snipClose = html.find(closing, snipTagEnd);
				if (snipClose != std::string::npos)
					r.snippet = textOf(html.substr(snipTagEnd + 1,
						snipClose - snipTagEnd - 1));
			}
		}

		// Domaine affiche : variantes Lite et HTML.
		size_t linkAt = html.find("link-text", pos);
		size_t modernLinkAt = html.find("result__url", pos);
		if (modernLinkAt != std::string::npos &&
			(linkAt == std::string::npos || modernLinkAt < linkAt))
			linkAt = modernLinkAt;
		if (linkAt != std::string::npos && linkAt - pos < 3000)
		{
			std::string inner;
			const char* tagName = linkAt == modernLinkAt ? "a" : "span";
			const size_t siteTagStart = html.rfind('<', linkAt);
			if (siteTagStart != std::string::npos &&
				extractElement(html, siteTagStart, tagName, inner))
				r.site = textOf(inner);
		}
		if (r.site.empty() && !r.url.empty())
		{
			// Repli : hote de l'URL.
			size_t s = r.url.find("://");
			size_t b = (s == std::string::npos) ? 0 : s + 3;
			size_t e = r.url.find('/', b);
			r.site = r.url.substr(b, (e == std::string::npos) ? std::string::npos : e - b);
		}

		// Bornage du resume : un texte trop long mange tout l'ecran.
	if (r.snippet.size() > 160)
		{
			r.snippet.resize(160);
			size_t space = r.snippet.find_last_of(' ');
			if (space != std::string::npos && space > 110) r.snippet.resize(space);
			r.snippet += "...";
		}
	if (r.title.size() > 90)
		{
			r.title.resize(90);
			size_t space = r.title.find_last_of(' ');
			if (space != std::string::npos && space > 65) r.title.resize(space);
			r.title += "...";
		}

		out.push_back(r);
	}
}

//--------------------------------------------------------------------------
// Reconstruit la page de resultats en HTML simple, adapte a un ecran etroit.
//
// Le moteur de mise en page n'aime ni les tableaux ni les mises en page
// complexes : on produit donc des blocs simples, un par resultat.
//--------------------------------------------------------------------------
bool searchPageRewrite(const std::string& html, std::string& out,
	const std::string& responseUrl)
{
	g_lastResultCount = 0;
	if (!searchPageDetected(html))
	{
		// DuckDuckGo Lite peut renvoyer sa page d'accueil ou sa navigation au
		// lieu des resultats. Garder une recherche utilisable plutot que de
		// presenter ses liens de navigation comme s'il s'agissait de resultats.
		if ((responseUrl.find("lite.duckduckgo.com/lite") == std::string::npos &&
			responseUrl.find("html.duckduckgo.com/html") == std::string::npos) ||
			searchPageIsVerificationChallenge(html)) return false;
		std::string query;
		const size_t q = responseUrl.find("?q=");
		if (q != std::string::npos)
		{
			const size_t end = responseUrl.find('&', q + 3);
			query = urlDecode(responseUrl.substr(q + 3,
				end == std::string::npos ? end : end - q - 3));
		}
		out = "<html><head><title>DuckDuckGo - recherche</title></head>"
			"<body style=\"background:#FFFFFF;color:#202020\">"
			"<form action=\"nw://search\"><input type=\"search\" name=\"q\" size=\"34\" value=\"" +
			htmlEscape(query) + "\"/><button type=\"submit\" value=\"Rechercher\">Rechercher</button></form>"
			"<p>Resultats indisponibles. Modifie la recherche ou choisis un autre moteur dans Reglages.</p>"
			"</body></html>";
		return true;
	}

	std::vector<SearchResult> results;
	extractResults(html, results);
	if (results.empty()) return false;

	// Titre de la page de resultats, si present.
	std::string pageTitle = "Resultats";
	{
		std::string inner;
		if (extractElement(html, 0, "title", inner))
		{
			const std::string t = textOf(inner);
			if (!t.empty() && t.size() < 120) pageTitle = t;
		}
	}

	std::string h;
	h += "<html><head><title>" + htmlEscape(pageTitle) + "</title></head>";
	h += "<body style=\"background: #FFFFFF; color: #202020\">";

	for (size_t i = 0; i < results.size(); i++)
	{
		const SearchResult& r = results[i];
		h += "<div style=\"margin: 9px 5px\"><div>";
		h += "<a href=\"" + htmlEscape(r.url) + "\" style=\"font-size: 20px; "
		     "font-weight: bold; color: #1645AD\">"
		     + htmlEscape(r.title) + "</a></div>";
		if (!r.site.empty())
			h += "<div style=\"font-size: 15px; color: #333333\">"
			     + htmlEscape(r.site) + "</div>";
		if (!r.snippet.empty())
			h += "<div style=\"font-size: 16px; color: #202020\">"
			     + htmlEscape(r.snippet) + "</div>";
		h += "</div>";
	}
	h += "</body></html>";

	out = h;
	g_lastResultCount = (int)results.size();
	return true;
}
