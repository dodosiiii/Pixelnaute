#include "search.hpp"

//=============================================================================
// Moteurs de recherche
//
// Tous ces moteurs repondent a une simple requete GET avec du HTML sans
// JavaScript. Les variantes "mobile" / "lite" sont privilegiees : elles
// pesent quelques kilo-octets, ce qui compte sur une New 3DS.
//=============================================================================

static const std::vector<SearchEngineInfo> kEngines = {
	{ "duckduckgo-lite", "DuckDuckGo Lite", "duckduckgo.com", true  },
	{ "wikipedia",       "Wikipedia (fr)",  "wikipedia.org",  true  },
	{ "mojeek",          "Mojeek",          "mojeek.com",     true  },
};

const std::vector<SearchEngineInfo>& searchEngines()
{
	return kEngines;
}

const char* defaultSearchEngine()
{
	return "duckduckgo-lite";
}

bool searchEngineKnown(const std::string& engine)
{
	for (const auto& e : kEngines)
		if (engine == e.key) return true;
	return false;
}

//-----------------------------------------------------------------------------
// Resolution d'une cle de moteur : accepte les anciennes clefs et les
// orthographes proches, retombe sur le moteur par defaut sinon.
//-----------------------------------------------------------------------------
static const SearchEngineInfo& resolveEngine(const std::string& engine)
{
	// Anciennes clefs et alias historiques.
	if (engine == "ddg" || engine == "ddg-lite" || engine == "lite")
		return kEngines[0];
	for (const auto& e : kEngines)
		if (engine == e.key) return e;
	return kEngines[0];
}

std::string searchUrl(const std::string& query, const std::string& engine)
{
	const std::string q = urlEncode(query);
	const SearchEngineInfo& e = resolveEngine(engine);

	if (std::string(e.key) == "duckduckgo-lite")
		return "https://lite.duckduckgo.com/lite/?q=" + q;
	if (std::string(e.key) == "wikipedia")
		return "https://fr.m.wikipedia.org/w/index.php?search=" + q;
	if (std::string(e.key) == "mojeek")
		return "https://www.mojeek.com/search?q=" + q;

	return "https://lite.duckduckgo.com/lite/?q=" + q;
}

const char* searchEngineDisplayName(const std::string& engine)
{
	return resolveEngine(engine).name;
}

const char* searchEngineShortName(const std::string& engine)
{
	if (engine == "duckduckgo-lite") return "DuckDuckGo Lite";
	return resolveEngine(engine).name;
}

const char* searchEngineNext(const std::string& engine)
{
	const SearchEngineInfo& current = resolveEngine(engine);
	for (size_t i = 0; i < kEngines.size(); i++)
		if (std::string(kEngines[i].key) == current.key)
			return kEngines[(i + 1) % kEngines.size()].key;
	return kEngines[0].key;
}

//-----------------------------------------------------------------------------
// Adresse ou recherche ?
//-----------------------------------------------------------------------------
static bool isIpv4Address(const std::string& host)
{
	int octets = 0;
	size_t start = 0;
	while (start <= host.size())
	{
		const size_t dot = host.find('.', start);
		const std::string part = host.substr(start,
			dot == std::string::npos ? std::string::npos : dot - start);
		if (part.empty() || part.size() > 3) return false;
		int value = 0;
		for (char c : part)
		{
			if (c < '0' || c > '9') return false;
			value = value * 10 + c - '0';
		}
		if (value > 255) return false;
		++octets;
		if (dot == std::string::npos) break;
		start = dot + 1;
	}
	return octets == 4;
}

bool looksLikeAddress(const std::string& input)
{
	if (input.empty()) return false;
	if (input.find(' ') != std::string::npos) return false;
	const std::string lowerInput = toLower(input);
	if (strStartsWith(lowerInput, "http://") || strStartsWith(lowerInput, "https://") ||
		strStartsWith(lowerInput, "nw://") || strStartsWith(lowerInput, "about:") ||
		strStartsWith(lowerInput, "file://"))
		return true;
	if (input.find('.') == std::string::npos) return false;

	// "exemple.fr", "127.0.0.1", "sous.domaine.org/chemin?x=1"
	std::string host = input;
	size_t cut = host.find_first_of("/?#");
	if (cut != std::string::npos) host = host.substr(0, cut);
	if (host.empty()) return false;
	const size_t colon = host.find(':');
	if (colon != std::string::npos)
	{
		const std::string port = host.substr(colon + 1);
		if (port.empty() || port.find_first_not_of("0123456789") != std::string::npos)
			return false;
		host = host.substr(0, colon);
	}
	if (isIpv4Address(host)) return true;

	// Un point final ou un point sans suite alphanumerique n'est pas un domaine.
	size_t dot = host.rfind('.');
	if (dot + 1 >= host.size()) return false;
	for (size_t i = dot + 1; i < host.size(); i++)
	{
		char c = host[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9')))
			return false;
	}
	// Le TLD doit contenir au moins une lettre ("3.14" n'est pas un domaine).
	bool tldHasLetter = false;
	for (size_t i = dot + 1; i < host.size(); i++)
		if ((host[i] >= 'a' && host[i] <= 'z') || (host[i] >= 'A' && host[i] <= 'Z'))
			tldHasLetter = true;
	return tldHasLetter;
}

std::string resolveInput(const std::string& input, const std::string& engine)
{
	std::string text = strTrim(input);
	if (text.empty()) return "";

	const std::string lowerText = toLower(text);
	if (strStartsWith(lowerText, "http://"))
		return "http://" + text.substr(7);
	if (strStartsWith(lowerText, "https://"))
		return "https://" + text.substr(8);
	if (strStartsWith(lowerText, "nw://"))
		return "nw://" + text.substr(5);
	if (strStartsWith(lowerText, "about:"))
		return "about:" + text.substr(6);
	if (strStartsWith(lowerText, "file://"))
		return "file://" + text.substr(7);

	if (looksLikeAddress(text))
		return "https://" + text;

	return searchUrl(text, engine);
}

//-----------------------------------------------------------------------------
int searchSelfTest()
{
	int fails = 0;

	if (searchUrl("bonjour monde", "duckduckgo-lite")
			.find("bonjour%20monde") == std::string::npos)
		fails++;
	if (searchUrl("test", "moteur-inexistant")
			.find("duckduckgo") == std::string::npos)
		fails++;

	if (std::string(defaultSearchEngine()) != "duckduckgo-lite") fails++;
	if (searchEngineKnown("google") || searchEngineKnown("bing") ||
		searchEngineKnown("startpage")) fails++;
	if (!searchEngineKnown("mojeek") || !searchEngineKnown("wikipedia")) fails++;
	if (std::string(searchEngineNext("duckduckgo-lite")) != "wikipedia") fails++;

	// detection d'adresse
	if (!looksLikeAddress("exemple.fr")) fails++;
	if (!looksLikeAddress("https://exemple.fr/x")) fails++;
	if (!looksLikeAddress("127.0.0.1:8080")) fails++;
	if (looksLikeAddress("bonjour")) fails++;
	if (looksLikeAddress("comment ca va")) fails++;
	if (looksLikeAddress("3.14")) fails++;
	if (looksLikeAddress("exemple.")) fails++;

	// resolution
	if (resolveInput("exemple.fr", "duckduckgo-lite") != "https://exemple.fr") fails++;
	if (resolveInput("bonjour", "duckduckgo-lite").find("lite.duckduckgo.com")
			== std::string::npos) fails++;
	if (resolveInput("  ", "duckduckgo-lite") != "") fails++;
	if (resolveInput("nw://settings", "duckduckgo-lite") != "nw://settings") fails++;
	if (resolveInput("NW://settings", "duckduckgo-lite") != "nw://settings") fails++;
	if (resolveInput("file://sdmc/page.html", "duckduckgo-lite") !=
		"file://sdmc/page.html") fails++;
	if (!looksLikeAddress("HTTPS://Example.com/x")) fails++;
	if (resolveInput("HtTp://Example.com/x", "duckduckgo-lite") !=
		"http://Example.com/x") fails++;

	return fails;
}
