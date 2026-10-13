#include "../common.hpp"


//=============================================================================
// Resolution d'une URL relative par rapport a une URL de base
//=============================================================================
std::string urlJoin(const std::string& base, const std::string& refIn)
{
	std::string ref = strTrim(refIn);
	if (ref.empty()) return base;

	static const char* abs[] = {
		"http://", "https://", "mailto:", "data:", "nw://", "about:", "javascript:", nullptr
	};
	for (int i = 0; abs[i]; i++)
		if (strStartsWith(ref, abs[i])) return ref;

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
