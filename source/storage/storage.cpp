#include "storage.hpp"
#include "../common.hpp"
#include <sys/stat.h>
#include <errno.h>

//--------------------------------------------------------------------------
// helpers fichiers
//--------------------------------------------------------------------------
static bool fileExists(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

static bool readAll(const std::string& path, std::string& out, size_t limit)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;
	char buf[4096];
	out.clear();
	size_t got;
	while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
	{
		if (out.size() + got > limit) { got = limit - out.size(); }
		out.append(buf, got);
		if (out.size() >= limit) break;
	}
	fclose(f);
	return true;
}

//--------------------------------------------------------------------------
// StorageKV
//--------------------------------------------------------------------------
bool StorageKV::load(const std::string& path)
{
	entries.clear();
	std::string data;
	if (!readAll(path, data, 256 * 1024)) return false;

	size_t p = 0;
	while (p < data.size())
	{
		size_t nl = data.find('\n', p);
		std::string line = data.substr(p, (nl == std::string::npos) ? std::string::npos : nl - p);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		size_t eq = line.find('=');
		if (eq != std::string::npos)
			set(strTrim(line.substr(0, eq)), strTrim(line.substr(eq + 1)));
		if (nl == std::string::npos) break;
		p = nl + 1;
	}
	return fileExists(path);
}

bool StorageKV::save(const std::string& path) const
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return false;
	for (const auto& e : entries)
		fprintf(f, "%s=%s\n", e.first.c_str(), e.second.c_str());
	fclose(f);
	return true;
}

std::string StorageKV::get(const std::string& key, const std::string& def) const
{
	for (const auto& e : entries)
		if (e.first == key) return e.second;
	return def;
}

void StorageKV::set(const std::string& key, const std::string& value)
{
	for (auto& e : entries)
		if (e.first == key) { e.second = value; return; }
	entries.push_back({ key, value });
}

//--------------------------------------------------------------------------
// History
//--------------------------------------------------------------------------
bool History::load()
{
	entries.clear();
	FILE* f = fopen(NW_SD_HISTORY, "r");
	if (!f) return false;
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		std::string s = line;
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
		size_t tab = s.find('\t');
		if (tab != std::string::npos)
			entries.push_back({ s.substr(0, tab), s.substr(tab + 1) });
	}
	fclose(f);
	return true;
}

bool History::save() const
{
	FILE* f = fopen(NW_SD_HISTORY, "w");
	if (!f) return false;
	for (const auto& e : entries)
		fprintf(f, "%s\t%s\n", e.url.c_str(), e.title.c_str());
	fclose(f);
	return true;
}

void History::add(const std::string& url, const std::string& title)
{
	bool found = false;
	for (auto& e : entries)
		if (e.url == url) { e.title = title; found = true; break; }
	if (!found)
		entries.insert(entries.begin(), { url, title });
	if ((int)entries.size() > NW_MAX_HISTORY)
		entries.resize(NW_MAX_HISTORY);
	save();
}

void History::clear()
{
	entries.clear();
	save();
}

//--------------------------------------------------------------------------
// Bookmarks
//--------------------------------------------------------------------------
bool Bookmarks::load()
{
	entries.clear();
	FILE* f = fopen(NW_SD_BOOKMARKS, "r");
	if (!f) return false;
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		std::string s = line;
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
		size_t tab = s.find('\t');
		if (tab != std::string::npos)
			entries.push_back({ s.substr(0, tab), s.substr(tab + 1) });
	}
	fclose(f);
	return true;
}

bool Bookmarks::save() const
{
	FILE* f = fopen(NW_SD_BOOKMARKS, "w");
	if (!f) return false;
	for (const auto& e : entries)
		fprintf(f, "%s\t%s\n", e.url.c_str(), e.title.c_str());
	fclose(f);
	return true;
}

void Bookmarks::add(const std::string& url, const std::string& title)
{
	for (const auto& e : entries)
		if (e.url == url) return;
	entries.push_back({ url, title });
	if (entries.size() > 64)
		entries.erase(entries.begin());
	save();
}

bool Bookmarks::remove(const std::string& url)
{
	for (size_t i = 0; i < entries.size(); i++)
		if (entries[i].url == url) { entries.erase(entries.begin() + i); save(); return true; }
	return false;
}

void Bookmarks::clear() { entries.clear(); save(); }

//--------------------------------------------------------------------------
// Initialisation disque
//--------------------------------------------------------------------------
bool storageInit()
{
	::mkdir(NW_SD_ROOT, 0777);
	::mkdir(NW_SD_CACHE, 0777);
	::mkdir(NW_SD_DOWN, 0777);
	const std::string segments = std::string(NW_SD_CACHE) + "/page-segments";
	::mkdir(segments.c_str(), 0777);
	return fileExists(NW_SD_ROOT) || errno == 0;
}

std::string storageUniqueDownloadName(const std::string& filename)
{
	std::string safe;
	for (char c : filename)
	{
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
			safe += '_';
		else if ((unsigned char)c >= 32)
			safe += c;
	}
	if (safe.empty()) safe = "fichier";
	if (safe.size() > 60) safe = safe.substr(safe.size() - 60);

	std::string base = std::string(NW_SD_DOWN) + "/";
	std::string path = base + safe;
	if (!fileExists(path)) return path;
	int i = 1;
	while (i < 1000)
	{
		std::string n = safe + "." + itos(i);
		if (!fileExists(base + n)) return base + n;
		i++;
	}
	return base + safe + ".tmp";
}