#include "downloads.hpp"
#include <cstdio>
#include <string>

bool isHtmlContent(const std::string& contentType, const std::string& body)
{
	std::string ct = toLower(contentType);
	if (ct.find("text/html") != std::string::npos) return true;
	if (ct.find("application/xhtml") != std::string::npos) return true;
	if (ct.find("text/") != std::string::npos && ct.find("html") != std::string::npos) return true;
	if (ct.empty() || ct.find("text/plain") != std::string::npos)
	{
		// sniff : debut du corps ressemblant a du HTML
		size_t n = body.size() < 16 ? body.size() : 16;
		for (size_t i = 0; i < n; i++)
			if (body[i] == '<') return true;
	}
	return false;
}

bool downloadsSave(const std::string& body, const std::string& filename,
	std::string& outPath)
{
	std::string path = storageUniqueDownloadName(filename);
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return false;
	size_t written = fwrite(body.data(), 1, body.size(), f);
	int err = fclose(f);
	if (written != body.size() || err != 0)
	{
		remove(path.c_str());
		return false;
	}
	outPath = path;
	return true;
}