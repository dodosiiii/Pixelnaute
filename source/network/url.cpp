#include "../common.hpp"
#include <ctype.h>

//=============================================================================
// Resolution d'une URL relative par rapport a une URL de base
//=============================================================================
static std::string normalizedHttpOrigin(const std::string& url)
{
	const size_t schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) return std::string();

	std::string scheme = toLower(url.substr(0, schemeEnd));
	if (scheme != "http" && scheme != "https") return std::string();

	const size_t authorityStart = schemeEnd + 3;
	size_t authorityEnd = url.find_first_of("/?#", authorityStart);
	if (authorityEnd == std::string::npos) authorityEnd = url.size();
	std::string authority = url.substr(authorityStart, authorityEnd - authorityStart);
	if (authority.empty() || authority.find('@') != std::string::npos)
		return std::string();
	for (unsigned char ch : authority)
		if (ch <= 0x20 || ch == '\\' || ch == '%')
			return std::string();

	std::string host;
	std::string port;
	if (authority[0] == '[')
	{
		const size_t bracket = authority.find(']');
		if (bracket == std::string::npos) return std::string();
		host = authority.substr(0, bracket + 1);
		if (bracket + 1 < authority.size())
		{
			if (authority[bracket + 1] != ':') return std::string();
			port = authority.substr(bracket + 2);
		}
	}
	else
	{
		const size_t colon = authority.find(':');
		if (colon != std::string::npos)
		{
			if (authority.find(':', colon + 1) != std::string::npos)
				return std::string();
			host = authority.substr(0, colon);
			port = authority.substr(colon + 1);
		}
		else host = authority;
	}
	if (host.empty()) return std::string();
	for (char& ch : host) ch = (char)tolower((unsigned char)ch);

	if (!port.empty())
	{
		unsigned value = 0;
		for (unsigned char ch : port)
		{
			if (ch < '0' || ch > '9') return std::string();
			const unsigned digit = ch - '0';
			if (value > (65535u - digit) / 10u) return std::string();
			value = value * 10u + digit;
		}
		if (value == 0) return std::string();
		if ((scheme == "http" && value == 80) ||
			(scheme == "https" && value == 443))
			port.clear();
		else port = std::to_string(value);
	}
	else if (!authority.empty() && authority.back() == ':')
		return std::string();

	return scheme + "://" + host + (port.empty() ? "" : ":" + port);
}

bool urlSameOrigin(const std::string& first, const std::string& second)
{
	const std::string firstOrigin = normalizedHttpOrigin(first);
	return !firstOrigin.empty() && firstOrigin == normalizedHttpOrigin(second);
}

std::string urlJoin(const std::string& base, const std::string& refIn)
{
	std::string ref = strTrim(refIn);
	if (ref.empty()) return base;

	const size_t colon = ref.find(':');
	const size_t pathStart = ref.find_first_of("/?#");
	if (colon != std::string::npos &&
		(pathStart == std::string::npos || colon < pathStart))
	{
		const std::string scheme = toLower(ref.substr(0, colon));
		static const char* absoluteSchemes[] = {
			"http", "https", "mailto", "data", "nw", "about", "javascript", nullptr
		};
		for (int i = 0; absoluteSchemes[i]; ++i)
		{
			if (scheme == absoluteSchemes[i])
				return scheme + ref.substr(colon);
		}
	}

	const size_t se = base.find("://");
	if (se == std::string::npos) return ref;

	const std::string scheme = base.substr(0, se);
	size_t hostEnd = base.find_first_of("/?#", se + 3);
	if (hostEnd == std::string::npos) hostEnd = base.size();
	const std::string origin = base.substr(0, hostEnd);

	if (strStartsWith(ref, "//")) return scheme + ":" + ref; // //hote/chemin
	if (ref[0] == '/') return origin + ref;

	const std::string noFrag = base.substr(0, base.find('#'));
	if (ref[0] == '#') return noFrag + ref;
	if (ref[0] == '?') return noFrag.substr(0, noFrag.find('?')) + ref;

	// chemin relatif : dossier de la page de base + normalisation ./ et ../
	const std::string basePath = noFrag.substr(0, noFrag.find('?')).substr(origin.size());
	const size_t slash = basePath.find_last_of('/');
	const std::string dir = (slash == std::string::npos) ? "/" : basePath.substr(0, slash + 1);

	std::string rel = ref, tail;
	const size_t qpos = rel.find_first_of("?#");
	if (qpos != std::string::npos)
	{
		tail = rel.substr(qpos);
		rel.resize(qpos);
	}

	std::vector<std::string> parts;
	const std::string full = dir + rel;
	std::string seg;
	for (size_t i = 0; i <= full.size(); i++)
	{
		if (i == full.size() || full[i] == '/')
		{
			if (seg == "..")
			{
				if (!parts.empty()) parts.pop_back();
			}
			else if (!seg.empty() && seg != ".")
			{
				parts.push_back(seg);
			}
			seg.clear();
		}
		else
		{
			seg += full[i];
		}
	}

	std::string path = "/";
	for (size_t i = 0; i < parts.size(); i++)
	{
		path += parts[i];
		if (i + 1 < parts.size()) path += "/";
	}
	if (!rel.empty() && rel.back() == '/' && path.back() != '/') path += '/';

	return origin + path + tail;
}
