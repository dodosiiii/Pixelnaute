#include "browser.hpp"
#include "compatibility_page.hpp"
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
#include <sys/stat.h>
#include <dirent.h>
#include <malloc.h>
#include <time.h>
#include <math.h>

static CssEngine g_emptyCss;
static std::string extractReaderHtml(const Document& doc, const std::string& title, bool dark, bool images);

static std::string normalizedHost(const std::string& url)
{
	const size_t scheme = url.find("://");
	if (scheme == std::string::npos) return std::string();
	const size_t end = url.find_first_of("/:?#", scheme + 3);
	std::string host = toLower(url.substr(scheme + 3, end - (scheme + 3)));
	const size_t port = host.find(':');
	if (port != std::string::npos) host.resize(port);
	return host;
}

static bool trackerUrl(const std::string& url)
{
	static const char* domains[] = { "doubleclick.net", "googletagmanager.com",
		"google-analytics.com", "facebook.net", "connect.facebook.net",
		"pixel.facebook.com", "ads-twitter.com", "adservice.google.com",
		"analytics.twitter.com", "hotjar.com", "scorecardresearch.com",
		"quantserve.com", "advertising.com", "adnxs.com", "criteo.com",
		"taboola.com", "outbrain.com" };
	const std::string host = normalizedHost(url);
	for (const char* domain : domains)
	{
		const size_t length = strlen(domain);
		if (host == domain || (host.size() > length &&
			host.compare(host.size() - length, length, domain) == 0 &&
			host[host.size() - length - 1] == '.')) return true;
	}
	return false;
}

static void rememberUnavailableImage(Browser::Tab& tab, const std::string& url)
{
	if (url.empty() || tab.unavailableImages.size() >= 128 ||
		std::find(tab.unavailableImages.begin(), tab.unavailableImages.end(), url) !=
			tab.unavailableImages.end())
		return;
	tab.unavailableImages.push_back(url);
}

static std::string rewriteLightUrl(const std::string& url)
{
	const std::string host = normalizedHost(url);
	std::string rewritten = url;
	const size_t hostStart = url.find("://") + 3;
	const size_t hostEnd = url.find_first_of("/:?#", hostStart);
	if (host == "reddit.com" || host == "www.reddit.com")
		rewritten.replace(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart, "old.reddit.com");
	else if (host == "wikipedia.org" || (host.size() > 14 && host.compare(host.size() - 14, 14, ".wikipedia.org") == 0))
	{
		const std::string lang = host == "wikipedia.org" ? "en" : host.substr(0, host.size() - 14);
		rewritten.replace(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart, lang + ".m.wikipedia.org");
	}
	return rewritten;
}

static int pageViewHeight(const PageModel& page, int zoomPercent = 100)
{
	const int width = page.bmp.w > 0
		? page.bmp.w * 100 / std::max(100, zoomPercent) : NW_PAGE_WIDTH;
	return std::max(1, (NW_TOP_HEIGHT * width + NW_PAGE_WIDTH - 1) / NW_PAGE_WIDTH);
}

static int maxPageScroll(const PageModel& page, int zoomPercent = 100)
{
	const int contentHeight = std::min(page.height,
		(int)NW_MAX_PAGE_CONTENT_HEIGHT);
	return std::max(0, contentHeight - pageViewHeight(page, zoomPercent));
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

struct ResourceCacheEntry
{
	std::string dataPath;
	std::string metaPath;
	std::string etag;
	std::string modified;
};

static ResourceCacheEntry resourceCacheEntry(const std::string& url)
{
	uint64_t hash = 1469598103934665603ULL;
	fingerprintString(hash, url);
	char name[32];
	snprintf(name, sizeof(name), "%08lx%08lx", (unsigned long)(hash >> 32), (unsigned long)hash);
	const std::string dir = std::string(NW_SD_CACHE) + "/resources";
	::mkdir(NW_SD_CACHE, 0777);
	::mkdir(dir.c_str(), 0777);
	ResourceCacheEntry entry;
	entry.dataPath = dir + "/" + name + ".bin";
	entry.metaPath = dir + "/" + name + ".meta";
	FILE* meta = fopen(entry.metaPath.c_str(), "rb");
	if (meta)
	{
		char line[1024];
		if (fgets(line, sizeof(line), meta)) entry.etag = strTrim(line);
		if (fgets(line, sizeof(line), meta)) entry.modified = strTrim(line);
		fclose(meta);
	}
	FILE* data = fopen(entry.dataPath.c_str(), "rb");
	if (!data)
	{
		entry.etag.clear();
		entry.modified.clear();
	}
	else fclose(data);
	for (char& c : entry.etag) if ((unsigned char)c < 32 || c == 127) c = ' ';
	for (char& c : entry.modified) if ((unsigned char)c < 32 || c == 127) c = ' ';
	return entry;
}

static bool readCachedResource(const ResourceCacheEntry& entry, std::string& body, size_t limit)
{
	FILE* file = fopen(entry.dataPath.c_str(), "rb");
	if (!file) return false;
	body.clear();
	char buf[4096];
	size_t got = 0;
	while ((got = fread(buf, 1, sizeof(buf), file)) > 0)
	{
		if (body.size() + got > limit) { fclose(file); body.clear(); return false; }
		body.append(buf, got);
	}
	const bool ok = !ferror(file);
	fclose(file);
	return ok;
}

static bool makeResourceCacheRoom(size_t required, const std::string& replacing)
{
	const size_t maxBytes = 16u * 1024u * 1024u;
	if (required > maxBytes) return false;
	const std::string dirPath = std::string(NW_SD_CACHE) + "/resources";
	DIR* dir = opendir(dirPath.c_str());
	if (!dir) return true;
	struct FileEntry { std::string path; std::string meta; size_t bytes; time_t modified; };
	std::vector<FileEntry> files;
	size_t total = 0;
	struct dirent* item = nullptr;
	while ((item = readdir(dir)) != nullptr)
	{
		const std::string name = item->d_name;
		if (!strEndsWith(name, ".bin")) continue;
		const std::string path = dirPath + "/" + name;
		if (path == replacing) continue;
		struct stat info;
		if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) continue;
		FileEntry entry;
		entry.path = path;
		entry.meta = path.substr(0, path.size() - 4) + ".meta";
		entry.bytes = (size_t)info.st_size;
		entry.modified = info.st_mtime;
		total = entry.bytes > SIZE_MAX - total ? SIZE_MAX : total + entry.bytes;
		if (files.size() < 1024) files.push_back(std::move(entry));
	}
	closedir(dir);
	const auto fitsBudget = [&]() {
		return total <= maxBytes && required <= maxBytes - total;
	};
	if (fitsBudget()) return true;
	std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
		return a.modified < b.modified;
	});
	for (const FileEntry& file : files)
	{
		if (remove(file.path.c_str()) != 0) continue;
		remove(file.meta.c_str());
		total = total > file.bytes ? total - file.bytes : 0;
		if (fitsBudget()) return true;
	}
	return fitsBudget();
}

static void saveCachedResource(const std::string& url, const NetResponse& response)
{
	ResourceCacheEntry entry = resourceCacheEntry(url);
	if (response.etag.empty() && response.lastModified.empty())
	{
		remove(entry.dataPath.c_str());
		remove(entry.metaPath.c_str());
		return;
	}
	if (response.body.empty()) return;
	if (!makeResourceCacheRoom(response.body.size(), entry.dataPath)) return;
	const std::string temp = entry.dataPath + ".tmp";
	FILE* file = fopen(temp.c_str(), "wb");
	if (!file) return;
	const bool saved = fwrite(response.body.data(), 1, response.body.size(), file) == response.body.size() &&
		fflush(file) == 0;
	const bool closed = fclose(file) == 0;
	if (!saved || !closed)
	{
		remove(temp.c_str());
		return;
	}
	remove(entry.dataPath.c_str());
	if (rename(temp.c_str(), entry.dataPath.c_str()) != 0)
	{
		remove(temp.c_str());
		return;
	}
	FILE* meta = fopen(entry.metaPath.c_str(), "wb");
	if (!meta) return;
	fprintf(meta, "%s\n%s\n", response.etag.empty() ? entry.etag.c_str() : response.etag.c_str(),
		response.lastModified.empty() ? entry.modified.c_str() : response.lastModified.c_str());
	fclose(meta);
}

static void restoreCachedResource(const std::string& url, NetResponse& response, size_t limit)
{
	if (!response.notModified) return;
	ResourceCacheEntry entry = resourceCacheEntry(url);
	if (readCachedResource(entry, response.body, limit))
	{
		response.ok = true;
		if (response.etag.empty()) response.etag = entry.etag;
		if (response.lastModified.empty()) response.lastModified = entry.modified;
	}
	else { response.ok = false; response.error = "Ressource modifiee mais cache absent."; }
}

static uint64_t pageFingerprint(const Browser::Tab& tab, int viewportWidth)
{
	uint64_t hash = 1469598103934665603ULL;
	fingerprintBytes(hash, &viewportWidth, sizeof(viewportWidth));
	fingerprintString(hash, tab.url);
	fingerprintBytes(hash, &tab.doc.hoveredNode, sizeof(tab.doc.hoveredNode));
	fingerprintBytes(hash, &tab.doc.focusedNode, sizeof(tab.doc.focusedNode));
	for (const Node& node : tab.doc.nodes)
	{
		const uint8_t type = (uint8_t)node.type;
		fingerprintBytes(hash, &type, sizeof(type));
		fingerprintString(hash, node.tag.c_str());
		fingerprintString(hash, tab.doc.nodeText((int)(&node - tab.doc.nodes.data())));
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
			fingerprintBytes(hash, &selector.lastChild, sizeof(selector.lastChild));
			fingerprintBytes(hash, &selector.onlyChild, sizeof(selector.onlyChild));
			fingerprintBytes(hash, &selector.nthChildSet,
				sizeof(selector.nthChildSet));
			fingerprintBytes(hash, &selector.nthChildA,
				sizeof(selector.nthChildA));
			fingerprintBytes(hash, &selector.nthChildB,
				sizeof(selector.nthChildB));
			fingerprintBytes(hash, &selector.nthOfTypeSet,
				sizeof(selector.nthOfTypeSet));
			fingerprintBytes(hash, &selector.nthOfTypeA,
				sizeof(selector.nthOfTypeA));
			fingerprintBytes(hash, &selector.nthOfTypeB,
				sizeof(selector.nthOfTypeB));
			fingerprintBytes(hash, &selector.root, sizeof(selector.root));
			fingerprintBytes(hash, &selector.hover, sizeof(selector.hover));
			fingerprintBytes(hash, &selector.focus, sizeof(selector.focus));
			fingerprintBytes(hash, &selector.checked, sizeof(selector.checked));
			fingerprintBytes(hash, &selector.disabled, sizeof(selector.disabled));
			for (const std::string& cls : selector.classes) fingerprintString(hash, cls);
			for (const CssAttrSelector& attr : selector.attrs)
			{
				fingerprintString(hash, attr.name);
				fingerprintString(hash, attr.value);
				fingerprintBytes(hash, &attr.operation, sizeof(attr.operation));
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
		fingerprintBytes(hash, &style.marginTopPercent,
			sizeof(style.marginTopPercent) * 4);
		fingerprintBytes(hash, &style.marginTopIsPercent,
			sizeof(style.marginTopIsPercent) * 4);
		fingerprintBytes(hash, &style.paddingTop,
			sizeof(style.paddingTop) * 4);
		fingerprintBytes(hash, &style.paddingTopPercent,
			sizeof(style.paddingTopPercent) * 4);
		fingerprintBytes(hash, &style.paddingTopIsPercent,
			sizeof(style.paddingTopIsPercent) * 4);
		fingerprintBytes(hash, &style.width, sizeof(style.width) * 3);
		fingerprintBytes(hash, &style.height, sizeof(style.height));
		fingerprintBytes(hash, &style.heightPercent,
			sizeof(style.heightPercent));
		fingerprintBytes(hash, &style.heightIsPercent,
			sizeof(style.heightIsPercent));
		fingerprintBytes(hash, &style.minHeightPercent,
			sizeof(style.minHeightPercent));
		fingerprintBytes(hash, &style.minHeightIsPercent,
			sizeof(style.minHeightIsPercent));
		fingerprintBytes(hash, &style.maxHeightPercent,
			sizeof(style.maxHeightPercent));
		fingerprintBytes(hash, &style.maxHeightIsPercent,
			sizeof(style.maxHeightIsPercent));
		fingerprintBytes(hash, &style.displayNone, sizeof(style.displayNone));
		fingerprintBytes(hash, &style.block, sizeof(style.block));
		fingerprintBytes(hash, &style.displayInline, sizeof(style.displayInline));
		fingerprintBytes(hash, &style.displayTable, sizeof(style.displayTable));
		fingerprintBytes(hash, &style.displayTableRow,
			sizeof(style.displayTableRow));
		fingerprintBytes(hash, &style.displayTableCell,
			sizeof(style.displayTableCell));
		fingerprintBytes(hash, &style.verticalAlignMiddle,
			sizeof(style.verticalAlignMiddle));
		fingerprintBytes(hash, &style.bold, sizeof(style.bold));
		fingerprintBytes(hash, &style.italic, sizeof(style.italic));
		fingerprintBytes(hash, &style.underline, sizeof(style.underline));
		fingerprintBytes(hash, &style.whiteSpace, sizeof(style.whiteSpace));
		fingerprintBytes(hash, &style.textTransform, sizeof(style.textTransform));
		fingerprintBytes(hash, &style.letterSpacing, sizeof(style.letterSpacing));
		fingerprintBytes(hash, &style.letterSpacingIsEm,
			sizeof(style.letterSpacingIsEm));
		fingerprintBytes(hash, &style.align, sizeof(style.align));
		fingerprintBytes(hash, &style.mono, sizeof(style.mono));
		fingerprintBytes(hash, &style.listStyleNone, sizeof(style.listStyleNone));
		fingerprintBytes(hash, &style.borderBox, sizeof(style.borderBox));
		fingerprintBytes(hash, &style.flex, sizeof(style.flex));
		fingerprintBytes(hash, &style.flexRow, sizeof(style.flexRow));
		fingerprintBytes(hash, &style.flexWrap, sizeof(style.flexWrap));
		fingerprintBytes(hash, &style.justifyContent, sizeof(style.justifyContent));
		fingerprintBytes(hash, &style.flexGrow, sizeof(style.flexGrow));
		fingerprintBytes(hash, &style.overflowXClip,
			sizeof(style.overflowXClip));
		fingerprintBytes(hash, &style.overflowYClip,
			sizeof(style.overflowYClip));
		fingerprintBytes(hash, &style.positionRelative,
			sizeof(style.positionRelative));
		fingerprintBytes(hash, &style.positionAbsolute,
			sizeof(style.positionAbsolute));
		fingerprintBytes(hash, &style.positionFixed,
			sizeof(style.positionFixed));
		fingerprintBytes(hash, &style.displayInlineBlock,
			sizeof(style.displayInlineBlock));
		fingerprintBytes(hash, &style.floatRight, sizeof(style.floatRight));
		fingerprintBytes(hash, &style.positionLeft,
			sizeof(style.positionLeft) * 4);
		fingerprintBytes(hash, &style.positionLeftPercent,
			sizeof(style.positionLeftPercent) * 4);
		fingerprintBytes(hash, &style.positionLeftIsPercent,
			sizeof(style.positionLeftIsPercent) * 4);
		fingerprintBytes(hash, &style.rowGap, sizeof(style.rowGap) * 2);
		fingerprintBytes(hash, &style.borderWidth, sizeof(style.borderWidth));
		fingerprintBytes(hash, &style.borderColor, sizeof(style.borderColor));
		fingerprintBytes(hash, &style.widthPercent, sizeof(style.widthPercent));
		fingerprintBytes(hash, &style.widthIsPercent, sizeof(style.widthIsPercent));
		fingerprintBytes(hash, &style.minWidthPercent, sizeof(style.minWidthPercent));
		fingerprintBytes(hash, &style.minWidthIsPercent,
			sizeof(style.minWidthIsPercent));
		fingerprintBytes(hash, &style.maxWidthPercent, sizeof(style.maxWidthPercent));
		fingerprintBytes(hash, &style.maxWidthIsPercent,
			sizeof(style.maxWidthIsPercent));
		fingerprintBytes(hash, &style.borderTop.width, sizeof(style.borderTop.width));
		fingerprintBytes(hash, &style.borderTop.color, sizeof(style.borderTop.color));
		fingerprintBytes(hash, &style.borderTop.style, sizeof(style.borderTop.style));
		fingerprintBytes(hash, &style.borderRight.width, sizeof(style.borderRight.width));
		fingerprintBytes(hash, &style.borderRight.color, sizeof(style.borderRight.color));
		fingerprintBytes(hash, &style.borderRight.style, sizeof(style.borderRight.style));
		fingerprintBytes(hash, &style.borderBottom.width, sizeof(style.borderBottom.width));
		fingerprintBytes(hash, &style.borderBottom.color, sizeof(style.borderBottom.color));
		fingerprintBytes(hash, &style.borderBottom.style, sizeof(style.borderBottom.style));
		fingerprintBytes(hash, &style.borderLeft.width, sizeof(style.borderLeft.width));
		fingerprintBytes(hash, &style.borderLeft.color, sizeof(style.borderLeft.color));
		fingerprintBytes(hash, &style.borderLeft.style, sizeof(style.borderLeft.style));
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
			values["verticalAlign"] = style.verticalAlignMiddle ? "middle" : "baseline";
			values["letterSpacing"] = std::to_string(style.letterSpacing) + "px";
			values["position"] = style.positionFixed ? "fixed" :
				(style.positionAbsolute ? "absolute" :
					(style.positionRelative ? "relative" : "static"));
			values["float"] = style.floatRight ? "right" : "none";
			const bool heading = tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
			const bool uaBlock = tag == "html" || tag == "body" || tag == "div" || tag == "p" ||
				tag == "section" || tag == "article" || tag == "header" || tag == "footer" ||
				tag == "main" || tag == "nav" || tag == "ul" || tag == "ol" || tag == "li" ||
				heading || tag == "form" || tag == "pre";
			values["display"] = style.displayNone ? "none" :
				(style.displayTable ? "table" :
					(style.displayTableRow ? "table-row" :
						(style.displayTableCell ? "table-cell" :
							(style.flex ? "flex" :
								(style.displayInline ? "inline" :
									((style.block || uaBlock) ? "block" : "inline"))))));
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

static std::string xmlField(const std::string& xml, const char* tag)
{
	const std::string open = std::string("<") + tag;
	const std::string close = std::string("</") + tag + ">";
	const std::string lower = toLower(xml);
	size_t start = lower.find(toLower(open));
	if (start == std::string::npos) return std::string();
	size_t content = xml.find('>', start);
	if (content == std::string::npos) return std::string();
	++content;
	size_t end = lower.find(toLower(close), content);
	if (end == std::string::npos) return std::string();
	std::string value = xml.substr(content, end - content);
	const std::string cdataStart = "<![CDATA[";
	const size_t cdata = value.find(cdataStart);
	if (cdata != std::string::npos)
	{
		const size_t cdataEnd = value.find("]]>", cdata + cdataStart.size());
		if (cdataEnd != std::string::npos)
			value = value.substr(cdata + cdataStart.size(),
				cdataEnd - cdata - cdataStart.size());
	}
	std::string plain;
	bool inTag = false;
	for (char c : value)
	{
		if (c == '<') inTag = true;
		else if (c == '>') inTag = false;
		else if (!inTag) plain += c;
	}
	const std::pair<const char*, const char*> entities[] = {
		{ "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" },
		{ "&quot;", "\"" }, { "&apos;", "'" }
	};
	for (const auto& entity : entities)
	{
		size_t at = 0;
		while ((at = plain.find(entity.first, at)) != std::string::npos)
		{
			plain.replace(at, strlen(entity.first), entity.second);
			at += strlen(entity.second);
		}
	}
	return strTrim(plain);
}

static std::string rssFeedHtml(const std::string& xml, const std::string& feedUrl)
{
	const std::string lower = toLower(xml);
	const bool rss = lower.find("<rss") != std::string::npos ||
		lower.find("<rdf:rdf") != std::string::npos;
	const bool atom = lower.find("<feed") != std::string::npos;
	if (!rss && !atom) return std::string();
	std::string title = xmlField(xml, "title");
	if (title.empty()) title = "Flux RSS / Atom";
	std::string html = "<html><body style=\"padding:8px;background:#17131A;color:#ECE8E8\"><h2>" +
		htmlEscape(title) + "</h2>";
	const char* itemTag = rss ? "item" : "entry";
	const std::string open = std::string("<") + itemTag;
	const std::string close = std::string("</") + itemTag + ">";
	size_t cursor = 0;
	int count = 0;
	while (count < 40)
	{
		const size_t begin = lower.find(toLower(open), cursor);
		if (begin == std::string::npos) break;
		const size_t body = xml.find('>', begin);
		if (body == std::string::npos) break;
		const size_t end = lower.find(toLower(close), body + 1);
		if (end == std::string::npos) break;
		const std::string item = xml.substr(body + 1, end - body - 1);
		std::string itemTitle = xmlField(item, "title");
		std::string link = xmlField(item, "link");
		if (atom && link.empty())
		{
			const std::string itemLower = toLower(item);
			const size_t linkAt = itemLower.find("<link");
			if (linkAt != std::string::npos)
			{
				size_t href = itemLower.find("href=", linkAt);
				if (href != std::string::npos && href + 5 < item.size())
				{
					size_t quote = href + 5;
					while (quote < item.size() && (item[quote] == ' ' || item[quote] == '\t')) ++quote;
					if (quote < item.size() && (item[quote] == '\"' || item[quote] == '\''))
					{
						const char delimiter = item[quote++];
						const size_t linkEnd = item.find(delimiter, quote);
						if (linkEnd != std::string::npos) link = item.substr(quote, linkEnd - quote);
					}
				}
			}
		}
		std::string description = xmlField(item, rss ? "description" : "summary");
		if (description.empty()) description = xmlField(item, "content");
		if (!itemTitle.empty() || !description.empty())
		{
			html += "<section style=\"padding:6px;margin:5px 0;border:1px solid #59434C\"><h3>";
			if (!link.empty())
				html += "<a href=\"" + htmlEscape(urlJoin(feedUrl, link)) + "\">";
			html += htmlEscape(itemTitle.empty() ? "(sans titre)" : itemTitle);
			if (!link.empty()) html += "</a>";
			html += "</h3>";
			if (description.size() > 900) description.resize(900);
			if (!description.empty()) html += "<p>" + htmlEscape(description) + "</p>";
			html += "</section>";
			++count;
		}
		cursor = end + close.size();
		if (html.size() > 48000) break;
	}
	if (count == 0) return std::string();
	html += "</body></html>";
	return html;
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

bool Browser::verifyTlsFor(const std::string& url) const
{
	if (!set_.verifyTLS) return false;
	const std::string host = normalizedHost(url);
	for (const std::string& exception : tlsSessionExceptions_)
		if (!host.empty() && host == exception) return false;
	return true;
}

bool Browser::initializeJavaScript(Tab& tab)
{
	if (!set_.enableJS || tab.jsContext) return true;
	// QuickJS est plafonne a 4 Mio par page. Garder au moins 8 Mio libres
	// avant de creer un nouveau runtime afin de laisser de la marge au DOM et
	// aux allocations transitoires du parseur.
	if (diagApplicationHeapFree() < 8u * 1024u * 1024u)
	{
		diagBootNote("js_skipped_low_heap", (long)diagApplicationHeapFree());
		return false;
	}
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
	if (!urlSameOrigin(binding->url, url)) return false; // CORS n'est pas encore implémenté.
	if (set_.blockTrackers && trackerUrl(url)) return false;
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
	if (!net_.start(job.url, 256 * 1024, verifyTlsFor(job.url), job.method, job.body,
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
	const bool crossOriginRedirect = !response.url.empty() &&
		!urlSameOrigin(binding->url, response.url);
	const char* error = crossOriginRedirect ? "Cross-origin redirect blocked" :
		(response.tooBig ? "Response body exceeds the fetch size limit" :
			(response.ok ? "" : (response.error.empty() ? "Network request failed" : response.error.c_str())));
	const bool blocked = crossOriginRedirect || response.tooBig;
	const char* responseUrl = crossOriginRedirect ? "" : response.url.c_str();
	const char* body = blocked ? "" : response.body.data();
	const size_t bodyLength = blocked ? 0 : response.body.size();
	const char* contentType = blocked ? "" : response.contentType.c_str();
	const int status = blocked ? 0 : response.status;
	if (job.xhr)
		jsPageResolveXHR(tab.jsRuntime, tab.jsContext, job.promiseId, responseUrl,
			body, bodyLength, contentType, status, error);
	else
		jsPageResolveFetch(tab.jsRuntime, tab.jsContext, job.promiseId,
			responseUrl, body, bodyLength, contentType, status, error);
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
	t.readerOriginal.reset();
	t.pdfDocument.reset();
	t.pdfPath.clear();
	t.pdfPage = 0;
	t.pdfZoomPercent = 100;
	t.scrollX = 0;
	t.jsRequired = false;
	t.doc = Document();
	t.css = CssEngine();
	t.css.setViewportWidth(layoutWidth());
	t.page = PageModel();
	t.pageReady = false;
	t.title.clear();
	t.scrollY = 0;
	t.focusNode = -1;
	t.failedSegmentIndex = -1;
	t.unavailableImages.clear();
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

void Browser::toggleReaderMode()
{
	if (current_ < 0 || current_ >= (int)tabs_.size()) return;
	Tab& tab = tabs_[(size_t)current_];
	if (tab.readerOriginal)
	{
		tab.doc = std::move(*tab.readerOriginal);
		tab.readerOriginal.reset();
		releaseJavaScript(tab);
		tab.jsRequired = false;
		rerenderTab(current_);
		scrollTo(0);
		return;
	}
	if (!tab.pageReady || strStartsWith(toLower(tab.url), "nw://")) return;
	std::string readerHtml = extractReaderHtml(tab.doc, tab.title, set_.darkPages, set_.loadImages);
	Document reader;
	if (!htmlParse(readerHtml, reader)) return;
	tab.readerOriginal.reset(new Document(std::move(tab.doc)));
	tab.doc = std::move(reader);
	releaseJavaScript(tab);
	tab.jsRequired = false;
	rerenderTab(current_);
	scrollTo(0);
}

void Browser::openUrl(const std::string& url, bool record)
{
	clearStyleQueue();
	clearImageQueue();
	std::string targetUrl = url;
	if (targetUrl.empty()) return;
	// Fragment-only/same-document links scroll locally without refetching.
	const size_t fragment = targetUrl.find('#');
	if (fragment != std::string::npos && current_ >= 0 && current_ < (int)tabs_.size())
	{
		const std::string currentBase = tabs_[current_].url.substr(0, tabs_[current_].url.find('#'));
		const std::string targetBase = targetUrl.substr(0, fragment);
		if ((targetBase.empty() || targetBase == currentBase) && tabs_[current_].pageReady)
		{
			const std::string id = targetUrl.substr(fragment + 1);
			if (id.empty()) { scrollTo(0); return; }
			for (const AnchorBox& anchor : tabs_[current_].page.anchors)
				if (anchor.name == id) { scrollTo(anchor.y); return; }
			return;
		}

	}
	if (set_.lightUrls && strStartsWith(toLower(targetUrl), "http")) targetUrl = rewriteLightUrl(targetUrl);
	cancelTabRequest(current_);
	if (strStartsWith(toLower(targetUrl), "nw://"))
	{
		if (toLower(targetUrl) == "nw://home" || toLower(targetUrl) == "nw:/home") { showInternalHome(); return; }
		showInternal(targetUrl);
		return;
	}
	pendingTlsHost_.clear();
	Tab& t = currentTab();
	std::string prevUrl = t.url;
	resetTab(t);
	loadUrlInto(current_, targetUrl, record, "GET", "", prevUrl);
}

bool Browser::openLocalPdf(const std::string& path, std::string& error)
{
	error.clear();
	clearStyleQueue();
	clearImageQueue();
	clearScriptQueue();
	cancelTabRequest(current_);
	responsePending_ = false;
	layoutPending_ = false;
	Tab& tab = currentTab();
	resetTab(tab);
	tab.pdfPath = path;
	tab.pdfPage = 0;
	tab.pdfZoomPercent = 100;
	tab.url = "file://" + path;
	const size_t slash = path.find_last_of("/\\");
	tab.title = slash == std::string::npos ? path : path.substr(slash + 1);
	tab.pageReady = false;
	pendingPdfPath_ = path;
	pendingPdfTab_ = current_;
	progressStatus_ = "Ouverture du PDF...";
	return true;
}

bool Browser::processPendingPdfOpen()
{
	if (pendingPdfPath_.empty() || pendingPdfTab_ < 0 ||
		pendingPdfTab_ >= (int)tabs_.size())
		return false;
	const std::string path = std::move(pendingPdfPath_);
	const int tabIndex = pendingPdfTab_;
	pendingPdfPath_.clear();
	pendingPdfTab_ = -1;
	PdfDocument document;
	std::string error;
	if (!pdfReadFile(path, document, error))
	{
		const std::string html =
			"<html><body><h2>Impossible d'ouvrir le PDF</h2><p>" +
			htmlEscape(error) + "</p></body></html>";
		renderInternal(tabIndex, html);
		progressStatus_.clear();
		return true;
	}
	Tab& tab = tabs_[(size_t)tabIndex];
	tab.pdfDocument.reset(new PdfDocument(std::move(document)));
	renderInternal(tabIndex, pdfDocumentHtml(*tab.pdfDocument, tab.title, 0,
		tab.pdfZoomPercent));
	if (!tab.pageReady)
	{
		tab.pdfDocument.reset();
		renderInternal(tabIndex,
			"<html><body><h2>Impossible d'afficher cette page PDF</h2></body></html>");
	}
	progressStatus_.clear();
	return true;
}

bool Browser::cyclePdfZoom()
{
	Tab& tab = currentTab();
	if (!tab.pdfDocument) return false;
	static const int levels[] = { 100, 125, 150, 200, 300 };
	size_t next = 0;
	for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i)
	{
		if (levels[i] != tab.pdfZoomPercent) continue;
		next = (i + 1) % (sizeof(levels) / sizeof(levels[0]));
		break;
	}
	tab.pdfZoomPercent = levels[next];
	tab.scrollX = 0;
	tab.scrollY = 0;
	renderInternal(current_, pdfDocumentHtml(*tab.pdfDocument, tab.title,
		tab.pdfPage, tab.pdfZoomPercent));
	return true;
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
	trimInactiveTabs();
	if (record)
	{
		if (t.back.empty() || t.back.back() != url)
			t.back.push_back(url);
		t.fwd.clear();
	}
	requestTab_ = tabIdx;
	const size_t bodyLimit = safePageBodyLimit();
	diagBootMemory("budget_requete");
	diagBootNote("page_body_budget", (long)bodyLimit);
	if (bodyLimit == 0)
	{
		pendingResponse_.url = url;
		pendingResponse_.error = "Memoire disponible insuffisante pour charger une nouvelle page. Fermez un onglet puis reessayez.";
		responsePending_ = true;
		layoutPending_ = true;
		return;
	}
	if (!net_.start(url, bodyLimit, verifyTlsFor(url), method, postData, NetRequestType::Page, 30000, referer, csrfToken))
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
	t.unavailableImages.clear();
	t.pageReady = false;
	diagStage("fetch");
	cancelTabRequest(current_);
	requestTab_ = current_;
	const size_t bodyLimit = safePageBodyLimit();
	diagBootMemory("budget_rechargement");
	diagBootNote("page_body_budget", (long)bodyLimit);
	if (bodyLimit == 0)
	{
		pendingResponse_.url = t.url;
		pendingResponse_.error = "Memoire disponible insuffisante pour recharger cette page.";
		responsePending_ = true;
		layoutPending_ = true;
		return;
	}
	if (!net_.start(t.url, bodyLimit, verifyTlsFor(t.url)))
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
		{ "Test affichage", "nw://display-test" },
		{ "Reglages",   "nw://settings" },
	};

	std::string h;
	h = "<html><head><title>Pixelnaute</title></head><body "
		"style=\"background: #10182D; color: #F4F0EF\">";

	// --- en-tete ---
	h += "<div style=\"text-align: center; margin-top: 22px; margin-bottom: 3px\">"
	     "<span style=\"font-size: 36px; font-weight: bold; color: #F4F0EF\">PIXELNAUTE</span></div>";
	h += "<div style=\"text-align: center; font-size: 13px; color: #A9BCD8; "
	     "margin-bottom: 18px\">Navigateur web pour New Nintendo 3DS</div>";


	// --- barre de recherche ---
	h += "<form action=\"nw://search\">";
	h += "<div style=\"background: #1B2B4B; margin-left: 10px; margin-right: 10px; "
	     "padding: 10px\">";
	h += "<input type=\"search\" name=\"q\" size=\"40\" value=\"\" "
	     "style=\"font-size: 19px; color: #10182D\">";
	h += "</div>";
	h += "<div style=\"text-align: center; margin-top: 10px\">"
	     "<button type=\"submit\" value=\"Rechercher\">Rechercher sur le Web</button></div>";
	h += "</form>";

	h += "<div style=\"text-align: center; font-size: 12px; color: #A9BCD8; "
	     "margin-top: 6px; margin-bottom: 16px\">"
	     "Moteur : " + std::string(searchEngineDisplayName(engine)) +
	     " &middot; touchez le champ pour taper</div>";

	// --- raccourcis ---
	h += "<div style=\"background: #172544; padding: 10px; margin-left: 6px; "
	     "margin-right: 6px\">";
	h += "<div style=\"font-size: 14px; font-weight: bold; color: #F2A25C; "
	     "margin-bottom: 7px\">RACCOURCIS</div>";
	for (const auto& s : kShortcuts)
	{
		h += "<a href=\"" + htmlEscape(s.url) + "\" style=\"display: block; "
		     "background: #22365C; padding: 9px; margin-bottom: 5px; "
		     "font-size: 17px; color: #F4F0EF\">" +
		     htmlEscape(s.label) + "</a>";
	}
	h += "</div>";

	// --- favoris ---
	h += "<div style=\"background: #172544; padding: 10px; margin: 10px 6px 6px 6px\">";
	h += "<div style=\"font-size: 14px; font-weight: bold; color: #F2A25C; "
	     "margin-bottom: 7px\">FAVORIS</div>";
	if (bookmarks_.entries.empty())
		h += "<div style=\"font-size: 14px; color: #A9BCD8\">"
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
			     "background: #22365C; padding: 8px; margin-bottom: 5px; "
			     "font-size: 16px; color: #F4F0EF\">" +
			     htmlEscape(label) + "</a>";
		}
	}
	h += "</div>";

	// --- visites recentes ---
	h += "<div style=\"background: #172544; padding: 10px; margin: 10px 6px 6px 6px\">";
	h += "<div style=\"font-size: 14px; font-weight: bold; color: #F2A25C; margin-bottom: 7px\">RECENTS</div>";
	if (history_.entries.empty())
		h += "<div style=\"font-size: 14px; color: #A9BCD8\">Aucune page recente.</div>";
	else
	{
		for (size_t i = history_.entries.size(); i > 0 && i + 5 > history_.entries.size(); --i)
		{
			const auto& entry = history_.entries[i - 1];
			if (entry.url.empty() || strStartsWith(toLower(entry.url), "nw://")) continue;
			const std::string label = entry.title.empty() ? entry.url : entry.title;
			h += "<a href=\"" + htmlEscape(entry.url) + "\" style=\"display:block; background:#22365C; padding:7px; margin-bottom:4px; font-size:14px; color:#F4F0EF\">" +
				htmlEscape(label) + "</a>";
		}
	}
	h += "</div>";

	// --- outils internes ---
	h += "<div style=\"text-align: center; margin: 14px 6px 24px 6px\">";
	h += "<a href=\"nw://bookmarks\" style=\"color: #F2A25C; font-size: 15px\">Favoris</a>";
	h += "<span style=\"color: #A9BCD8\"> &middot; </span>";
	h += "<a href=\"nw://history\" style=\"color: #F2A25C; font-size: 15px\">Historique</a>";
	h += "<span style=\"color: #A9BCD8\"> &middot; </span>";
	h += "<a href=\"nw://settings\" style=\"color: #F2A25C; font-size: 15px\">Reglages</a>";
	h += "<span style=\"color: #A9BCD8\"> &middot; </span>";
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
	tab.css = CssEngine();
	tab.css.setViewportWidth(layoutWidth(tab));
	collectStyleBlocks(d, tab.css);
	renderInto(tabIdx, std::move(d));
}

// Intercepte les URL internes "nw://..." (menu / actions).
bool Browser::showInternal(const std::string& url)
{
	std::string u = toLower(url);
	if (u.rfind("nw://pdf-page", 0) == 0)
	{
		Tab& tab = currentTab();
		if (!tab.pdfDocument || tab.pdfDocument->pages.empty()) return false;
		const size_t query = url.find("page=");
		if (query == std::string::npos) return false;
		const int page = atoi(url.c_str() + query + 5);
		if (page < 1 || page > (int)tab.pdfDocument->pages.size()) return false;
		tab.pdfPage = (size_t)(page - 1);
		tab.scrollY = 0;
		tab.scrollX = 0;
		renderInternal(current_, pdfDocumentHtml(*tab.pdfDocument,
			tab.title, tab.pdfPage, tab.pdfZoomPercent));
		return true;
	}
	if (u == "nw://display-test")
	{
		clearStyleQueue();
		clearImageQueue();
		clearScriptQueue();
		cancelTabRequest(current_);
		responsePending_ = false;
		layoutPending_ = false;
		pendingResponse_ = NetResponse();
		progressStatus_.clear();
		Tab& tab = currentTab();
		resetTab(tab);
		tab.url = "nw://display-test";
		renderInternal(current_, internalCompatibilityPageHtml());
		return true;
	}
	if (u.rfind("nw://tls-allow", 0) == 0)
	{
		const size_t query = url.find("url=");
		if (query == std::string::npos || pendingTlsHost_.empty()) return false;
		const std::string target = urlDecode(url.substr(query + 4));
		const std::string host = normalizedHost(target);
		if (!strStartsWith(toLower(target), "https://") || host.empty() ||
			host != pendingTlsHost_) return false;
		if (std::find(tlsSessionExceptions_.begin(), tlsSessionExceptions_.end(), host) ==
			tlsSessionExceptions_.end())
		{
			if (tlsSessionExceptions_.size() >= 16) tlsSessionExceptions_.erase(tlsSessionExceptions_.begin());
			tlsSessionExceptions_.push_back(host);
		}
		pendingTlsHost_.clear();
		openUrl(target);
		return true;
	}
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

bool Browser::downloadUrl(const std::string& url)
{
	if (net_.state() != NetState::Idle || url.empty() ||
		!(strStartsWith(toLower(url), "http://") ||
		  strStartsWith(toLower(url), "https://")))
		return false;
	const std::string cleanUrl = url.substr(0, url.find_first_of("?#"));
	const size_t slash = cleanUrl.find_last_of('/');
	std::string filename = slash == std::string::npos
		? std::string() : cleanUrl.substr(slash + 1);
	if (filename.empty()) filename = "download.bin";
	const std::string path = storageUniqueDownloadName(filename);
	requestTab_ = current_;
	progressStatus_ = "Telechargement du fichier...";
	if (!net_.start(url, 32 * 1024 * 1024, verifyTlsFor(url), "GET", "",
		NetRequestType::Download, 120000, currentTab().url, "", path))
	{
		requestTab_ = -1;
		progressStatus_ = "Impossible de demarrer le telechargement.";
		return false;
	}
	return true;
}

// -------------------------------------------------------------------------
void Browser::newTab(const std::string& url)
{
	if ((int)tabs_.size() >= NW_MAX_TABS) return;
	tabs_.push_back(Tab());
	current_ = (int)tabs_.size() - 1;
	trimInactiveTabs();
	if (!url.empty())
		openInput(url);
	else goHome();
}

void Browser::closeTab(int index)
{
	if (index < 0 || index >= (int)tabs_.size()) return;
	if ((int)tabs_.size() <= 1) return;
	clearStyleQueue();
	clearScriptQueue();
	if (segmentTab_ == index)
	{
		segmentPending_ = false;
		segmentTab_ = -1;
		segmentIndexPending_ = -1;
		pendingScrollY_ = -1;
		layoutPending_ = false;
	}
	else if (segmentTab_ > index) --segmentTab_;
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
	const bool closingCurrent = current_ == index;
	tabs_.erase(tabs_.begin() + index);
	if (current_ > index) --current_;
	else if (closingCurrent) current_ = std::min(index, (int)tabs_.size() - 1);
	if (!tabs_[(size_t)current_].pageReady && !tabs_[(size_t)current_].doc.empty())
		rerenderTab(current_);
	trimInactiveTabs();
}

void Browser::selectTab(int index)
{
	if (index >= 0 && index < (int)tabs_.size() && current_ != index)
	{
		clearStyleQueue();
		clearImageQueue();
		clearScriptQueue();
		current_ = index;
		if (!tabs_[(size_t)current_].pageReady && !tabs_[(size_t)current_].doc.empty())
			rerenderTab(current_);
		trimInactiveTabs();
	}
}

void Browser::nextTab()
{
	if (tabs_.empty()) return;
	clearStyleQueue();
	clearImageQueue();
	clearScriptQueue();
	current_ = (current_ + 1) % (int)tabs_.size();
	if (!tabs_[(size_t)current_].pageReady && !tabs_[(size_t)current_].doc.empty())
		rerenderTab(current_);
	trimInactiveTabs();
}

void Browser::prevTab()
{
	if (tabs_.empty()) return;
	clearStyleQueue();
	clearImageQueue();
	clearScriptQueue();
	current_ = (current_ == 0) ? (int)tabs_.size() - 1 : current_ - 1;
	if (!tabs_[(size_t)current_].pageReady && !tabs_[(size_t)current_].doc.empty())
		rerenderTab(current_);
	trimInactiveTabs();
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
	bool ok = pageSegmentCache_.clear();
	const std::string dirPath = std::string(NW_SD_CACHE) + "/resources";
	DIR* dir = opendir(dirPath.c_str());
	if (dir)
	{
		struct dirent* item = nullptr;
		while ((item = readdir(dir)) != nullptr)
		{
			if (item->d_name[0] == '.') continue;
			const std::string path = dirPath + "/" + item->d_name;
			struct stat info;
			if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
				remove(path.c_str()) != 0) ok = false;
		}
		closedir(dir);
		if (rmdir(dirPath.c_str()) != 0) ok = false;
	}
	return ok;
}

size_t Browser::safePageBodyLimit() const
{
	// Le corps HTTP reste en RAM pendant que le parseur construit le DOM et le
	// layout alloue son bitmap. Garder une marge pour ces copies et les buffers
	// de libctru/curl au lieu d'accepter une page qui ferait echouer newlib.
	const size_t reserve = 10u * 1024u * 1024u;
	const size_t freeBytes = diagApplicationHeapFree();
	if (freeBytes <= reserve + 64u * 1024u) return 0;
	const size_t budget = freeBytes - reserve;
	return std::min((size_t)std::max(0, set_.maxHtmlBytes), budget);
}

void Browser::trimInactiveTabs()
{
	for (int i = 0; i < (int)tabs_.size(); ++i)
	{
		if (i == current_) continue;
		Tab& tab = tabs_[(size_t)i];
		// Le mode lecteur garde l'original pour un retour immediat; une fois
		// l'onglet en arriere-plan, liberer cette seconde copie du DOM.
		tab.readerOriginal.reset();
		if (!tab.pageReady && !tab.page.bmp.px) continue;
		// Les onglets de fond gardent leur DOM et leur URL, mais leur bitmap et
		// leurs hitboxes seront reconstruits au retour. Chaque bitmap peut
		// consommer jusqu'a 1,8 Mio sur le tas application.
		tab.page = PageModel();
		tab.pageReady = false;
		tab.renderedDomVersion = 0;
		tab.failedSegmentIndex = -1;
	}
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
				restoreCachedResource(cssRequestUrl_, pendingResponse_, 64 * 1024);
				if (pendingResponse_.ok) saveCachedResource(cssRequestUrl_, pendingResponse_);
				onStylesheetResponse(pendingResponse_);
				cssRequestUrl_.clear();
				pendingResponse_ = NetResponse();
				return;
			}
			if (pendingResponse_.requestType == NetRequestType::Script)
			{
				restoreCachedResource(scriptRequestUrl_, pendingResponse_, 256 * 1024);
				if (pendingResponse_.ok) saveCachedResource(scriptRequestUrl_, pendingResponse_);
				onScriptResponse(pendingResponse_);
				pendingResponse_ = NetResponse();
				return;
			}
			if (pendingResponse_.requestType == NetRequestType::Image)
			{
				const int tabIdx = imageRequestTab_;
				bool imageStored = false;
				if (tabIdx >= 0 && tabIdx == current_ && tabIdx < (int)tabs_.size() &&
					tabs_[tabIdx].url == imageRequestReferer_ && pendingResponse_.ok)
				{
					restoreCachedResource(imageRequestUrl_, pendingResponse_, 512 * 1024);
					if (pendingResponse_.ok) saveCachedResource(imageRequestUrl_, pendingResponse_);
					if (images_.storeDownloaded(imageRequestUrl_, pendingResponse_.body))
					{
						imageStored = true;
						imageRedrawPending_ = true;
					}
				}
				if (!imageStored && tabIdx >= 0 && tabIdx < (int)tabs_.size() &&
					tabs_[tabIdx].url == imageRequestReferer_)
					rememberUnavailableImage(tabs_[tabIdx], imageRequestUrl_);
				imageRequestTab_ = -1;
				imageRequestUrl_.clear();
				imageRequestReferer_.clear();
				if (imageStored && tabIdx == current_ && tabIdx >= 0 && tabIdx < (int)tabs_.size() &&
					tabs_[tabIdx].pageReady && set_.loadImages)
					queueImages(tabIdx, 6);
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
			const ResourceCacheEntry cache = resourceCacheEntry(request.url);
			if (!net_.start(request.url, 256 * 1024, verifyTlsFor(request.url), "GET", "",
				NetRequestType::Script, 8000, request.referer, "", "",
				cache.etag, cache.modified))
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
			if (url.size() > 2048) continue;
			if (set_.blockTrackers && trackerUrl(url)) continue;
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
	// Le premier rendu est deja visible : ne pas laisser une feuille CSS lente
	// retarder indéfiniment les scripts, les images et la navigation.
	if (cssQueueTab_ == current_ && cssStartMs_ &&
		timeMs() - cssStartMs_ >= 2000 &&
		(cssRequestTab_ >= 0 || !cssQueue_.empty()))
	{
		const int tabIdx = cssQueueTab_;
		const std::string pageUrl = cssPageUrl_;
		const bool needsLayout = cssLayoutPending_;
		clearStyleQueue();
		if (needsLayout && tabIdx >= 0 && tabIdx == current_ &&
			tabIdx < (int)tabs_.size() && tabs_[(size_t)tabIdx].url == pageUrl)
		{
			const int savedScrollY = tabs_[(size_t)tabIdx].scrollY;
			rerenderTab(tabIdx);
			tabs_[(size_t)tabIdx].scrollY = savedScrollY;
			styleRedrawPending_ = true;
		}
		return redraw || needsLayout;
	}
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
	const ResourceCacheEntry cache = resourceCacheEntry(request.url);
	if (!net_.start(request.url, 64 * 1024, verifyTlsFor(request.url), "GET", "",
		NetRequestType::Stylesheet, 5000, request.referer, "", "",
		cache.etag, cache.modified))
	{
		if (cssQueue_.empty()) cssQueueTab_ = -1;
		return redraw;
	}
	cssRequestOrder_ = request.sourceOrder;
	cssRequestTab_ = current_;
	cssRequestUrl_ = request.url;
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
		if (url.size() > 2048 || cssQueue_.size() >= 24) continue;
		if (set_.blockTrackers && trackerUrl(url)) continue;
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
	const ResourceCacheEntry cache = resourceCacheEntry(url);
	if (net_.start(url, 512 * 1024, verifyTlsFor(url), "GET", "",
		NetRequestType::Image, 6000, pendingImagesReferer_, "", "",
		cache.etag, cache.modified))
	{
		imageRequestTab_ = current_;
		imageRequestUrl_ = url;
		imageRequestReferer_ = pendingImagesReferer_;
		diagStage("image fetch");
	}
	else if (current_ >= 0 && current_ < (int)tabs_.size())
		rememberUnavailableImage(tabs_[(size_t)current_], url);
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
	diagBootNote("app_heap_free_response", (long)diagApplicationHeapFree());
	diagBootNote("linear_mem_free_response", (long)linearSpaceFree());
	diagBootMemory("reponse");
	const int tabIdx = requestTab_;
	requestTab_ = -1;
	if (tabIdx < 0 || tabIdx >= (int)tabs_.size()) return;
	Tab& t = tabs_[tabIdx];
	if (resp.requestType == NetRequestType::Download)
	{
		Document d;
		std::string html = "<html><body style=\"padding:12px;background:#17131A;color:#ECE8E8\"><h2>";
		if (resp.ok && !resp.downloadPath.empty())
			html += "Telechargement termine</h2><p>Fichier enregistre sur la carte SD :</p><p>" +
				htmlEscape(resp.downloadPath) + "</p>";
		else
			html += "Echec du telechargement</h2><p>" + htmlEscape(resp.error.empty()
				? "Le fichier n'a pas pu etre enregistre." : resp.error) + "</p>";
		html += "</body></html>";
		if (htmlParse(html, d)) renderInto(tabIdx, std::move(d));
		return;
	}
	// Ne pas conserver le DOM precedent pendant le parsing de sa replacement.
	if (resp.ok) resetTab(t);

	// Les images decodes peuvent etre evincees avant un gros parse. Si le tas
	// application reste trop serre, abandonner proprement le document au lieu
	// de laisser l'allocateur newlib appeler le gestionnaire fatal.
	if (resp.ok && diagApplicationHeapFree() < 8u * 1024u * 1024u)
	{
		images_.clear();
		trimInactiveTabs();
		for (int i = 0; i < (int)tabs_.size(); ++i)
			if (i != tabIdx) releaseJavaScript(tabs_[(size_t)i]);
		diagBootNote("page_heap_after_pressure_cleanup",
			(long)diagApplicationHeapFree());
		diagBootMemory("nettoyage_memoire");
		if (diagApplicationHeapFree() < 6u * 1024u * 1024u)
		{
			diagBootNote("page_rejected_body_bytes", (long)resp.body.size());
			diagBootNote("page_rejected_heap_free",
				(long)diagApplicationHeapFree());
			std::string().swap(resp.body);
			resetTab(t);
			Document lowMemory;
			const std::string html = "<html><body><h2>Memoire faible</h2><p>Cette page est trop lourde pour la memoire disponible. Fermez un onglet ou desactivez les images puis reessayez.</p></body></html>";
			if (htmlParse(html, lowMemory)) renderInto(tabIdx, std::move(lowMemory));
			else
			{
				t.page = PageModel();
				t.page.bmp.create(layoutWidth(), NW_TOP_HEIGHT * layoutWidth() / NW_PAGE_WIDTH);
				if (t.page.bmp.px) t.page.bmp.clear(C_BG);
				t.page.height = pageViewHeight(t.page);
				t.pageReady = t.page.bmp.px != nullptr;
			}
			progressStatus_ = "Memoire faible : page non chargee";
			diagBootNote("page_rejected_low_heap", 1);
			return;
		}
	}

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
			pendingTlsHost_ = resp.certError ? normalizedHost(resp.url) : std::string();
			h += "<p style=\"margin-bottom: 6px\">Connexion securisee impossible.</p>"
			     "<p style=\"color: #B0A4A6; margin-bottom: 16px\">Ce site utilise un certificat ou une version TLS "
			     "incompatible avec le composant reseau de la New 3DS. La verification du certificat n'a pas "
			     "ete contournee automatiquement. Verifiez la date de la console ou modifiez le reglage TLS "
			     "dans les parametres, en connaissance des risques.</p>";
			if (resp.certError && !pendingTlsHost_.empty())
				h += "<p><a href=\"nw://tls-allow?url=" + urlEncode(resp.url) +
					"\" style=\"color:#F2A25C\">Continuer pour cet hote pendant cette session : " +
					htmlEscape(pendingTlsHost_) + " (risque de securite)</a></p>";
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

	if (toLower(resp.contentType).find("xml") != std::string::npos)
	{
		const std::string feedHtml = rssFeedHtml(resp.body, resp.url);
		if (!feedHtml.empty())
		{
			Document feedDoc;
			if (htmlParse(feedHtml, feedDoc))
			{
				t.url = resp.url;
				t.scrollY = 0;
				t.css = CssEngine();
				t.css.setViewportWidth(layoutWidth());
				collectStyleBlocks(feedDoc, t.css);
				renderInto(tabIdx, std::move(feedDoc));
				history().add(t.url, t.title);
				return;
			}
		}
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
	diagBootMemory("avant_parse_recherche");
	if (searchPageRewrite(resp.body, rewritten, t.url))
	{
		Document dr;
		diagBootMemory("avant_parse_resultats");
		if (htmlParse(rewritten, dr))
		{
			diagBootMemory("apres_parse_resultats");
			std::string().swap(resp.body);
			t.url = resp.url;
			t.scrollY = 0;
			t.css = CssEngine();
			t.css.setViewportWidth(layoutWidth());
			diagStage("css parse");
			diagBootMemory("avant_css_resultats");
			collectStyleBlocks(dr, t.css);
			diagBootMemory("apres_css_resultats");
			diagStage("css ready");
			renderInto(tabIdx, std::move(dr));
			history().add(t.url, t.title);
			return;
		}
	}
	std::string().swap(rewritten);

	Document d;
	diagStage("html parse start");
	diagBootMemory("avant_parse_html");
	diagBootNote("app_heap_free_before_parse", (long)diagApplicationHeapFree());
	diagBootNote("linear_mem_free_before_parse", (long)linearSpaceFree());
	const bool parsed = htmlParse(resp.body, d);
	diagBootMemory("apres_parse_html");
	diagBootNote("page_parse_ok", parsed ? 1 : 0);
	diagBootNote("page_dom_nodes", (long)d.nodes.size());
	diagBootNote("app_heap_free_after_parse", (long)diagApplicationHeapFree());
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
	diagBootMemory("avant_css");
	diagBootNote("app_heap_free_before_css", (long)diagApplicationHeapFree());
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
				cssInputBytes += text.textLength;
		}
	}
	diagBootNote("page_css_blocks", (long)cssBlockCount);
	diagBootNote("page_css_input_bytes", (long)cssInputBytes);
	t.css = CssEngine();
	t.css.setViewportWidth(layoutWidth());
	collectStyleBlocks(d, t.css);

	// Les feuilles externes sont chargees en arriere-plan apres le premier rendu.
	if (tabIdx == current_) queueStyles(tabIdx, d, resp.url);

	diagBootNote("page_css_bytes", (long)t.css.stylesheetBytes());
	diagBootNote("page_css_rules", (long)t.css.rules().size());
	diagBootMemory("apres_css");
	diagBootNote("app_heap_free_after_css", (long)diagApplicationHeapFree());
	diagStage("css ready");

	renderInto(tabIdx, std::move(d));
	history().add(t.url, t.title);
}

static std::string extractReaderHtml(const Document& doc, const std::string& title, bool dark, bool images)
{
	const char* background = dark ? "#242126" : "#FFFFFF";
	const char* foreground = dark ? "#EEEEF0" : "#202020";
	std::string h = "<html><head><title>" + htmlEscape(title) + "</title></head>"
	                "<body style=\"background: #FFFFFF; color: #202020; padding: 10px;\">";
	const std::string bodyStyle = "<style>body{background:" + std::string(background) +
		"!important;color:" + foreground + "!important}pre{background:" +
		(dark ? "#38343A" : "#EEEEEE") + "!important;color:" + foreground +
		"!important}h1,h2,h3,h4,h5,h6{color:" + (dark ? "#FFB878" : "#B05010") +
		"!important}</style>";
	h.insert(h.find(">", h.find("<body")) + 1, bodyStyle);
	if (!title.empty())
		h += "<h2 style=\"color: #C06010; margin-bottom: 12px;\">" + htmlEscape(title) + "</h2>";

	for (size_t i = 0; i < doc.nodes.size(); i++)
	{
		const Node& n = doc.nodes[i];
		if (!n.isElement()) continue;
		const std::string& t = n.tag;
		if (t == "img" && images)
		{
			const char* src = n.getAttr("src");
			const char* alt = n.getAttr("alt");
			if (src && *src)
				h += "<p><img src=\"" + htmlEscape(src) + "\" alt=\"" +
					htmlEscape(alt ? alt : "") + "\"></p>";
			continue;
		}
		if (t == "p" || t == "h1" || t == "h2" || t == "h3" || t == "h4" ||
		    t == "h5" || t == "h6" || t == "li" || t == "pre" || t == "blockquote")
		{
			if (t == "p")
			{
				int parent = n.parent;
				while (parent >= 0 && (size_t)parent < doc.nodes.size())
				{
					const Node& ancestor = doc.nodes[(size_t)parent];
					if (ancestor.tag == "li" || ancestor.tag == "blockquote") break;
					parent = ancestor.parent;
				}
				if (parent >= 0 && (size_t)parent < doc.nodes.size()) continue;
			}
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
	tab.pageCacheKey = pageFingerprint(tab, layoutWidth(tab));
	tab.failedSegmentIndex = -1;
	int focusedDomNode = -1;
	if (tab.focusNode >= 0 && tab.focusNode < (int)tab.page.fields.size())
		focusedDomNode = tab.page.fields[(size_t)tab.focusNode].node;
	tab.title = tab.doc.title();
	tab.pageReady = false;

	PageLayout lay(tab.doc, tab.css, set_, layoutWidth(tab));
	diagStage("layout start");
	diagBootNote("page_layout_cap", set_.maxPageHeight);
	diagBootMemory("avant_mise_en_page");
	diagBootNote("app_heap_free_before_layout", (long)diagApplicationHeapFree());
	diagBootNote("linear_mem_free_before_layout", (long)linearSpaceFree());
	const int segmentIndex = clamp((tab.scrollY +
		pageViewHeight(tab.page, tab.pdfZoomPercent) - 1) /
		NW_PAGE_SEGMENT_HEIGHT, 0, NW_MAX_PAGE_SEGMENTS - 1);
	const int segmentHeight = segmentIndex == 0
		? NW_PAGE_SEGMENT_HEIGHT
		: NW_PAGE_SEGMENT_HEIGHT + NW_PAGE_SEGMENT_OVERLAP;
	if (!lay.renderSegment(tab.page, tab.url, segmentIndex, segmentHeight))
	{
		diagStage("layout failed; rendering fallback");
		diagBootNote("page_segment_render_failed_heap",
			(long)diagApplicationHeapFree());
		tab.page = PageModel();
		std::string rHtml = extractReaderHtml(tab.doc, tab.title, set_.darkPages, set_.loadImages);
		Document rDoc;
		if (htmlParse(rHtml, rDoc))
		{
			PageLayout lay2(rDoc, g_emptyCss, set_, layoutWidth(tab));
			lay2.renderSegment(tab.page, tab.url, 0, NW_PAGE_SEGMENT_HEIGHT);
		}
		else
		{
			Document errDoc;
			std::string h = "<html><body><h3>Memoire insuffisante</h3><p>"
				"La page est trop lourde pour la New 3DS.</p></body></html>";
			htmlParse(h, errDoc);
			PageLayout lay2(errDoc, g_emptyCss, set_, layoutWidth(tab));
			lay2.renderSegment(tab.page, tab.url, 0, NW_PAGE_SEGMENT_HEIGHT);
		}
		diagStage("layout fallback complete");
	}
	else
	{
		const bool isInternal = strStartsWith(toLower(tab.url), "nw://") || strStartsWith(toLower(tab.url), "nw:/");
		if (!isInternal && tab.page.height < 60 && tab.doc.nodes.size() > 25)
		{
			std::string rHtml = extractReaderHtml(tab.doc, tab.title, set_.darkPages, set_.loadImages);
			Document rDoc;
			if (htmlParse(rHtml, rDoc))
			{
				tab.page = PageModel();
				PageLayout layReader(rDoc, g_emptyCss, set_, layoutWidth(tab));
				layReader.render(tab.page, tab.url);
			}
		}
		diagBootNote("page_render_height", tab.page.height);
		diagBootMemory("apres_mise_en_page");
		diagBootNote("app_heap_free_after_layout", (long)diagApplicationHeapFree());
		diagBootNote("linear_mem_free_after_layout", (long)linearSpaceFree());
		diagStage("layout complete");
	}

	tab.pageReady = true;
	layoutPending_ = false;
	progressStatus_ = tab.page.truncated
		? "Page longue — defilez pour continuer"
		: std::string();
	if (tab.page.bmp.px &&
		!pageSegmentCache_.store(tab.pageCacheKey, tab.page.segmentIndex,
			tab.page.bmp, tab.url))
		diagBootNote("segment_cache_write_failed", 1);
	if (!tab.page.bmp.px)
	{
		tab.page = PageModel();
		if (tab.page.bmp.create(layoutWidth(tab),
			NW_TOP_HEIGHT * layoutWidth(tab) / NW_PAGE_WIDTH))
		{
			tab.page.bmp.clear(0xFF17131A);
			TextEngine::instance().drawLine(tab.page.bmp,
				"Mémoire insuffisante pour afficher cette page",
				16, 96, 0.58f, 0xFFF2A25C, true, false);
			tab.page.height = pageViewHeight(tab.page, tab.pdfZoomPercent);
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
	if (maximum <= 0 || tabIdx < 0 || tabIdx >= (int)tabs_.size()) return;

	const Tab& tab = tabs_[tabIdx];
	pendingImagesReferer_ = tab.url;
	// Ne charger que les images proches de la zone visible. La meme fonction
	// est rappelee apres un defilement, ce qui permet d'atteindre les images
	// plus loin dans une longue page sans garder toutes les reponses en RAM.
	const int viewHeight = pageViewHeight(tab.page, tab.pdfZoomPercent);
	const int viewTop = std::max(0, tab.scrollY - viewHeight / 2);
	const int viewBottom = tab.scrollY + viewHeight * 2;
	std::vector<bool> selected(tab.page.images.size(), false);
	for (int slot = 0; slot < maximum; ++slot)
	{
		int best = -1;
		int bestDistance = 0x7fffffff;
		for (size_t i = 0; i < tab.page.images.size(); ++i)
		{
			if (selected[i]) continue;
			const auto& image = tab.page.images[i];
			if (image.src.empty() || images_.contains(image.src) ||
				(set_.blockTrackers && trackerUrl(image.src)) ||
				std::find(tab.unavailableImages.begin(),
					tab.unavailableImages.end(), image.src) != tab.unavailableImages.end() ||
				(imageRequestTab_ == tabIdx && imageRequestUrl_ == image.src) ||
				std::find(pendingImages_.begin(), pendingImages_.end(), image.src) != pendingImages_.end())
				continue;
			const int imageBottom = image.y + std::max(1, image.h);
			int distance = image.y > viewBottom ? image.y - viewBottom :
				(imageBottom < viewTop ? viewTop - imageBottom : 0);
			if (distance < bestDistance) { best = (int)i; bestDistance = distance; }
		}
		if (best < 0) break;
		selected[(size_t)best] = true;
		pendingImages_.push_back(tab.page.images[(size_t)best].src);
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
	const float scale = pageScale();
	x = t.scrollX + (int)((float)x * scale);
	y = (int)((float)y * scale);
	for (size_t i = 0; i < t.page.links.size(); i++)
	{
		const LinkBox& lb = t.page.links[i];
		const int linkY = lb.fixed
			? y + t.page.segmentIndex * NW_PAGE_SEGMENT_HEIGHT
			: y + sy;
		if (x >= lb.x && x < lb.x + lb.w &&
			linkY >= lb.y && linkY < lb.y + lb.h)
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
	const int logicalX = pageXFromView(x);
	const int pageY = tab.scrollY + pageYFromView(y);

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
			const bool pointerAllowed = jsPageDispatchEvent(tab.jsContext, "pointerdown", target.c_str(), nullptr, logicalX, pageY);
			jsPageDispatchEvent(tab.jsContext, "pointerup", target.c_str(), nullptr, logicalX, pageY);
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
	for (size_t i = 0; i < tab.page.fields.size(); i++)
	{
		FormField& f = tab.page.fields[i];
		if (logicalX >= f.x && logicalX < f.x + f.w && pageY >= f.y && pageY < f.y + f.h)
		{
			if (f.node >= 0 && tab.doc.nodes[(size_t)f.node].hasAttr("disabled")) return;
			const std::string target = "node:" + std::to_string(f.node);
			if (tab.jsContext && f.node >= 0)
			{
				tab.doc.hoveredNode = f.node; ++tab.doc.mutationVersion; refreshComputedStyles(tab);
				const bool pointerAllowed = jsPageDispatchEvent(tab.jsContext, "pointerdown", target.c_str(), nullptr, logicalX, pageY);
				jsPageDispatchEvent(tab.jsContext, "pointerup", target.c_str(), nullptr, logicalX, pageY);
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
		if (logicalX >= fs.x && logicalX < fs.x + fs.w &&
			pageY >= fs.y && pageY < fs.y + fs.h)
		{
			if (fs.node >= 0 && tab.doc.nodes[(size_t)fs.node].hasAttr("disabled")) return;
			if (tab.jsContext && fs.node >= 0)
			{
				tab.doc.hoveredNode = fs.node; ++tab.doc.mutationVersion; refreshComputedStyles(tab);
				const std::string target = "node:" + std::to_string(fs.node);
				const bool pointerAllowed = jsPageDispatchEvent(tab.jsContext, "pointerdown", target.c_str(), nullptr, logicalX, pageY);
				jsPageDispatchEvent(tab.jsContext, "pointerup", target.c_str(), nullptr, logicalX, pageY);
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

bool Browser::submitFormForField(int fieldIndex)
{
	Tab& tab = currentTab();
	if (fieldIndex < 0 || fieldIndex >= (int)tab.page.fields.size())
		return false;
	const int formId = tab.page.fields[(size_t)fieldIndex].formId;
	for (size_t i = 0; i < tab.page.submits.size(); ++i)
	{
		if (tab.page.submits[i].formId != formId) continue;
		submitFormByPage((int)i);
		return true;
	}
	return false;
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
	if (!tab.pageReady || tab.page.height <=
		pageViewHeight(tab.page, tab.pdfZoomPercent)) return;
	const int logicalDy = (int)floorf((float)dy * pageScale() + (dy >= 0 ? 0.5f : -0.5f));
	const int desired = std::max(0, tab.scrollY + logicalDy);
	const int max = maxPageScroll(tab.page, tab.pdfZoomPercent);
	if (desired > max && tab.page.truncated)
	{
		const int viewHeight = pageViewHeight(tab.page, tab.pdfZoomPercent);
		const int requestedSegment = (desired + viewHeight - 1) /
			NW_PAGE_SEGMENT_HEIGHT;
		const int index = std::min(requestedSegment, tab.page.segmentIndex + 1);
		requestSegment(current_, index, desired);
		tab.scrollY = max;
		return;
	}
	const int oldScrollY = tab.scrollY;
	tab.scrollY = std::min(desired, max);
	if (tab.scrollY != oldScrollY && set_.loadImages && imageRequestTab_ < 0)
		queueImages(current_, 6);
	const int visibleSegment = (tab.scrollY +
		pageViewHeight(tab.page, tab.pdfZoomPercent) - 1) /
		NW_PAGE_SEGMENT_HEIGHT;
	if (visibleSegment != tab.page.segmentIndex)
		requestSegment(current_, visibleSegment, tab.scrollY);
}

int Browser::scrollY() const
{
	if (tabs_.empty()) return 0;
	return tabs_[current_].scrollY;
}

void Browser::scrollHorizontal(int dx)
{
	if (tabs_.empty()) return;
	const int pageDelta = (int)floorf((float)dx * pageScale() +
		(dx >= 0 ? 0.5f : -0.5f));
	scrollHorizontalTo(currentTab().scrollX + pageDelta);
}

void Browser::scrollHorizontalTo(int x)
{
	if (tabs_.empty()) return;
	Tab& tab = currentTab();
	if (!tab.pdfDocument || tab.pdfZoomPercent <= 100 ||
		tab.page.bmp.w <= 0) return;
	const int viewWidth = (int)floorf((float)NW_PAGE_WIDTH * pageScale() +
		0.5f);
	const int max = std::max(0, tab.page.bmp.w - viewWidth);
	tab.scrollX = clamp(x, 0, max);
}

int Browser::scrollX() const
{
	return tabs_.empty() ? 0 : tabs_[current_].scrollX;
}

float Browser::pageScale() const
{
	if (tabs_.empty() || tabs_[(size_t)current_].page.bmp.w <= 0) return 1.0f;
	const Tab& tab = tabs_[(size_t)current_];
	return (float)tab.page.bmp.w * 100.0f /
		((float)NW_PAGE_WIDTH * (float)std::max(100, tab.pdfZoomPercent));
}

int Browser::pageXFromView(int x) const
{
	const int offset = tabs_.empty() ? 0 : tabs_[(size_t)current_].scrollX;
	return offset + (int)floorf((float)x * pageScale() + 0.5f);
}

int Browser::pageYFromView(int y) const
{
	return (int)floorf((float)y * pageScale() + 0.5f);
}

void Browser::scrollTo(int y)
{
	Tab& tab = currentTab();
	if (!tab.pageReady) return;
	const int desired = std::max(0, y);
	const int max = maxPageScroll(tab.page, tab.pdfZoomPercent);
	if (desired > max && tab.page.truncated)
	{
		const int requestedSegment = (desired +
			pageViewHeight(tab.page, tab.pdfZoomPercent) - 1) /
			NW_PAGE_SEGMENT_HEIGHT;
		const int index = std::min(requestedSegment, tab.page.segmentIndex + 1);
		requestSegment(current_, index, desired);
		tab.scrollY = max;
		return;
	}
	const int oldScrollY = tab.scrollY;
	tab.scrollY = std::min(desired, max);
	if (tab.scrollY != oldScrollY && set_.loadImages && imageRequestTab_ < 0)
		queueImages(current_, 6);
	const int visibleSegment = (tab.scrollY +
		pageViewHeight(tab.page, tab.pdfZoomPercent) - 1) /
		NW_PAGE_SEGMENT_HEIGHT;
	if (visibleSegment != tab.page.segmentIndex)
		requestSegment(current_, visibleSegment, tab.scrollY);
}

void Browser::requestSegment(int tabIdx, int segmentIndex, int desiredScrollY)
{
	if (tabIdx < 0 || tabIdx >= (int)tabs_.size() || segmentIndex < 0) return;
	if (segmentIndex >= NW_MAX_PAGE_SEGMENTS)
	{
		progressStatus_ = "Limite de longueur atteinte (" +
			itos(NW_MAX_PAGE_CONTENT_HEIGHT) + " px)";
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
	const bool needsDomLayout = segmentIndex > tab.page.furthestSegmentIndex ||
		!tab.page.fixedBoxes.empty();
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
		PageLayout layout(tab.doc, tab.css, set_, layoutWidth(tab));
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
			if (needsDomLayout)
			{
				tab.page.links.erase(std::remove_if(tab.page.links.begin(),
					tab.page.links.end(), [](const LinkBox& link)
					{ return link.fixed; }), tab.page.links.end());
				for (const LinkBox& link : rendered.links)
					if (link.fixed) tab.page.links.push_back(link);
				tab.page.fixedBoxes = std::move(rendered.fixedBoxes);
			}
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

	const int max = maxPageScroll(tab.page, tab.pdfZoomPercent);
	tab.scrollY = std::min(std::max(0, desiredScroll), max);
	pendingScrollY_ = -1;
	layoutPending_ = false;
	progressStatus_ = tab.page.truncated
		? "Page longue — defilez pour continuer"
		: std::string();

	if (tab.page.truncated && desiredScroll > max)
	{
		const int nextIndex = (desiredScroll +
			pageViewHeight(tab.page, tab.pdfZoomPercent) - 1) /
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
	if (!tab.pageReady || tab.page.height <=
		pageViewHeight(tab.page, tab.pdfZoomPercent)) return false;
	const int max = maxPageScroll(tab.page, tab.pdfZoomPercent);
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
