#pragma once

#include "../render/bitmap.hpp"
#include <cstdint>
#include <string>
#include <vector>

class PageSegmentCache
{
public:
	struct SiteEntry
	{
		uint64_t key = 0;
		uint64_t bytes = 0;
		int segmentCount = 0;
		std::string url;
		std::string host;
		std::vector<uint64_t> pageKeys;
	};

	static const uint64_t MaxBytes = 1024ULL * 1024ULL * 1024ULL;
	static const uint64_t MaxAgeSeconds = 7ULL * 24ULL * 60ULL * 60ULL;

	bool load(uint64_t pageKey, int segmentIndex, Bitmap& out) const;
	bool store(uint64_t pageKey, int segmentIndex, const Bitmap& bitmap,
		const std::string& pageUrl) const;
	bool usage(uint64_t& bytes) const;
	bool listSites(std::vector<SiteEntry>& sites) const;
	bool clearSite(uint64_t pageKey) const;
	bool clear() const;
	void prune() const;
};
