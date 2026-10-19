#include "page_segment_cache.hpp"
#include "../storage/storage.hpp"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include <vector>

namespace
{
	const char* kCacheDir = NW_SD_CACHE "/page-segments";
	const uint32_t kMagic = 0x50475331;

	struct CacheEntry
	{
		std::string path;
		uint64_t bytes = 0;
		time_t modified = 0;
	};

	std::string segmentPath(uint64_t key, int index, const char* suffix = ".bin")
	{
		char name[48];
		snprintf(name, sizeof(name), "%016llX_%05d%s",
			(unsigned long long)key, index, suffix);
		return std::string(kCacheDir) + "/" + name;
	}

	std::string sitePath(uint64_t key, const char* suffix = ".url")
	{
		char name[32];
		snprintf(name, sizeof(name), "%016llX%s", (unsigned long long)key, suffix);
		return std::string(kCacheDir) + "/" + name;
	}

	bool ensureCacheDir()
	{
		if (::mkdir(NW_SD_CACHE, 0777) != 0 && errno != EEXIST) return false;
		if (::mkdir(kCacheDir, 0777) != 0 && errno != EEXIST) return false;
		struct stat info;
		return stat(kCacheDir, &info) == 0 && S_ISDIR(info.st_mode);
	}

	std::vector<CacheEntry> scanEntries()
	{
		std::vector<CacheEntry> entries;
		DIR* dir = opendir(kCacheDir);
		if (!dir) return entries;
		struct dirent* item = nullptr;
		while ((item = readdir(dir)) != nullptr)
		{
			if (item->d_name[0] == '.') continue;
			CacheEntry entry;
			entry.path = std::string(kCacheDir) + "/" + item->d_name;
			struct stat info;
			if (stat(entry.path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
			entry.bytes = (uint64_t)info.st_size;
			entry.modified = info.st_mtime;
			entries.push_back(entry);
		}
		closedir(dir);
		return entries;
	}

	bool pruneEntries(uint64_t requiredBytes, const std::string& replacing,
		const std::string& replacingMetadata)
	{
		const time_t now = time(nullptr);
		std::vector<CacheEntry> entries;
		const std::vector<CacheEntry> scanned = scanEntries();
		uint64_t total = 0;
		for (const CacheEntry& entry : scanned)
		{
			if (now > 0 && entry.modified > 0 && now >= entry.modified &&
				(uint64_t)(now - entry.modified) > PageSegmentCache::MaxAgeSeconds)
			{
				if (remove(entry.path.c_str()) == 0) continue;
			}
			if (entry.path != replacing &&
				entry.path != replacingMetadata) total += entry.bytes;
			entries.push_back(entry);
		}

		while (total + requiredBytes > PageSegmentCache::MaxBytes && !entries.empty())
		{
			size_t oldest = entries.size();
			for (size_t i = 0; i < entries.size(); ++i)
			{
				if (entries[i].path == replacing ||
					entries[i].path == replacingMetadata ||
					entries[i].bytes == 0) continue;
				if (oldest == entries.size() || entries[i].modified < entries[oldest].modified)
					oldest = i;
			}
			if (oldest == entries.size()) break;
			if (remove(entries[oldest].path.c_str()) != 0)
			{
				entries[oldest].bytes = 0;
				continue;
			}
			total = total >= entries[oldest].bytes ? total - entries[oldest].bytes : 0;
			entries.erase(entries.begin() + oldest);
		}
		return total + requiredBytes <= PageSegmentCache::MaxBytes;
	}
}

bool PageSegmentCache::load(uint64_t pageKey, int segmentIndex, Bitmap& out) const
{
	if (segmentIndex < 0) return false;
	const std::string path = segmentPath(pageKey, segmentIndex);
	FILE* file = fopen(path.c_str(), "rb");
	if (!file) return false;
	uint32_t header[4] = {};
	const bool headerOk = fread(header, sizeof(uint32_t), 4, file) == 4 &&
		header[0] == kMagic && header[1] == NW_PAGE_WIDTH &&
		header[2] > 0 && header[2] <=
			(uint32_t)(NW_PAGE_SEGMENT_HEIGHT + NW_PAGE_SEGMENT_OVERLAP) &&
		header[3] <= (uint32_t)(NW_PAGE_SEGMENT_HEIGHT * NW_MAX_PAGE_SEGMENTS);
	if (!headerOk || !out.create((int)header[1], (int)header[2], (int)header[3]))
	{
		fclose(file);
		remove(path.c_str());
		return false;
	}
	const size_t expected = (size_t)out.w * (size_t)out.h;
	const bool readOk = fread(out.px, sizeof(uint16_t), expected, file) == expected &&
		fgetc(file) == EOF && !ferror(file);
	fclose(file);
	if (!readOk)
	{
		out.destroy();
		remove(path.c_str());
		return false;
	}
	return true;
}

bool PageSegmentCache::store(uint64_t pageKey, int segmentIndex, const Bitmap& bitmap,
	const std::string& pageUrl) const
{
	if (segmentIndex < 0 || !bitmap.px || bitmap.w != NW_PAGE_WIDTH ||
		bitmap.h <= 0 ||
		bitmap.h > NW_PAGE_SEGMENT_HEIGHT + NW_PAGE_SEGMENT_OVERLAP ||
		bitmap.originY < 0 ||
		bitmap.originY > NW_PAGE_SEGMENT_HEIGHT * NW_MAX_PAGE_SEGMENTS)
		return false;
	if (!ensureCacheDir()) return false;
	const uint64_t bytes = sizeof(uint32_t) * 4ULL +
		(uint64_t)bitmap.w * (uint64_t)bitmap.h * sizeof(uint16_t);
	const bool writeMetadata = !pageUrl.empty() && pageUrl.size() <= 4096;
	const uint64_t metadataBytes = writeMetadata
		? sizeof(uint32_t) + (uint64_t)pageUrl.size() : 0;
	if (bytes + metadataBytes > MaxBytes) return false;

	const std::string path = segmentPath(pageKey, segmentIndex);
	const std::string temporary = segmentPath(pageKey, segmentIndex, ".tmp");
	const std::string metadataPath = sitePath(pageKey);
	if (!pruneEntries(bytes + metadataBytes, path,
		writeMetadata ? metadataPath : std::string()))
		return false;

	FILE* file = fopen(temporary.c_str(), "wb");
	if (!file) return false;
	const uint32_t header[4] = {
		kMagic, (uint32_t)bitmap.w, (uint32_t)bitmap.h, (uint32_t)bitmap.originY
	};
	bool ok = fwrite(header, sizeof(uint32_t), 4, file) == 4 &&
		fwrite(bitmap.px, sizeof(uint16_t), (size_t)bitmap.w * bitmap.h, file) ==
			(size_t)bitmap.w * bitmap.h;
	if (fclose(file) != 0) ok = false;
	if (!ok)
	{
		remove(temporary.c_str());
		return false;
	}
	if (writeMetadata)
	{
		const std::string metadataTemp = sitePath(pageKey, ".tmp-url");
		FILE* metadata = fopen(metadataTemp.c_str(), "wb");
		if (!metadata) { remove(temporary.c_str()); return false; }
		const uint32_t urlLength = (uint32_t)pageUrl.size();
		const bool metadataOk = fwrite(&urlLength, sizeof(urlLength), 1, metadata) == 1 &&
			fwrite(pageUrl.data(), 1, pageUrl.size(), metadata) == pageUrl.size();
		if (fclose(metadata) != 0 || !metadataOk)
		{
			remove(metadataTemp.c_str());
			remove(temporary.c_str());
			return false;
		}
		remove(metadataPath.c_str());
		if (rename(metadataTemp.c_str(), metadataPath.c_str()) != 0)
		{
			remove(metadataTemp.c_str());
			remove(temporary.c_str());
			return false;
		}
	}
	remove(path.c_str());
	if (rename(temporary.c_str(), path.c_str()) != 0)
	{
		remove(temporary.c_str());
		return false;
	}
	return true;
}

bool PageSegmentCache::usage(uint64_t& bytes) const
{
	bytes = 0;
	DIR* dir = opendir(kCacheDir);
	if (!dir) return errno == ENOENT;
	bool ok = true;
	struct dirent* item = nullptr;
	while ((item = readdir(dir)) != nullptr)
	{
		if (item->d_name[0] == '.') continue;
		const std::string path = std::string(kCacheDir) + "/" + item->d_name;
		struct stat info;
		if (stat(path.c_str(), &info) != 0) { ok = false; continue; }
		if (S_ISREG(info.st_mode)) bytes += (uint64_t)info.st_size;
	}
	if (closedir(dir) != 0) ok = false;
	return ok;
}

bool PageSegmentCache::listSites(std::vector<SiteEntry>& sites) const
{
	sites.clear();
	DIR* dir = opendir(kCacheDir);
	if (!dir) return errno == ENOENT;
	bool ok = true;
	struct dirent* item = nullptr;
	while ((item = readdir(dir)) != nullptr)
	{
		const std::string name = item->d_name;
		if (name.size() != 26 || name.substr(16, 1) != "_" ||
			name.substr(22) != ".bin") continue;
		char* end = nullptr;
		const std::string keyText = name.substr(0, 16);
		const uint64_t key = strtoull(keyText.c_str(), &end, 16);
		if (!end || *end != '\0') continue;
		const std::string path = std::string(kCacheDir) + "/" + name;
		struct stat info;
		if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
		{
			ok = false;
			continue;
		}
		std::string url;
		const std::string metadataPath = sitePath(key);
		uint64_t metadataBytes = 0;
		struct stat metadataInfo;
		if (stat(metadataPath.c_str(), &metadataInfo) == 0)
		{
			if (!S_ISREG(metadataInfo.st_mode))
				ok = false;
			else
			{
				metadataBytes = (uint64_t)metadataInfo.st_size;
				FILE* metadata = fopen(metadataPath.c_str(), "rb");
				if (!metadata)
					ok = false;
				else
				{
					uint32_t length = 0;
					const bool validLength =
						fread(&length, sizeof(length), 1, metadata) == 1 &&
						length > 0 && length <= 4096;
					if (validLength)
					{
						url.resize(length);
						if (fread(&url[0], 1, length, metadata) != length ||
							fgetc(metadata) != EOF || ferror(metadata))
						{
							url.clear();
							ok = false;
						}
					}
					else
						ok = false;
					fclose(metadata);
				}
			}
		}
		else if (errno != ENOENT)
			ok = false;
		const size_t scheme = url.find("://");
		const size_t hostStart = scheme == std::string::npos ? 0 : scheme + 3;
		const size_t hostEnd = url.find_first_of("/:?#", hostStart);
		std::string host = url.substr(hostStart,
			hostEnd == std::string::npos ? hostEnd : hostEnd - hostStart);
		for (char& ch : host)
			ch = (char)tolower((unsigned char)ch);
		if (host.empty()) host = "unknown";

		size_t index = 0;
		while (index < sites.size() && sites[index].host != host) ++index;
		if (index == sites.size())
		{
			SiteEntry entry;
			entry.key = key;
			entry.url = url;
			entry.host = host;
			sites.push_back(entry);
		}
		SiteEntry& entry = sites[index];
		bool knownKey = false;
		for (uint64_t pageKey : entry.pageKeys)
			if (pageKey == key) { knownKey = true; break; }
		if (!knownKey)
		{
			entry.pageKeys.push_back(key);
			entry.bytes += metadataBytes;
		}
		entry.bytes += (uint64_t)info.st_size;
		++entry.segmentCount;
	}
	if (closedir(dir) != 0) ok = false;
	return ok;
}

bool PageSegmentCache::clearSite(uint64_t pageKey) const
{
	DIR* dir = opendir(kCacheDir);
	if (!dir) return errno == ENOENT;
	bool ok = true;
	std::vector<std::string> paths;
	struct dirent* item = nullptr;
	char prefix[18];
	snprintf(prefix, sizeof(prefix), "%016llX_", (unsigned long long)pageKey);
	while ((item = readdir(dir)) != nullptr)
		if (strncmp(item->d_name, prefix, 17) == 0)
			paths.push_back(std::string(kCacheDir) + "/" + item->d_name);
	if (closedir(dir) != 0) ok = false;
	paths.push_back(sitePath(pageKey));
	for (const std::string& path : paths)
		if (remove(path.c_str()) != 0 && errno != ENOENT) ok = false;
	return ok;
}

bool PageSegmentCache::clear() const
{
	DIR* dir = opendir(kCacheDir);
	if (!dir) return errno == ENOENT;
	bool ok = true;
	std::vector<std::string> paths;
	struct dirent* item = nullptr;
	while ((item = readdir(dir)) != nullptr)
		if (item->d_name[0] != '.')
			paths.push_back(std::string(kCacheDir) + "/" + item->d_name);
	if (closedir(dir) != 0) ok = false;
	for (const std::string& path : paths)
		if (remove(path.c_str()) != 0 && errno != ENOENT) ok = false;
	return ok;
}

void PageSegmentCache::prune() const
{
	if (ensureCacheDir())
		pruneEntries(0, std::string(), std::string());
}
