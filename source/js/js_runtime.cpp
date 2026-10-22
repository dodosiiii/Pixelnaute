#include "js_runtime.hpp"
#include "quickjs.h"
#include "../html/dom.hpp"
#include "../html/parser.hpp"
#include "../storage/storage.hpp"
#include "../network/http.hpp"
#include <3ds.h>

#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include <string>
#include <cstdio>
#include <cerrno>
#include <iterator>
#include <cstring>

namespace
{
constexpr size_t kJsHeapLimit = 4u * 1024u * 1024u;
constexpr size_t kJsStackLimit = 96u * 1024u;

// newlib n'expose pas malloc_usable_size. Garder la taille demandee dans un
// petit en-tete permet a QuickJS de suivre correctement son tas et d'appliquer
// JS_SetMemoryLimit sur la 3DS.
struct alignas(8) AllocationHeader
{
	size_t size;
};

static_assert(sizeof(AllocationHeader) == 8, "L'entete QuickJS doit faire 8 octets");
constexpr size_t kAllocationOverhead = sizeof(AllocationHeader);
constexpr uint64_t kJsScriptBudgetMs = 100;
constexpr uint64_t kJsPageBudgetMs = 300;

struct ExecutionBudget
{
	uint64_t pageElapsedMs = 0;
	uint64_t executionStartedMs = 0;
	uint64_t executionDeadlineMs = 0;
	bool active = false;
	bool interrupted = false;
	bool chargePageBudget = true;
};

int interruptJavaScript(JSRuntime*, void* opaque)
{
	ExecutionBudget* budget = static_cast<ExecutionBudget*>(opaque);
	if (!budget || !budget->active) return 0;
	if (osGetTime() >= budget->executionDeadlineMs)
	{
		budget->interrupted = true;
		return 1;
	}
	return 0;
}

bool allocationFits(const JSMallocState* state, size_t size)
{
	if (size > std::numeric_limits<size_t>::max() - kAllocationOverhead)
		return false;
	if (state->malloc_size > state->malloc_limit)
		return false;
	return size + kAllocationOverhead <= state->malloc_limit - state->malloc_size;
}

void* quickJsMalloc(JSMallocState* state, size_t size)
{
	if (!allocationFits(state, size)) return nullptr;
	AllocationHeader* header = static_cast<AllocationHeader*>(
		std::malloc(sizeof(AllocationHeader) + size));
	if (!header) return nullptr;
	header->size = size;
	++state->malloc_count;
	state->malloc_size += size + kAllocationOverhead;
	return header + 1;
}

void quickJsFree(JSMallocState* state, void* ptr)
{
	if (!ptr) return;
	AllocationHeader* header = static_cast<AllocationHeader*>(ptr) - 1;
	--state->malloc_count;
	state->malloc_size -= header->size + kAllocationOverhead;
	std::free(header);
}

void* quickJsRealloc(JSMallocState* state, void* ptr, size_t size)
{
	if (!ptr) return size ? quickJsMalloc(state, size) : nullptr;
	if (!size)
	{
		quickJsFree(state, ptr);
		return nullptr;
	}

	AllocationHeader* oldHeader = static_cast<AllocationHeader*>(ptr) - 1;
	const size_t oldSize = oldHeader->size;
	const size_t oldCharge = oldSize + kAllocationOverhead;
	if (state->malloc_size < oldCharge) return nullptr;
	const size_t baseSize = state->malloc_size - oldCharge;
	if (size > std::numeric_limits<size_t>::max() - kAllocationOverhead ||
		baseSize > state->malloc_limit ||
		size + kAllocationOverhead > state->malloc_limit - baseSize)
		return nullptr;

	AllocationHeader* newHeader = static_cast<AllocationHeader*>(
		std::realloc(oldHeader, sizeof(AllocationHeader) + size));
	if (!newHeader) return nullptr;
	newHeader->size = size;
	state->malloc_size = baseSize + size + kAllocationOverhead;
	return newHeader + 1;
}

size_t quickJsUsableSize(const void* ptr)
{
	if (!ptr) return 0;
	const AllocationHeader* header = static_cast<const AllocationHeader*>(ptr) - 1;
	return header->size;
}

const JSMallocFunctions kQuickJsAllocator = {
	quickJsMalloc,
	quickJsFree,
	quickJsRealloc,
	quickJsUsableSize
};

static std::string jsString(JSContext* ctx, JSValueConst value)
{
	const char* s = JS_ToCString(ctx, value);
	if (!s) return std::string();
	std::string result(s);
	JS_FreeCString(ctx, s);
	return result;
}

static Document* boundDocument(JSContext* ctx)
{
	JSPageBinding* binding = static_cast<JSPageBinding*>(JS_GetContextOpaque(ctx));
	return binding ? binding->document : nullptr;
}

static std::string hexEncode(const std::string& value)
{
	static const char digits[] = "0123456789abcdef";
	std::string out; out.reserve(value.size() * 2);
	for (unsigned char ch : value) { out += digits[ch >> 4]; out += digits[ch & 15]; }
	return out;
}

static bool hexDecode(const std::string& value, std::string& out)
{
	if (value.size() & 1) return false;
	out.clear(); out.reserve(value.size() / 2);
	for (size_t i = 0; i < value.size(); i += 2)
	{
		auto nibble = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
		int a = nibble(value[i]), b = nibble(value[i + 1]); if (a < 0 || b < 0) return false;
		out.push_back((char)((a << 4) | b));
	}
	return true;
}

using WebStore = std::map<std::string, std::string>;
static bool storageWithinLimit(const WebStore& map)
{
	if (map.size() > 256) return false;
	size_t total = 0;
	for (const auto& item : map)
	{
		if (item.first.size() > 2048 || item.second.size() > 8192) return false;
		total += item.first.size() + item.second.size();
		if (total > 32768) return false;
	}
	return true;
}
static bool loadOriginStorage(const std::string& origin, WebStore& out)
{
	out.clear(); FILE* f = fopen("sdmc:/N3DSWeb/localstorage.txt", "rb"); if (!f) return true;
	char line[32768];
	while (fgets(line, sizeof(line), f))
	{
		std::string row(line); while (!row.empty() && (row.back() == '\n' || row.back() == '\r')) row.pop_back();
		size_t a = row.find('\t'), b = a == std::string::npos ? a : row.find('\t', a + 1);
		if (a == std::string::npos || b == std::string::npos) continue;
		std::string rowOrigin, key, value;
		if (hexDecode(row.substr(0, a), rowOrigin) && rowOrigin == origin &&
			hexDecode(row.substr(a + 1, b - a - 1), key) && hexDecode(row.substr(b + 1), value))
			out[key] = value;
	}
	fclose(f); return true;
}

static bool saveOriginStorage(const std::string& origin, const WebStore& changed)
{
	FILE* f = nullptr; char line[32768];
	// Conserver les lignes des autres origines sans les interpréter.
	std::vector<std::string> otherRows;
	f = fopen("sdmc:/N3DSWeb/localstorage.txt", "rb");
	if (f) { while (fgets(line, sizeof(line), f)) { std::string row(line); size_t end = row.find_last_not_of("\r\n"); if (end != std::string::npos) row.resize(end + 1); if (row.find(hexEncode(origin) + "\t") != 0 && row.find('\t') != std::string::npos) otherRows.push_back(row); } fclose(f); }
	FILE* out = fopen("sdmc:/N3DSWeb/localstorage.tmp", "wb"); if (!out) return false;
	for (const std::string& row : otherRows) fprintf(out, "%s\n", row.c_str());
	size_t total = 0;
	for (const auto& entry : changed)
	{
		if (entry.first.size() > 2048 || entry.second.size() > 8192) { fclose(out); return false; }
		const std::string row = hexEncode(origin) + "\t" + hexEncode(entry.first) + "\t" + hexEncode(entry.second) + "\n";
		total += row.size(); if (total > 32768) { fclose(out); return false; }
		fwrite(row.data(), 1, row.size(), out);
	}
	if (fclose(out) != 0) return false;
	remove("sdmc:/N3DSWeb/localstorage.bak");
	if (rename("sdmc:/N3DSWeb/localstorage.txt", "sdmc:/N3DSWeb/localstorage.bak") != 0 && errno != ENOENT) return false;
	if (rename("sdmc:/N3DSWeb/localstorage.tmp", "sdmc:/N3DSWeb/localstorage.txt") != 0)
	{ rename("sdmc:/N3DSWeb/localstorage.bak", "sdmc:/N3DSWeb/localstorage.txt"); return false; }
	remove("sdmc:/N3DSWeb/localstorage.bak"); return true;
}

static WebStore* storageMap(JSContext* ctx, int kind, WebStore& local)
{
	JSPageBinding* binding = static_cast<JSPageBinding*>(JS_GetContextOpaque(ctx));
	if (!binding) return nullptr;
	if (kind) return &binding->sessionStorage;
	if (binding->origin.empty() || !loadOriginStorage(binding->origin, local)) return nullptr;
	return &local;
}

static int storageKind(JSContext* ctx, JSValueConst thisVal)
{
	JSValue v = JS_GetPropertyStr(ctx, thisVal, "__pixelKind"); int kind = JS_ToBool(ctx, v); JS_FreeValue(ctx, v); return kind;
}

static JSValue storageGetItem(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (argc < 1) return JS_NULL;
	WebStore local; WebStore* map = storageMap(ctx, storageKind(ctx, thisVal), local); if (!map) return JS_NULL;
	const std::string key = jsString(ctx, argv[0]); auto it = map->find(key);
	return it == map->end() ? JS_NULL : JS_NewStringLen(ctx, it->second.data(), it->second.size());
}
static JSValue storageSetItem(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (argc < 2) return JS_UNDEFINED;
	JSPageBinding* binding = static_cast<JSPageBinding*>(JS_GetContextOpaque(ctx)); const int kind = storageKind(ctx, thisVal);
	WebStore local; WebStore* map = storageMap(ctx, kind, local); if (!map) return JS_ThrowTypeError(ctx, "Storage unavailable for this origin");
	const std::string key = jsString(ctx, argv[0]), value = jsString(ctx, argv[1]);
	const auto old = map->find(key); const bool existed = old != map->end(); const std::string oldValue = existed ? old->second : std::string();
	(*map)[key] = value;
	if (!storageWithinLimit(*map)) { if (existed) (*map)[key] = oldValue; else map->erase(key); return JS_ThrowRangeError(ctx, "Storage quota exceeded"); }
	if (!kind && !saveOriginStorage(binding->origin, *map)) { if (existed) (*map)[key] = oldValue; else map->erase(key); return JS_ThrowTypeError(ctx, "Storage write failed"); }
	return JS_UNDEFINED;
}
static JSValue storageRemoveItem(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (argc < 1) return JS_UNDEFINED;
	JSPageBinding* binding = static_cast<JSPageBinding*>(JS_GetContextOpaque(ctx)); const int kind = storageKind(ctx, thisVal);
	WebStore local; WebStore* map = storageMap(ctx, kind, local); if (!map) return JS_UNDEFINED; map->erase(jsString(ctx, argv[0]));
	if (!kind && !saveOriginStorage(binding->origin, *map)) return JS_ThrowTypeError(ctx, "Storage write failed");
	return JS_UNDEFINED;
}
static JSValue storageClear(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JSPageBinding* binding = static_cast<JSPageBinding*>(JS_GetContextOpaque(ctx)); const int kind = storageKind(ctx, thisVal); WebStore local; WebStore* map = storageMap(ctx, kind, local);
	if (!map) return JS_UNDEFINED;
	map->clear(); if (!kind && !saveOriginStorage(binding->origin, *map)) return JS_ThrowTypeError(ctx, "Storage write failed");
	return JS_UNDEFINED;
}
static JSValue storageKey(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	int32_t index = -1;
	if (argc) JS_ToInt32(ctx, &index, argv[0]);
	WebStore local;
	WebStore* map = storageMap(ctx, storageKind(ctx, thisVal), local);
	if (!map || index < 0 || (size_t)index >= map->size()) return JS_NULL;
	auto it = map->begin();
	std::advance(it, index);
	return JS_NewStringLen(ctx, it->first.data(), it->first.size());
}
static JSValue storageLength(JSContext* ctx, JSValueConst thisVal)
{
	WebStore local; WebStore* map = storageMap(ctx, storageKind(ctx, thisVal), local); return JS_NewUint32(ctx, map ? (uint32_t)map->size() : 0);
}
static JSValue createStorage(JSContext* ctx, bool session)
{
	JSValue obj = JS_NewObject(ctx); JS_SetPropertyStr(ctx, obj, "__pixelKind", JS_NewBool(ctx, session));
	JS_SetPropertyStr(ctx, obj, "getItem", JS_NewCFunction(ctx, storageGetItem, "getItem", 1));
	JS_SetPropertyStr(ctx, obj, "setItem", JS_NewCFunction(ctx, storageSetItem, "setItem", 2));
	JS_SetPropertyStr(ctx, obj, "removeItem", JS_NewCFunction(ctx, storageRemoveItem, "removeItem", 1));
	JS_SetPropertyStr(ctx, obj, "clear", JS_NewCFunction(ctx, storageClear, "clear", 0));
	JS_SetPropertyStr(ctx, obj, "key", JS_NewCFunction(ctx, storageKey, "key", 1));
	JSAtom lengthAtom = JS_NewAtom(ctx, "length");
	JSCFunctionType getter; getter.getter = storageLength;
	JS_DefinePropertyGetSet(ctx, obj, lengthAtom, JS_NewCFunction2(ctx, getter.generic, "get length", 0, JS_CFUNC_getter, 0), JS_UNDEFINED, JS_PROP_CONFIGURABLE);
	JS_FreeAtom(ctx, lengthAtom);
	return obj;
}

static JSValue documentCookieGet(JSContext* ctx, JSValueConst)
{
	JSPageBinding* binding = static_cast<JSPageBinding*>(JS_GetContextOpaque(ctx));
	if (!binding || binding->url.empty()) return JS_NewString(ctx, "");
	const std::string cookies = httpCookieHeaderForUrl(binding->url);
	return JS_NewStringLen(ctx, cookies.data(), cookies.size());
}
static JSValue documentCookieSet(JSContext* ctx, JSValueConst, JSValueConst value)
{
	JSPageBinding* binding = static_cast<JSPageBinding*>(JS_GetContextOpaque(ctx));
	if (!binding || binding->url.empty()) return JS_UNDEFINED;
	const std::string cookie = jsString(ctx, value);
	if (cookie.find_first_of("\r\n") != std::string::npos || cookie.size() > 4096)
		return JS_ThrowTypeError(ctx, "Invalid cookie value");
	std::string lowered = toLower(cookie);
	if (lowered.find("; httponly") != std::string::npos)
		return JS_ThrowTypeError(ctx, "HttpOnly cookies cannot be set by script");
	if (!httpSetCookieForUrl(binding->url, cookie)) return JS_ThrowTypeError(ctx, "Cookie rejected");
	return JS_UNDEFINED;
}

static JSPageBinding* pageBinding(JSContext* ctx)
{
	return static_cast<JSPageBinding*>(JS_GetContextOpaque(ctx));
}
static bool executePendingJobs(JSRuntime* runtime, JSContext* fallbackContext);
static std::string upperAscii(std::string value)
{
	for (char& ch : value) ch = (char)std::toupper((unsigned char)ch);
	return value;
}
static std::string urlComponentValue(const std::string& url, int magic)
{
	const size_t schemeEnd = url.find("://");
	if (magic == 0) return url;
	if (schemeEnd == std::string::npos) return std::string();
	const size_t authorityStart = schemeEnd + 3;
	const size_t authorityEnd = url.find_first_of("/?#", authorityStart);
	const std::string authority = url.substr(authorityStart, authorityEnd == std::string::npos ? authorityEnd : authorityEnd - authorityStart);
	const size_t at = authority.rfind('@');
	const std::string hostPort = at == std::string::npos ? authority : authority.substr(at + 1);
	std::string host = hostPort, port;
	if (!hostPort.empty() && hostPort[0] != '[')
	{
		const size_t colon = hostPort.rfind(':');
		if (colon != std::string::npos) { host = hostPort.substr(0, colon); port = hostPort.substr(colon + 1); }
	}
	else if (!hostPort.empty() && hostPort[0] == '[')
	{
		const size_t bracket = hostPort.find(']');
		if (bracket != std::string::npos) { host = hostPort.substr(0, bracket + 1); if (bracket + 1 < hostPort.size() && hostPort[bracket + 1] == ':') port = hostPort.substr(bracket + 2); }
	}
	const size_t pathStart = authorityEnd == std::string::npos ? url.size() : authorityEnd;
	const size_t query = url.find('?', pathStart), fragment = url.find('#', pathStart);
	std::string value;
	switch (magic)
	{
	case 1: value = url.substr(0, schemeEnd + 1); break;
	case 2: value = url.substr(0, schemeEnd + 3) + hostPort; break;
	case 3: value = hostPort; break;
	case 4: value = host; break;
	case 5: value = port; break;
	case 6:
		value = pathStart == url.size() || url[pathStart] == '?' || url[pathStart] == '#' ? "/" :
			url.substr(pathStart, std::min(query == std::string::npos ? url.size() : query, fragment == std::string::npos ? url.size() : fragment) - pathStart);
		break;
	case 7: if (query != std::string::npos && (fragment == std::string::npos || query < fragment)) value = url.substr(query, (fragment == std::string::npos ? url.size() : fragment) - query); break;
	case 8: if (fragment != std::string::npos) value = url.substr(fragment); break;
	default: break;
	}
	if (magic == 3 || magic == 4 || magic == 5)
		for (char& ch : value) ch = (char)std::tolower((unsigned char)ch);
	return value;
}
static JSValue locationComponentGet(JSContext* ctx, JSValueConst, int magic)
{
	JSPageBinding* binding = pageBinding(ctx);
	const std::string value = urlComponentValue(binding ? binding->url : std::string(), magic);
	return JS_NewStringLen(ctx, value.data(), value.size());
}
static JSValue locationHrefGet(JSContext* ctx, JSValueConst)
{
	return locationComponentGet(ctx, JS_UNDEFINED, 0);
}
static JSValue queueLocation(JSContext* ctx, JSValueConst value, int mode)
{
	JSPageBinding* binding = pageBinding(ctx); if (!binding) return JS_UNDEFINED;
	std::string target = jsString(ctx, value);
	binding->pendingNavigation = urlJoin(binding->url, target);
	binding->navigationMode = mode;
	return JS_UNDEFINED;
}
static JSValue locationHrefSet(JSContext* ctx, JSValueConst, JSValueConst value)
{
	return queueLocation(ctx, value, 1);
}
static JSValue locationAssign(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	return argc ? queueLocation(ctx, argv[0], 1) : JS_UNDEFINED;
}
static JSValue locationReplace(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	return argc ? queueLocation(ctx, argv[0], 2) : JS_UNDEFINED;
}
static JSValue locationReload(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
	JSPageBinding* binding = pageBinding(ctx); if (binding) { binding->pendingNavigation = binding->url; binding->navigationMode = 3; }
	return JS_UNDEFINED;
}

static JSValue responseText(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JSValue resolving[2]; JSValue promise = JS_NewPromiseCapability(ctx, resolving);
	JSValue body = JS_GetPropertyStr(ctx, thisVal, "__pixelBody");
	JSValue result = JS_Call(ctx, resolving[0], JS_UNDEFINED, 1, &body);
	JS_FreeValue(ctx, result); JS_FreeValue(ctx, body); JS_FreeValue(ctx, resolving[0]); JS_FreeValue(ctx, resolving[1]);
	return promise;
}
static JSValue responseJson(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JSValue resolving[2]; JSValue promise = JS_NewPromiseCapability(ctx, resolving);
	JSValue bodyValue = JS_GetPropertyStr(ctx, thisVal, "__pixelBody");
	const char* body = JS_ToCString(ctx, bodyValue); JSValue parsed = body ? JS_ParseJSON(ctx, body, strlen(body), "<fetch.json>") : JS_EXCEPTION;
	if (body) JS_FreeCString(ctx, body);
	JS_FreeValue(ctx, bodyValue);
	JSValue settle = JS_IsException(parsed) ? resolving[1] : resolving[0];
	if (JS_IsException(parsed)) parsed = JS_GetException(ctx);
	JSValue result = JS_Call(ctx, settle, JS_UNDEFINED, 1, &parsed);
	JS_FreeValue(ctx, result); JS_FreeValue(ctx, parsed); JS_FreeValue(ctx, resolving[0]); JS_FreeValue(ctx, resolving[1]);
	return promise;
}
static JSValue responseHeaderGet(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (!argc || toLower(jsString(ctx, argv[0])) != "content-type") return JS_NULL;
	return JS_GetPropertyStr(ctx, thisVal, "contentType");
}

static JSValue pageFetch(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	JSPageBinding* binding = pageBinding(ctx);
	JSValue resolving[2]; JSValue promise = JS_NewPromiseCapability(ctx, resolving);
	if (!binding || !argc || !binding->enqueueFetch)
	{
		JSValue error = JS_NewError(ctx); JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, "fetch unavailable"));
		JS_Call(ctx, resolving[1], JS_UNDEFINED, 1, &error); JS_FreeValue(ctx, error);
		JS_FreeValue(ctx, resolving[0]); JS_FreeValue(ctx, resolving[1]); return promise;
	}
	std::string url = urlJoin(binding->url, jsString(ctx, argv[0]));
	std::string method = "GET", body;
	if (argc > 1 && JS_IsObject(argv[1]))
	{
		JSValue value = JS_GetPropertyStr(ctx, argv[1], "method"); if (!JS_IsUndefined(value)) method = upperAscii(jsString(ctx, value)); JS_FreeValue(ctx, value);
		value = JS_GetPropertyStr(ctx, argv[1], "body"); if (!JS_IsUndefined(value) && !JS_IsNull(value)) body = jsString(ctx, value); JS_FreeValue(ctx, value);
	}
	if ((method != "GET" && method != "POST") || url.size() > 2048 || body.size() > 8192)
	{
		JSValue error = JS_NewError(ctx); JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, "Unsupported fetch request"));
		JS_Call(ctx, resolving[1], JS_UNDEFINED, 1, &error); JS_FreeValue(ctx, error);
		JS_FreeValue(ctx, resolving[0]); JS_FreeValue(ctx, resolving[1]); return promise;
	}
	const uint32_t id = binding->nextFetchId++;
	JSValue global = JS_GetGlobalObject(ctx), entries = JS_GetPropertyStr(ctx, global, "__pixelFetchPromises");
	JSValue record = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, record, "resolve", JS_DupValue(ctx, resolving[0]));
	JS_SetPropertyStr(ctx, record, "reject", JS_DupValue(ctx, resolving[1]));
	JS_SetPropertyUint32(ctx, entries, id, JS_DupValue(ctx, record));
	JS_FreeValue(ctx, global);
 if (!binding->enqueueFetch(binding->fetchOwner, binding, binding->fetchTabIndex, id, false, url, method, body))
	{
		JSValue error = JS_NewError(ctx); JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, "Unable to queue fetch"));
		JSValue reject = JS_GetPropertyStr(ctx, record, "reject"); JS_Call(ctx, reject, JS_UNDEFINED, 1, &error);
		JS_FreeValue(ctx, reject); JS_FreeValue(ctx, error);
		JS_SetPropertyUint32(ctx, entries, id, JS_UNDEFINED);
	}
	JS_FreeValue(ctx, record); JS_FreeValue(ctx, entries); JS_FreeValue(ctx, resolving[0]); JS_FreeValue(ctx, resolving[1]);
	return promise;
}

static void callXhrHandler(JSContext* ctx, JSValueConst xhr, const char* name)
{
	JSValue callback = JS_GetPropertyStr(ctx, xhr, name);
	if (JS_IsFunction(ctx, callback))
	{
		JSValue result = JS_Call(ctx, callback, xhr, 0, nullptr);
		if (JS_IsException(result)) { JSValue exception = JS_GetException(ctx); JS_FreeValue(ctx, exception); }
		else executePendingJobs(JS_GetRuntime(ctx), ctx);
		JS_FreeValue(ctx, result);
	}
	JS_FreeValue(ctx, callback);
}
static JSValue xhrOpen(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (argc < 2) return JS_ThrowTypeError(ctx, "XMLHttpRequest.open requires method and URL");
	const bool async = argc < 3 || JS_ToBool(ctx, argv[2]);
	if (!async) return JS_ThrowTypeError(ctx, "Synchronous XMLHttpRequest is unsupported");
	JSPageBinding* binding = pageBinding(ctx);
	const std::string method = upperAscii(jsString(ctx, argv[0]));
	const std::string url = urlJoin(binding ? binding->url : std::string(), jsString(ctx, argv[1]));
	JS_SetPropertyStr(ctx, thisVal, "__pixelMethod", JS_NewString(ctx, method.c_str()));
	JS_SetPropertyStr(ctx, thisVal, "__pixelUrl", JS_NewStringLen(ctx, url.data(), url.size()));
	JS_SetPropertyStr(ctx, thisVal, "__pixelSent", JS_NewBool(ctx, 0));
	JS_SetPropertyStr(ctx, thisVal, "__pixelAborted", JS_NewBool(ctx, 0));
	JS_SetPropertyStr(ctx, thisVal, "readyState", JS_NewInt32(ctx, 1));
	JS_SetPropertyStr(ctx, thisVal, "status", JS_NewInt32(ctx, 0));
	JS_SetPropertyStr(ctx, thisVal, "responseText", JS_NewString(ctx, ""));
	callXhrHandler(ctx, thisVal, "onreadystatechange");
	return JS_UNDEFINED;
}
static JSValue xhrSend(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	JSPageBinding* binding = pageBinding(ctx); if (!binding || !binding->enqueueFetch) return JS_ThrowTypeError(ctx, "XMLHttpRequest is unavailable");
	JSValue sent = JS_GetPropertyStr(ctx, thisVal, "__pixelSent"); const bool alreadySent = JS_ToBool(ctx, sent) != 0; JS_FreeValue(ctx, sent);
	if (alreadySent) return JS_ThrowTypeError(ctx, "XMLHttpRequest has already been sent");
	JSValue methodValue = JS_GetPropertyStr(ctx, thisVal, "__pixelMethod");
	JSValue urlValue = JS_GetPropertyStr(ctx, thisVal, "__pixelUrl");
	const std::string method = jsString(ctx, methodValue), url = jsString(ctx, urlValue);
	JS_FreeValue(ctx, methodValue); JS_FreeValue(ctx, urlValue);
	std::string body;
	if (argc && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0])) body = jsString(ctx, argv[0]);
	if ((method != "GET" && method != "POST") || url.size() > 2048 || body.size() > 8192)
		return JS_ThrowTypeError(ctx, "Unsupported XMLHttpRequest");
	const uint32_t id = binding->nextFetchId++;
	JS_SetPropertyStr(ctx, thisVal, "__pixelId", JS_NewUint32(ctx, id));
	JS_SetPropertyStr(ctx, thisVal, "__pixelSent", JS_NewBool(ctx, 1));
	JSValue global = JS_GetGlobalObject(ctx), entries = JS_GetPropertyStr(ctx, global, "__pixelXHRs");
	JS_SetPropertyUint32(ctx, entries, id, JS_DupValue(ctx, thisVal));
	JS_FreeValue(ctx, entries); JS_FreeValue(ctx, global);
	if (!binding->enqueueFetch(binding->fetchOwner, binding, binding->fetchTabIndex, id, true, url, method, body))
		jsPageResolveXHR(JS_GetRuntime(ctx), ctx, id, "", "", 0, "", 0, "Unable to queue XMLHttpRequest");
	return JS_UNDEFINED;
}
static JSValue xhrAbort(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JS_SetPropertyStr(ctx, thisVal, "__pixelAborted", JS_NewBool(ctx, 1));
	JS_SetPropertyStr(ctx, thisVal, "readyState", JS_NewInt32(ctx, 0));
	JS_SetPropertyStr(ctx, thisVal, "status", JS_NewInt32(ctx, 0));
	callXhrHandler(ctx, thisVal, "onreadystatechange"); callXhrHandler(ctx, thisVal, "onabort");
	return JS_UNDEFINED;
}
static JSValue xhrSetRequestHeader(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
	return JS_ThrowTypeError(ctx, "Custom XMLHttpRequest headers are not supported yet");
}
static JSValue xhrGetResponseHeader(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (!argc || toLower(jsString(ctx, argv[0])) != "content-type") return JS_NULL;
	return JS_GetPropertyStr(ctx, thisVal, "__pixelContentType");
}
static JSValue xhrGetAllResponseHeaders(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JSValue type = JS_GetPropertyStr(ctx, thisVal, "__pixelContentType");
	const std::string value = jsString(ctx, type); JS_FreeValue(ctx, type);
	return value.empty() ? JS_NewString(ctx, "") : JS_NewString(ctx, ("content-type: " + value + "\r\n").c_str());
}
static JSValue xhrConstructor(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
	JSValue xhr = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, xhr, "readyState", JS_NewInt32(ctx, 0));
	JS_SetPropertyStr(ctx, xhr, "status", JS_NewInt32(ctx, 0));
	JS_SetPropertyStr(ctx, xhr, "responseText", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, xhr, "responseURL", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, xhr, "onreadystatechange", JS_NULL);
	JS_SetPropertyStr(ctx, xhr, "onload", JS_NULL);
	JS_SetPropertyStr(ctx, xhr, "onerror", JS_NULL);
	JS_SetPropertyStr(ctx, xhr, "onabort", JS_NULL);
	JS_SetPropertyStr(ctx, xhr, "open", JS_NewCFunction(ctx, xhrOpen, "open", 3));
	JS_SetPropertyStr(ctx, xhr, "send", JS_NewCFunction(ctx, xhrSend, "send", 1));
	JS_SetPropertyStr(ctx, xhr, "abort", JS_NewCFunction(ctx, xhrAbort, "abort", 0));
	JS_SetPropertyStr(ctx, xhr, "setRequestHeader", JS_NewCFunction(ctx, xhrSetRequestHeader, "setRequestHeader", 2));
	JS_SetPropertyStr(ctx, xhr, "getResponseHeader", JS_NewCFunction(ctx, xhrGetResponseHeader, "getResponseHeader", 1));
	JS_SetPropertyStr(ctx, xhr, "getAllResponseHeaders", JS_NewCFunction(ctx, xhrGetAllResponseHeaders, "getAllResponseHeaders", 0));
	return xhr;
}

static JSValue paramsAppend(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue paramsGet(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue paramsGetAll(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue paramsSet(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue paramsDelete(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue paramsHas(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue paramsToString(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue paramsForEach(JSContext*, JSValueConst, int, JSValueConst*);
static uint32_t jsArrayLength(JSContext* ctx, JSValueConst array);
static JSValue urlToString(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	return JS_GetPropertyStr(ctx, thisVal, "href");
}

static JSValue newParamsObject(JSContext* ctx, const std::string& initial)
{
	JSValue object = JS_NewObject(ctx), pairs = JS_NewArray(ctx); uint32_t count = 0;
	std::string input = initial; if (!input.empty() && input[0] == '?') input.erase(input.begin());
	for (size_t start = 0; start <= input.size() && count < 256; )
	{
		const size_t amp = input.find('&', start);
		const std::string item = input.substr(start, amp == std::string::npos ? amp : amp - start);
		if (!item.empty())
		{
			const size_t eq = item.find('='); const std::string name = urlDecode(item.substr(0, eq));
			const std::string value = eq == std::string::npos ? std::string() : urlDecode(item.substr(eq + 1));
			JSValue pair = JS_NewObject(ctx); JS_SetPropertyStr(ctx, pair, "name", JS_NewStringLen(ctx, name.data(), name.size()));
			JS_SetPropertyStr(ctx, pair, "value", JS_NewStringLen(ctx, value.data(), value.size()));
			JS_SetPropertyUint32(ctx, pairs, count++, pair);
		}
		if (amp == std::string::npos) break;
		start = amp + 1;
	}
	JS_SetPropertyStr(ctx, object, "__pixelPairs", pairs);
	JS_SetPropertyStr(ctx, object, "append", JS_NewCFunction(ctx, paramsAppend, "append", 2));
	JS_SetPropertyStr(ctx, object, "get", JS_NewCFunction(ctx, paramsGet, "get", 1));
	JS_SetPropertyStr(ctx, object, "getAll", JS_NewCFunction(ctx, paramsGetAll, "getAll", 1));
	JS_SetPropertyStr(ctx, object, "set", JS_NewCFunction(ctx, paramsSet, "set", 2));
	JS_SetPropertyStr(ctx, object, "delete", JS_NewCFunction(ctx, paramsDelete, "delete", 1));
	JS_SetPropertyStr(ctx, object, "has", JS_NewCFunction(ctx, paramsHas, "has", 1));
	JS_SetPropertyStr(ctx, object, "toString", JS_NewCFunction(ctx, paramsToString, "toString", 0));
	JS_SetPropertyStr(ctx, object, "forEach", JS_NewCFunction(ctx, paramsForEach, "forEach", 1));
	return object;
}

static JSValue paramsAppend(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (argc < 2) return JS_UNDEFINED;
	JSValue pairs = JS_GetPropertyStr(ctx, thisVal, "__pixelPairs"); const uint32_t n = jsArrayLength(ctx, pairs);
	if (n >= 256) { JS_FreeValue(ctx, pairs); return JS_ThrowRangeError(ctx, "URLSearchParams limit exceeded"); }
	JSValue pair = JS_NewObject(ctx); const std::string name = jsString(ctx, argv[0]);
	JS_SetPropertyStr(ctx, pair, "name", JS_NewStringLen(ctx, name.data(), name.size()));
	const std::string value = jsString(ctx, argv[1]);
	JS_SetPropertyStr(ctx, pair, "value", JS_NewStringLen(ctx, value.data(), value.size()));
	JS_SetPropertyUint32(ctx, pairs, n, pair); JS_FreeValue(ctx, pairs); return JS_UNDEFINED;
}
static JSValue paramsGetAll(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	JSValue result = JS_NewArray(ctx); if (!argc) return result;
	const std::string name = jsString(ctx, argv[0]); JSValue pairs = JS_GetPropertyStr(ctx, thisVal, "__pixelPairs");
	uint32_t output = 0;
	for (uint32_t i = 0; i < jsArrayLength(ctx, pairs); ++i)
	{
		JSValue pair = JS_GetPropertyUint32(ctx, pairs, i), key = JS_GetPropertyStr(ctx, pair, "name");
		if (jsString(ctx, key) == name) { JSValue value = JS_GetPropertyStr(ctx, pair, "value"); JS_SetPropertyUint32(ctx, result, output++, value); }
		JS_FreeValue(ctx, key); JS_FreeValue(ctx, pair);
	}
	JS_FreeValue(ctx, pairs); return result;
}
static JSValue paramsGet(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	JSValue all = paramsGetAll(ctx, thisVal, argc, argv); JSValue value = JS_GetPropertyUint32(ctx, all, 0);
	JS_FreeValue(ctx, all); return JS_IsUndefined(value) ? JS_NULL : value;
}
static JSValue paramsDelete(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (!argc) return JS_UNDEFINED;
	const std::string name = jsString(ctx, argv[0]); JSValue pairs = JS_GetPropertyStr(ctx, thisVal, "__pixelPairs");
	uint32_t i = 0, n = jsArrayLength(ctx, pairs);
	while (i < n)
	{
		JSValue pair = JS_GetPropertyUint32(ctx, pairs, i), key = JS_GetPropertyStr(ctx, pair, "name");
		const bool match = jsString(ctx, key) == name; JS_FreeValue(ctx, key); JS_FreeValue(ctx, pair);
		if (match)
		{
			for (uint32_t j = i + 1; j < n; ++j) JS_SetPropertyUint32(ctx, pairs, j - 1, JS_GetPropertyUint32(ctx, pairs, j));
			JS_SetPropertyStr(ctx, pairs, "length", JS_NewUint32(ctx, --n));
		}
		else ++i;
	}
	JS_FreeValue(ctx, pairs); return JS_UNDEFINED;
}
static JSValue paramsSet(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (argc < 2) return JS_UNDEFINED;
	const std::string name = jsString(ctx, argv[0]), value = jsString(ctx, argv[1]);
	JSValue pairs = JS_GetPropertyStr(ctx, thisVal, "__pixelPairs"); uint32_t n = jsArrayLength(ctx, pairs), first = UINT32_MAX;
	for (uint32_t i = 0; i < n; )
	{
		JSValue pair = JS_GetPropertyUint32(ctx, pairs, i), key = JS_GetPropertyStr(ctx, pair, "name");
		const bool match = jsString(ctx, key) == name; JS_FreeValue(ctx, key); JS_FreeValue(ctx, pair);
		if (match)
		{
			if (first == UINT32_MAX)
			{
				first = i; JSValue replacement = JS_NewObject(ctx);
				JS_SetPropertyStr(ctx, replacement, "name", JS_NewStringLen(ctx, name.data(), name.size()));
				JS_SetPropertyStr(ctx, replacement, "value", JS_NewStringLen(ctx, value.data(), value.size()));
				JS_SetPropertyUint32(ctx, pairs, i, replacement); ++i;
			}
			else { for (uint32_t j = i + 1; j < n; ++j) JS_SetPropertyUint32(ctx, pairs, j - 1, JS_GetPropertyUint32(ctx, pairs, j)); JS_SetPropertyStr(ctx, pairs, "length", JS_NewUint32(ctx, --n)); }
		}
		else ++i;
	}
	JS_FreeValue(ctx, pairs);
	if (first == UINT32_MAX) { JSValue args[2] = { JS_NewStringLen(ctx, name.data(), name.size()), JS_NewStringLen(ctx, value.data(), value.size()) }; JSValue result = paramsAppend(ctx, thisVal, 2, args); JS_FreeValue(ctx, args[0]); JS_FreeValue(ctx, args[1]); return result; }
	return JS_UNDEFINED;
}
static JSValue paramsHas(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (!argc) return JS_FALSE;
	JSValue all = paramsGetAll(ctx, thisVal, argc, argv);
	const bool found = jsArrayLength(ctx, all) != 0;
	JS_FreeValue(ctx, all);
	return JS_NewBool(ctx, found);
}
static JSValue paramsToString(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JSValue pairs = JS_GetPropertyStr(ctx, thisVal, "__pixelPairs"); std::string result;
	bool first = true;
	for (uint32_t i = 0; i < jsArrayLength(ctx, pairs); ++i)
	{
		JSValue pair = JS_GetPropertyUint32(ctx, pairs, i), key = JS_GetPropertyStr(ctx, pair, "name"), value = JS_GetPropertyStr(ctx, pair, "value");
		if (!first) result += '&';
		first = false;
		std::string encodedKey = urlEncode(jsString(ctx, key)), encodedValue = urlEncode(jsString(ctx, value));
		for (size_t pos; (pos = encodedKey.find("%20")) != std::string::npos; ) encodedKey.replace(pos, 3, "+");
		for (size_t pos; (pos = encodedValue.find("%20")) != std::string::npos; ) encodedValue.replace(pos, 3, "+");
		result += encodedKey + "=" + encodedValue;
		JS_FreeValue(ctx, pair); JS_FreeValue(ctx, key); JS_FreeValue(ctx, value);
	}
	JS_FreeValue(ctx, pairs); return JS_NewStringLen(ctx, result.data(), result.size());
}
static JSValue paramsForEach(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (!argc || !JS_IsFunction(ctx, argv[0])) return JS_ThrowTypeError(ctx, "forEach callback must be a function");
	JSValue pairs = JS_GetPropertyStr(ctx, thisVal, "__pixelPairs"); const uint32_t n = jsArrayLength(ctx, pairs);
	for (uint32_t i = 0; i < n; ++i)
	{
		JSValue pair = JS_GetPropertyUint32(ctx, pairs, i), key = JS_GetPropertyStr(ctx, pair, "name"), value = JS_GetPropertyStr(ctx, pair, "value");
		JSValue args[3] = { value, key, JS_DupValue(ctx, thisVal) }; JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 3, args);
		JS_FreeValue(ctx, args[0]); JS_FreeValue(ctx, args[1]); JS_FreeValue(ctx, args[2]); JS_FreeValue(ctx, result); JS_FreeValue(ctx, pair);
	}
	JS_FreeValue(ctx, pairs); return JS_UNDEFINED;
}
static JSValue urlSearchParamsConstructor(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	return newParamsObject(ctx, argc ? jsString(ctx, argv[0]) : std::string());
}

static JSValue urlConstructor(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	if (!argc) return JS_ThrowTypeError(ctx, "URL requires an input");
	const std::string input = jsString(ctx, argv[0]);
	const std::string base = argc > 1 ? jsString(ctx, argv[1]) : std::string();
	const std::string href = urlJoin(base, input);
	if (href.find("://") == std::string::npos) return JS_ThrowTypeError(ctx, "Invalid URL");
	JSValue object = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, object, "href", JS_NewStringLen(ctx, href.data(), href.size()));
	const char* names[] = { "protocol", "origin", "host", "hostname", "port", "pathname", "search", "hash" };
	for (int i = 0; i < 8; ++i)
	{
		const std::string value = urlComponentValue(href, i + 1);
		JS_SetPropertyStr(ctx, object, names[i], JS_NewStringLen(ctx, value.data(), value.size()));
	}
	const std::string search = urlComponentValue(href, 7);
	JS_SetPropertyStr(ctx, object, "searchParams", newParamsObject(ctx, search));
	JS_SetPropertyStr(ctx, object, "toString", JS_NewCFunction(ctx, urlToString, "toString", 0));
	JS_SetPropertyStr(ctx, object, "toJSON", JS_NewCFunction(ctx, urlToString, "toJSON", 0));
	return object;
}
static JSValue historyGo(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	JSPageBinding* binding = pageBinding(ctx); if (!binding || !argc) return JS_UNDEFINED;
	int32_t delta = 0; JS_ToInt32(ctx, &delta, argv[0]);
	binding->historyDelta = clamp(delta, -20, 20);
	binding->navigationMode = 4;
	return JS_UNDEFINED;
}
static JSValue historyBack(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JSValue delta = JS_NewInt32(ctx, -1); JSValue result = historyGo(ctx, thisVal, 1, &delta); JS_FreeValue(ctx, delta); return result;
}
static JSValue historyForward(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JSValue delta = JS_NewInt32(ctx, 1); JSValue result = historyGo(ctx, thisVal, 1, &delta); JS_FreeValue(ctx, delta); return result;
}

static int nodeIndex(JSContext* ctx, JSValueConst object)
{
	JSValue value = JS_GetPropertyStr(ctx, object, "__pixelNode");
	int32_t index = -1;
	JS_ToInt32(ctx, &index, value);
	JS_FreeValue(ctx, value);
	Document* doc = boundDocument(ctx);
	return doc && index >= 0 && (size_t)index < doc->nodes.size() ? index : -1;
}
static uint32_t jsArrayLength(JSContext* ctx, JSValueConst array);

static std::string cssCamelName(std::string name)
{
	for (size_t i = 0; i < name.size(); )
	{
		if (name[i] == '-' && i + 1 < name.size())
		{
			name.erase(i, 1); name[i] = (char)std::toupper((unsigned char)name[i]);
		}
		else ++i;
	}
	return name;
}
static JSValue computedStyleGetProperty(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (!argc) return JS_NewString(ctx, "");
	const std::string property = cssCamelName(jsString(ctx, argv[0]));
	JSValue value = JS_GetPropertyStr(ctx, thisVal, property.c_str());
	if (JS_IsUndefined(value)) { JS_FreeValue(ctx, value); return JS_NewString(ctx, ""); }
	return value;
}
static JSValue computedStyleItem(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (!argc) return JS_NewString(ctx, "");
	int32_t index = -1; JS_ToInt32(ctx, &index, argv[0]);
	JSValue names = JS_GetPropertyStr(ctx, thisVal, "__pixelNames");
	JSValue value = index < 0 ? JS_UNDEFINED : JS_GetPropertyUint32(ctx, names, (uint32_t)index);
	JS_FreeValue(ctx, names);
	return JS_IsUndefined(value) ? JS_NewString(ctx, "") : value;
}
static JSValue computedStyleLength(JSContext* ctx, JSValueConst thisVal)
{
	JSValue names = JS_GetPropertyStr(ctx, thisVal, "__pixelNames");
	const uint32_t count = jsArrayLength(ctx, names); JS_FreeValue(ctx, names);
	return JS_NewUint32(ctx, count);
}
static JSValue getComputedStyle(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	JSValue style = JS_NewObject(ctx);
	if (!argc) return style;
	const int index = nodeIndex(ctx, argv[0]);
	JSPageBinding* binding = pageBinding(ctx);
	if (!binding || index < 0 || (size_t)index >= binding->computedStyles.size()) return style;
	JSValue names = JS_NewArray(ctx); uint32_t slot = 0;
	for (const auto& property : binding->computedStyles[(size_t)index])
	{
		JS_SetPropertyStr(ctx, style, property.first.c_str(), JS_NewStringLen(ctx, property.second.data(), property.second.size()));
		JS_SetPropertyUint32(ctx, names, slot++, JS_NewString(ctx, property.first.c_str()));
	}
	JS_SetPropertyStr(ctx, style, "__pixelNames", names);
	JS_SetPropertyStr(ctx, style, "getPropertyValue", JS_NewCFunction(ctx, computedStyleGetProperty, "getPropertyValue", 1));
	JS_SetPropertyStr(ctx, style, "item", JS_NewCFunction(ctx, computedStyleItem, "item", 1));
	JSAtom lengthAtom = JS_NewAtom(ctx, "length"); JSCFunctionType getter; getter.getter = computedStyleLength;
	JS_DefinePropertyGetSet(ctx, style, lengthAtom, JS_NewCFunction2(ctx, getter.generic, "get length", 0, JS_CFUNC_getter, 0), JS_UNDEFINED, JS_PROP_CONFIGURABLE);
	JS_FreeAtom(ctx, lengthAtom);
	return style;
}

static JSValue wrapNode(JSContext* ctx, int index);

static void appendEscapedHtml(std::string& out, const std::string& value, bool attribute)
{
	for (char ch : value)
	{
		if (ch == '&') out += "&amp;";
		else if (ch == '<') out += "&lt;";
		else if (ch == '>') out += "&gt;";
		else if (attribute && ch == '"') out += "&quot;";
		else out.push_back(ch);
		if (out.size() >= 32768) break;
	}
}

static void serializeHtmlNode(const Document& doc, int index, std::string& out, int depth)
{
	if (index < 0 || (size_t)index >= doc.nodes.size() || depth > 32 || out.size() >= 32768) return;
	const Node& node = doc.nodes[(size_t)index];
	if (node.type == NodeType::Text)
	{
		appendEscapedHtml(out, doc.nodeText(index), false);
		return;
	}
	if (node.type == NodeType::RawText)
	{
		out += doc.nodeText(index).substr(0, 32768 - out.size());
		return;
	}
	if (node.type == NodeType::Comment)
	{
		out += "<!--";
		out += doc.nodeText(index).substr(0, 32768 - out.size());
		if (out.size() < 32768) out += "-->";
		return;
	}
	const bool element = node.isElement();
	const std::string tag = element ? node.tag.c_str() : std::string();
	if (element)
	{
		out += "<" + tag;
		for (const Attribute& attr : node.attrs)
		{
			if (out.size() >= 32768) return;
			out += " " + attr.name + "=\"";
			appendEscapedHtml(out, attr.value, true);
			out += "\"";
		}
		out += ">";
		if (tag == "area" || tag == "base" || tag == "br" || tag == "col" ||
			tag == "embed" || tag == "hr" || tag == "img" || tag == "input" ||
			tag == "link" || tag == "meta" || tag == "source" || tag == "wbr") return;
	}
	for (int child : node.children) serializeHtmlNode(doc, child, out, depth + 1);
	if (element && out.size() < 32768) out += "</" + tag + ">";
}

static int cloneDomNode(Document& dst, const Document& src, int srcIndex, int parent)
{
	if (srcIndex < 0 || (size_t)srcIndex >= src.nodes.size()) return -1;
	Node copy = src.nodes[(size_t)srcIndex];
	copy.children.clear();
	copy.parent = parent;
	copy.layoutLine = -1;
	copy.textOffset = 0;
	copy.textLength = 0;
	const int index = dst.addNode(std::move(copy));
	if (parent >= 0) dst.nodes[(size_t)parent].children.push_back(index);
	const NodeType sourceType = src.nodes[(size_t)srcIndex].type;
	if (sourceType == NodeType::Text || sourceType == NodeType::RawText ||
		sourceType == NodeType::Comment)
	{
		if (!dst.setNodeText(index, src.nodeText(srcIndex))) return -1;
	}
	for (int child : src.nodes[(size_t)srcIndex].children)
		cloneDomNode(dst, src, child, index);
	return index;
}

static bool hasClass(const Node& node, const std::string& wanted)
{
	const char* classes = node.getAttr("class");
	if (!classes) return false;
	const std::string all(classes);
	size_t p = 0;
	while (p < all.size())
	{
		while (p < all.size() && isspace((unsigned char)all[p])) ++p;
		size_t e = all.find_first_of(" \t\r\n\f", p);
		if (all.substr(p, e == std::string::npos ? e : e - p) == wanted) return true;
		if (e == std::string::npos) break;
		p = e + 1;
	}
	return false;
}

static std::string trimSelector(const std::string& value)
{
	size_t start = 0, end = value.size();
	while (start < end && isspace((unsigned char)value[start])) ++start;
	while (end > start && isspace((unsigned char)value[end - 1])) --end;
	return value.substr(start, end - start);
}

static bool simpleSelectorMatches(const Document& doc, int index, const std::string& source)
{
	if (index < 0 || (size_t)index >= doc.nodes.size()) return false;
	const Node& node = doc.nodes[(size_t)index];
	if (!node.isElement()) return false;
	const std::string selector = trimSelector(source);
	if (selector.empty()) return false;
	size_t p = 0;
	while (p < selector.size() && selector[p] != '#' && selector[p] != '.' &&
		selector[p] != '[' && selector[p] != ':') ++p;
	if (p > 0 && selector.substr(0, p) != "*" &&
		std::string(node.tag) != toLower(selector.substr(0, p))) return false;
	while (p < selector.size())
	{
		const char marker = selector[p++];
		if (marker == '#' || marker == '.')
		{
			const size_t end = selector.find_first_of("#.[:", p);
			const std::string token = selector.substr(p, end == std::string::npos ? end : end - p);
			if (token.empty()) return false;
			if (marker == '#')
			{
				const char* id = node.getAttr("id");
				if (!id || token != id) return false;
			}
			else if (!hasClass(node, token)) return false;
			p = end == std::string::npos ? selector.size() : end;
		}
		else if (marker == '[')
		{
			const size_t end = selector.find(']', p);
			if (end == std::string::npos) return false;
			std::string expression = trimSelector(selector.substr(p, end - p));
			const size_t equal = expression.find('=');
			const std::string name = trimSelector(expression.substr(0, equal));
			const char* value = node.getAttr(name.c_str());
			if (!value) return false;
			if (equal != std::string::npos)
			{
				std::string wanted = trimSelector(expression.substr(equal + 1));
				if (wanted.size() >= 2 && (wanted.front() == '\'' || wanted.front() == '"') && wanted.back() == wanted.front())
					wanted = wanted.substr(1, wanted.size() - 2);
				if (wanted != value) return false;
			}
			p = end + 1;
		}
		else if (marker == ':')
		{
			const size_t open = selector.find('(', p);
			const size_t nameEnd = open == std::string::npos ? selector.find_first_of("#.[:", p) : open;
			const std::string pseudo = selector.substr(p,
				nameEnd == std::string::npos ? nameEnd : nameEnd - p);
			if (pseudo == "not" && open != std::string::npos)
			{
				const size_t close = selector.find(')', open + 1);
				if (close == std::string::npos || simpleSelectorMatches(doc, index, selector.substr(open + 1, close - open - 1)))
					return false;
				p = close + 1;
			}
			else if (pseudo == "checked")
			{
				if (!node.hasAttr("checked")) return false;
				p = nameEnd == std::string::npos ? selector.size() : nameEnd;
			}
			else if (pseudo == "disabled")
			{
				if (!node.hasAttr("disabled")) return false;
				p = nameEnd == std::string::npos ? selector.size() : nameEnd;
			}
			else if (pseudo == "first-child")
			{
				if (node.parent < 0 || (size_t)node.parent >= doc.nodes.size()) return false;
				bool isFirst = false;
				for (int sibling : doc.nodes[(size_t)node.parent].children)
				{
					if (!doc.nodes[(size_t)sibling].isElement()) continue;
					isFirst = sibling == index;
					break;
				}
				if (!isFirst) return false;
				p = nameEnd == std::string::npos ? selector.size() : nameEnd;
			}
			else return false;
		}
	}
	return true;
}

static bool selectorMatches(const Document& doc, int index, const std::string& selector)
{
	if (index < 0 || (size_t)index >= doc.nodes.size() || selector.size() > 512) return false;
	// Les groupes séparés par des virgules sont évalués indépendamment.
	size_t groupStart = 0;
	while (groupStart < selector.size())
	{
		size_t groupEnd = groupStart;
		int bracketDepth = 0, parenDepth = 0;
		for (; groupEnd < selector.size(); ++groupEnd)
		{
			const char ch = selector[groupEnd];
			if (ch == '[') ++bracketDepth; else if (ch == ']') --bracketDepth;
			else if (ch == '(') ++parenDepth; else if (ch == ')') --parenDepth;
			else if (ch == ',' && bracketDepth == 0 && parenDepth == 0) break;
		}
		const std::string group = selector.substr(groupStart, groupEnd - groupStart);
		std::vector<std::string> parts;
		std::vector<char> combinators;
		std::string part;
		char pending = ' ';
		bracketDepth = parenDepth = 0;
		auto pushPart = [&]() {
			const std::string trimmed = trimSelector(part);
			if (trimmed.empty()) return;
			if (!parts.empty()) combinators.push_back(pending);
			parts.push_back(trimmed); part.clear(); pending = ' ';
		};
		for (char ch : group)
		{
			if (ch == '[') ++bracketDepth; else if (ch == ']') --bracketDepth;
			else if (ch == '(') ++parenDepth; else if (ch == ')') --parenDepth;
			if (bracketDepth == 0 && parenDepth == 0 &&
				(ch == '>' || ch == '+' || ch == '~'))
			{
				pushPart(); pending = ch; continue;
			}
			if (bracketDepth == 0 && parenDepth == 0 && isspace((unsigned char)ch))
			{
				pushPart(); continue;
			}
			part.push_back(ch);
		}
		pushPart();
		if (!parts.empty())
		{
			int current = index;
			bool matched = simpleSelectorMatches(doc, current, parts.back());
			for (int partIndex = (int)parts.size() - 1; matched && partIndex > 0; --partIndex)
			{
				const char combinator = combinators[(size_t)partIndex - 1];
				int parent = doc.nodes[(size_t)current].parent;
				if (combinator == '>')
				{
					matched = parent >= 0 && (size_t)parent < doc.nodes.size() &&
						simpleSelectorMatches(doc, parent, parts[(size_t)partIndex - 1]);
					current = parent;
				}
				else if (combinator == '+' || combinator == '~')
				{
					matched = false;
					if (parent >= 0 && (size_t)parent < doc.nodes.size())
					{
						const auto& siblings = doc.nodes[(size_t)parent].children;
						auto it = std::find(siblings.begin(), siblings.end(), current);
						while (it != siblings.begin() && it != siblings.end())
						{
							--it;
							if (!doc.nodes[(size_t)*it].isElement()) continue;
							const bool siblingMatches = simpleSelectorMatches(doc, *it, parts[(size_t)partIndex - 1]);
							if (siblingMatches) { current = *it; matched = true; break; }
							if (combinator == '+') break;
						}
					}
				}
				else
				{
					matched = false;
					while (parent >= 0 && (size_t)parent < doc.nodes.size())
					{
						if (simpleSelectorMatches(doc, parent, parts[(size_t)partIndex - 1]))
						{ current = parent; matched = true; break; }
						parent = doc.nodes[(size_t)parent].parent;
					}
				}
			}
			if (matched) return true;
		}
		if (groupEnd == selector.size()) break;
		groupStart = groupEnd + 1;
	}
	return false;
}

static int findSelector(const Document& doc, const std::string& selector, int first)
{
	if (doc.root < 0) return -1;
	std::vector<int> pending(1, doc.root);
	int found = -1;
	while (!pending.empty())
	{
		int i = pending.back(); pending.pop_back();
		if (selectorMatches(doc, i, selector))
		{
			if (first) return i;
			found = i;
		}
		for (auto it = doc.nodes[i].children.rbegin(); it != doc.nodes[i].children.rend(); ++it)
			pending.push_back(*it);
	}
	return found;
}

static bool nodeConnected(const Document& doc, int index)
{
	if (index < 0 || (size_t)index >= doc.nodes.size()) return false;
	for (size_t steps = 0; steps <= doc.nodes.size(); ++steps)
	{
		if (index == doc.root) return true;
		index = doc.nodes[(size_t)index].parent;
		if (index < 0 || (size_t)index >= doc.nodes.size()) return false;
	}
	return false;
}

static JSValue wrapClassList(JSContext* ctx, int idx);
static JSValue wrapStyle(JSContext* ctx, int idx);

static JSValue domNodeGet(JSContext* ctx, JSValueConst thisVal, int magic)
{
	Document* doc = boundDocument(ctx);
	int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0) return JS_UNDEFINED;
	const Node& node = doc->nodes[(size_t)idx];
	if (magic == 0 || magic == 1)
	{
		std::string text;
		if (magic == 0) text = doc->textOf(idx);
		else for (int child : node.children) serializeHtmlNode(*doc, child, text, 0);
		return JS_NewStringLen(ctx, text.data(), text.size());
	}
	if (magic == 7 || magic == 8)
	{
		const int child = magic == 7 ? node.parent :
			(node.children.empty() ? -1 : node.children.front());
		return wrapNode(ctx, child);
	}
	if (magic == 15 || magic == 16)
	{
		if (node.parent < 0 || (size_t)node.parent >= doc->nodes.size()) return JS_NULL;
		const auto& siblings = doc->nodes[(size_t)node.parent].children;
		auto it = std::find(siblings.begin(), siblings.end(), idx);
		if (it == siblings.end()) return JS_NULL;
		if (magic == 15) return wrapNode(ctx, it + 1 == siblings.end() ? -1 : *(it + 1));
		return wrapNode(ctx, it == siblings.begin() ? -1 : *(it - 1));
	}
	if (magic == 17) return node.parent >= 0 && (size_t)node.parent < doc->nodes.size() &&
		doc->nodes[(size_t)node.parent].isElement() ? wrapNode(ctx, node.parent) : JS_NULL;
	if (magic == 18)
	{
		int count = 0;
		for (int child : node.children) if (doc->nodes[(size_t)child].isElement()) ++count;
		return JS_NewInt32(ctx, count);
	}
	if (magic == 14) return wrapClassList(ctx, idx);
	if (magic == 19) return wrapStyle(ctx, idx);
	if (magic == 20)
	{
		for (int parent = node.parent; parent >= 0 && (size_t)parent < doc->nodes.size();
			parent = doc->nodes[(size_t)parent].parent)
			if (doc->nodes[(size_t)parent].tag == "form") return wrapNode(ctx, parent);
		return JS_NULL;
	}
	if (magic == 9 || magic == 10)
	{
		JSValue array = JS_NewArray(ctx);
		uint32_t count = 0;
		for (int child : node.children)
		{
			if (magic == 10 && !doc->nodes[(size_t)child].isElement()) continue;
			JS_SetPropertyUint32(ctx, array, count++, wrapNode(ctx, child));
		}
		return array;
	}
	if (magic == 4) return JS_NewString(ctx, node.tag.c_str());
	if (magic == 12 || magic == 13 || magic == 22 || magic == 23 || magic == 24)
		return JS_NewBool(ctx, node.hasAttr(magic == 12 ? "checked" : magic == 13 ? "disabled" :
			magic == 22 ? "selected" : magic == 23 ? "required" : "readonly"));
	const char* name = magic == 2 ? "id" : magic == 3 ? "class" :
		magic == 5 ? "value" : magic == 6 ? "href" : magic == 11 ? "name" :
		magic == 21 ? "type" : "href";
	const char* value = node.getAttr(name);
	if (magic == 5 && node.tag == "textarea" && !value)
	{
		const std::string text = doc->textOf(idx);
		return JS_NewStringLen(ctx, text.data(), text.size());
	}
	if (magic == 5 && node.tag == "select" && !value)
	{
		int firstOption = -1, selectedOption = -1;
		for (int child : node.children)
			if (doc->nodes[(size_t)child].isElement() && doc->nodes[(size_t)child].tag == "option")
			{
				if (firstOption < 0) firstOption = child;
				if (doc->nodes[(size_t)child].hasAttr("selected")) { selectedOption = child; break; }
			}
		const int option = selectedOption >= 0 ? selectedOption : firstOption;
		if (option >= 0)
		{
			const char* optionValue = doc->nodes[(size_t)option].getAttr("value");
			if (optionValue) return JS_NewString(ctx, optionValue);
			const std::string text = doc->textOf(option);
			return JS_NewStringLen(ctx, text.data(), text.size());
		}
		return JS_NewString(ctx, "");
	}
	if (magic == 5 && !value && node.tag == "input")
	{
		const char* inputType = node.getAttr("type");
		if (inputType && (toLower(inputType) == "checkbox" || toLower(inputType) == "radio"))
			return JS_NewString(ctx, "on");
	}
	return value ? JS_NewString(ctx, value) : JS_NewString(ctx, "");
}

static JSValue domNodeSet(JSContext* ctx, JSValueConst thisVal, JSValueConst value, int magic)
{
	Document* doc = boundDocument(ctx);
	int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0) return JS_UNDEFINED;
	std::string s = jsString(ctx, value);
	Node& node = doc->nodes[(size_t)idx];
	if (magic == 0 || magic == 1)
	{
		if (node.type == NodeType::Text && magic == 0)
		{
			if (!doc->setNodeText(idx, s))
				return JS_ThrowRangeError(ctx, "DOM text limit exceeded");
			return JS_UNDEFINED;
		}
		if (node.type == NodeType::Text && magic == 1)
			return JS_ThrowTypeError(ctx, "innerHTML is only available on elements");
		if (magic == 0 && node.children.size() == 1)
		{
			const int onlyChild = node.children[0];
			if (onlyChild >= 0 && (size_t)onlyChild < doc->nodes.size() &&
				doc->nodes[(size_t)onlyChild].type == NodeType::Text)
			{
				if (!doc->setNodeText(onlyChild, s))
					return JS_ThrowRangeError(ctx, "DOM text limit exceeded");
				return JS_UNDEFINED;
			}
		}
		if (magic == 0 && !s.empty() && doc->nodes.size() >= NW_MAX_DOM_NODES)
			return JS_ThrowRangeError(ctx, "DOM node limit exceeded");
		Document fragment;
		int fragmentBody = -1;
		if (magic == 1 && s.size() > 32768)
			return JS_ThrowRangeError(ctx, "innerHTML exceeds the page limit");
		if (magic == 1 && !s.empty())
		{
			if (!htmlParse("<html><body>" + s + "</body></html>", fragment))
				return JS_ThrowTypeError(ctx, "Invalid innerHTML fragment");
			if (doc->nodes.size() + fragment.nodes.size() > NW_MAX_DOM_NODES)
				return JS_ThrowRangeError(ctx, "DOM node limit exceeded");
			fragmentBody = fragment.firstByTag(fragment.root, "body");
		}
		doc->clearChildren(idx);
		doc->clearNodeText(idx);
		node.type = NodeType::Element;
		if (magic == 1 && fragmentBody >= 0)
		{
			for (int child : fragment.nodes[(size_t)fragmentBody].children)
				cloneDomNode(*doc, fragment, child, idx);
		}
		else if (!s.empty()) doc->createText(idx, std::move(s));
	}
	else
	{
			if (magic == 5 && node.tag == "textarea")
			{
				doc->clearChildren(idx);
				doc->createText(idx, s);
			}
			else if (magic == 5 && node.tag == "select")
			{
				for (int child : node.children)
					if (doc->nodes[(size_t)child].isElement() && doc->nodes[(size_t)child].tag == "option")
					{
						const char* optionValue = doc->nodes[(size_t)child].getAttr("value");
						const std::string candidate = optionValue ? optionValue : doc->textOf(child);
						if (candidate == s) doc->setAttr(child, "selected", "");
						else doc->removeAttr(child, "selected");
					}
			}
		if (magic == 12 || magic == 13 || magic == 22 || magic == 23 || magic == 24)
		{
			const char* name = magic == 12 ? "checked" : magic == 13 ? "disabled" :
				magic == 22 ? "selected" : magic == 23 ? "required" : "readonly";
			if (JS_ToBool(ctx, value)) doc->setAttr(idx, name, "");
			else doc->removeAttr(idx, name);
		}
		else
		{
			const char* name = magic == 2 ? "id" : magic == 3 ? "class" :
				magic == 5 ? "value" : magic == 6 ? "href" : magic == 11 ? "name" :
				magic == 21 ? "type" : "href";
			doc->setAttr(idx, name, s);
		}
	}
	return JS_UNDEFINED;
}

static JSValue domAppendChild(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	int parent = nodeIndex(ctx, thisVal);
	int child = argc ? nodeIndex(ctx, argv[0]) : -1;
	if (!doc || !doc->insertChild(parent, child))
		return JS_ThrowTypeError(ctx, "Invalid DOM insertion");
	return wrapNode(ctx, child);
}

static JSValue domInsertBefore(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	const int parent = nodeIndex(ctx, thisVal);
	const int child = argc > 0 ? nodeIndex(ctx, argv[0]) : -1;
	const bool hasBefore = argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]);
	const int before = hasBefore ? nodeIndex(ctx, argv[1]) : -1;
	if (!doc || child < 0 || (hasBefore && before < 0) ||
		!doc->insertChild(parent, child, before))
		return JS_ThrowTypeError(ctx, "Invalid DOM insertion");
	return wrapNode(ctx, child);
}

static JSValue domRemoveChild(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	int parent = nodeIndex(ctx, thisVal);
	int child = argc ? nodeIndex(ctx, argv[0]) : -1;
	if (!doc || parent < 0 || child < 0 || (size_t)child >= doc->nodes.size() ||
		doc->nodes[(size_t)child].parent != parent)
		return JS_ThrowTypeError(ctx, "Node is not a child of this element");
	doc->detachNode(child);
	return wrapNode(ctx, child);
}

static JSValue domRemove(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	Document* doc = boundDocument(ctx);
	int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0) return JS_UNDEFINED;
	doc->detachNode(idx);
	return JS_UNDEFINED;
}

static std::string eventTargetKey(JSContext* ctx, JSValueConst thisVal)
{
	JSValue node = JS_GetPropertyStr(ctx, thisVal, "__pixelNode");
	if (JS_IsNumber(node))
	{
		int32_t idx = -1;
		JS_ToInt32(ctx, &idx, node);
		JS_FreeValue(ctx, node);
		return "node:" + std::to_string(idx);
	}
	JS_FreeValue(ctx, node);
	JSValue target = JS_GetPropertyStr(ctx, thisVal, "__pixelTarget");
	std::string key = jsString(ctx, target);
	JS_FreeValue(ctx, target);
	return key;
}

static JSValue eventListenerList(JSContext* ctx)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue list = JS_GetPropertyStr(ctx, global, "__pixelListeners");
	if (JS_IsUndefined(list))
	{
		JS_FreeValue(ctx, list);
		list = JS_NewArray(ctx);
		JS_SetPropertyStr(ctx, global, "__pixelListeners", JS_DupValue(ctx, list));
	}
	JS_FreeValue(ctx, global);
	return list;
}

static uint32_t jsArrayLength(JSContext* ctx, JSValueConst array)
{
	JSValue value = JS_GetPropertyStr(ctx, array, "length");
	uint32_t length = 0;
	JS_ToUint32(ctx, &length, value);
	JS_FreeValue(ctx, value);
	return length;
}

static void compactEventListeners(JSContext* ctx, JSValueConst list)
{
	uint32_t length = jsArrayLength(ctx, list);
	for (uint32_t i = 0; i < length; )
	{
		JSValue item = JS_GetPropertyUint32(ctx, list, i);
		JSValue removed = JS_GetPropertyStr(ctx, item, "removed");
		const bool discard = JS_ToBool(ctx, removed) != 0;
		JS_FreeValue(ctx, removed); JS_FreeValue(ctx, item);
		if (!discard) { ++i; continue; }
		for (uint32_t j = i + 1; j < length; ++j)
			JS_SetPropertyUint32(ctx, (JSValue)list, j - 1, JS_GetPropertyUint32(ctx, list, j));
		--length;
		JS_SetPropertyStr(ctx, (JSValue)list, "length", JS_NewUint32(ctx, length));
	}
}

static bool executePendingJobs(JSRuntime* runtime, JSContext* fallbackContext)
{
	while (JS_IsJobPending(runtime))
	{
		JSContext* jobContext = nullptr;
		const int result = JS_ExecutePendingJob(runtime, &jobContext);
		if (result < 0)
		{
			JSContext* exceptionContext = jobContext ? jobContext : fallbackContext;
			JSValue exception = JS_GetException(exceptionContext);
			JS_FreeValue(exceptionContext, exception);
			return false;
		}
		if (result == 0) break;
	}
	return true;
}

static JSValue cryptoGetRandomValues(JSContext* ctx, JSValueConst, int argc,
	JSValueConst* argv)
{
	if (argc < 1) return JS_ThrowTypeError(ctx, "Expected an integer typed array");
	size_t offset = 0, byteLength = 0, bytesPerElement = 0;
	JSValue buffer = JS_GetTypedArrayBuffer(ctx, argv[0], &offset,
		&byteLength, &bytesPerElement);
	if (JS_IsException(buffer)) return buffer;
	JSValue constructor = JS_GetPropertyStr(ctx, argv[0], "constructor");
	JSValue constructorName = JS_GetPropertyStr(ctx, constructor, "name");
	const std::string typeName = jsString(ctx, constructorName);
	JS_FreeValue(ctx, constructorName);
	JS_FreeValue(ctx, constructor);
	if (typeName != "Uint8Array" && typeName != "Uint8ClampedArray" &&
		typeName != "Uint16Array" && typeName != "Uint32Array")
	{
		JS_FreeValue(ctx, buffer);
		return JS_ThrowTypeError(ctx, "getRandomValues requires an unsigned integer typed array");
	}
	if (!bytesPerElement || byteLength > 65536)
	{
		JS_FreeValue(ctx, buffer);
		return JS_ThrowRangeError(ctx, "getRandomValues accepts at most 65536 bytes");
	}
	size_t bufferLength = 0;
	uint8_t* bytes = JS_GetArrayBuffer(ctx, &bufferLength, buffer);
	if (!bytes || offset > bufferLength || byteLength > bufferLength - offset)
	{
		JS_FreeValue(ctx, buffer);
		return JS_ThrowTypeError(ctx, "Expected an integer typed array");
	}
	if (byteLength == 0)
	{
		JS_FreeValue(ctx, buffer);
		return JS_DupValue(ctx, argv[0]);
	}
	const Result initResult = psInit();
	const Result randomResult = R_SUCCEEDED(initResult)
		? PS_GenerateRandomBytes(bytes + offset, byteLength) : initResult;
	if (R_SUCCEEDED(initResult)) psExit();
	JS_FreeValue(ctx, buffer);
	if (R_FAILED(randomResult)) return JS_ThrowInternalError(ctx, "System random service unavailable");
	return JS_DupValue(ctx, argv[0]);
}

static bool eventOption(JSContext* ctx, JSValueConst options, const char* name,
	bool booleanOptionDefault = false)
{
	if (JS_IsBool(options)) return booleanOptionDefault && JS_ToBool(ctx, options);
	if (!JS_IsObject(options) || JS_IsNull(options)) return false;
	JSValue value = JS_GetPropertyStr(ctx, options, name);
	const bool enabled = JS_ToBool(ctx, value) != 0;
	JS_FreeValue(ctx, value);
	return enabled;
}

static JSValue eventPreventDefault(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JSValue cancelable = JS_GetPropertyStr(ctx, thisVal, "__pixelCancelable");
	if (JS_ToBool(ctx, cancelable))
		JS_SetPropertyStr(ctx, (JSValue)thisVal, "defaultPrevented", JS_NewBool(ctx, 1));
	JS_FreeValue(ctx, cancelable);
	return JS_UNDEFINED;
}

static JSValue eventStopPropagation(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	JS_SetPropertyStr(ctx, (JSValue)thisVal, "__pixelStop", JS_NewBool(ctx, 1));
	return JS_UNDEFINED;
}

static JSValue eventTargetAdd(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (argc < 2 || !JS_IsFunction(ctx, argv[1])) return JS_UNDEFINED;
	const std::string type = toLower(jsString(ctx, argv[0]));
	const std::string target = eventTargetKey(ctx, thisVal);
	JSValue list = eventListenerList(ctx);
	compactEventListeners(ctx, list);
	uint32_t length = jsArrayLength(ctx, list);
	if (length >= 128) { JS_FreeValue(ctx, list); return JS_UNDEFINED; }
	for (uint32_t i = 0; i < length; ++i)
	{
		JSValue item = JS_GetPropertyUint32(ctx, list, i);
		JSValue itemType = JS_GetPropertyStr(ctx, item, "type");
		JSValue itemTarget = JS_GetPropertyStr(ctx, item, "target");
		JSValue callback = JS_GetPropertyStr(ctx, item, "callback");
		JSValue itemCapture = JS_GetPropertyStr(ctx, item, "capture");
		JSValue removed = JS_GetPropertyStr(ctx, item, "removed");
		const bool duplicate = !JS_ToBool(ctx, removed) && jsString(ctx, itemType) == type &&
			jsString(ctx, itemTarget) == target &&
			(JS_ToBool(ctx, itemCapture) != 0) == eventOption(ctx, argc > 2 ? argv[2] : JS_UNDEFINED, "capture", true) &&
			JS_VALUE_GET_TAG(callback) == JS_VALUE_GET_TAG(argv[1]) &&
			JS_VALUE_GET_PTR(callback) == JS_VALUE_GET_PTR(argv[1]);
		JS_FreeValue(ctx, itemType); JS_FreeValue(ctx, itemTarget);
		JS_FreeValue(ctx, callback); JS_FreeValue(ctx, itemCapture);
		JS_FreeValue(ctx, removed); JS_FreeValue(ctx, item);
		if (duplicate) { JS_FreeValue(ctx, list); return JS_UNDEFINED; }
	}
	JSValue item = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, item, "type", JS_NewString(ctx, type.c_str()));
	JS_SetPropertyStr(ctx, item, "target", JS_NewString(ctx, target.c_str()));
	JS_SetPropertyStr(ctx, item, "callback", JS_DupValue(ctx, argv[1]));
	JSValueConst options = argc > 2 ? argv[2] : JS_UNDEFINED;
	JS_SetPropertyStr(ctx, item, "capture", JS_NewBool(ctx, eventOption(ctx, options, "capture", true)));
	JS_SetPropertyStr(ctx, item, "once", JS_NewBool(ctx, eventOption(ctx, options, "once")));
	JS_SetPropertyStr(ctx, item, "removed", JS_NewBool(ctx, 0));
	JS_SetPropertyUint32(ctx, list, length, item);
	JS_FreeValue(ctx, list);
	return JS_UNDEFINED;
}

static JSValue eventTargetRemove(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	if (argc < 2) return JS_UNDEFINED;
	const std::string type = toLower(jsString(ctx, argv[0]));
	const std::string target = eventTargetKey(ctx, thisVal);
	JSValueConst options = argc > 2 ? argv[2] : JS_UNDEFINED;
	const bool capture = eventOption(ctx, options, "capture", true);
	JSValue list = eventListenerList(ctx);
	uint32_t length = jsArrayLength(ctx, list);
	for (uint32_t i = 0; i < length; ++i)
	{
		JSValue item = JS_GetPropertyUint32(ctx, list, i);
		JSValue itemType = JS_GetPropertyStr(ctx, item, "type");
		JSValue itemTarget = JS_GetPropertyStr(ctx, item, "target");
		JSValue callback = JS_GetPropertyStr(ctx, item, "callback");
		JSValue itemCapture = JS_GetPropertyStr(ctx, item, "capture");
		JSValue removed = JS_GetPropertyStr(ctx, item, "removed");
		const bool match = jsString(ctx, itemType) == type &&
			jsString(ctx, itemTarget) == target &&
			(JS_ToBool(ctx, itemCapture) != 0) == capture && !JS_ToBool(ctx, removed) &&
			JS_VALUE_GET_TAG(callback) == JS_VALUE_GET_TAG(argv[1]) &&
			JS_VALUE_GET_PTR(callback) == JS_VALUE_GET_PTR(argv[1]);
		JS_FreeValue(ctx, itemType);
		JS_FreeValue(ctx, itemTarget);
		JS_FreeValue(ctx, callback);
		JS_FreeValue(ctx, itemCapture);
		JS_FreeValue(ctx, removed);
		JS_FreeValue(ctx, item);
		if (match)
		{
			JSValue item = JS_GetPropertyUint32(ctx, list, i);
			JS_SetPropertyStr(ctx, item, "removed", JS_NewBool(ctx, 1));
			JS_FreeValue(ctx, item);
		}
	}
	JS_FreeValue(ctx, list);
	return JS_UNDEFINED;
}

static JSValue domSetAttribute(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0 || argc < 2) return JS_UNDEFINED;
	const std::string name = toLower(jsString(ctx, argv[0]));
	doc->setAttr(idx, name, jsString(ctx, argv[1]));
	return JS_UNDEFINED;
}

static JSValue domGetAttribute(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0 || argc < 1) return JS_NULL;
	const std::string name = toLower(jsString(ctx, argv[0]));
	const char* value = doc->nodes[(size_t)idx].getAttr(name.c_str());
	return value ? JS_NewString(ctx, value) : JS_NULL;
}

static JSValue domRemoveAttribute(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	int idx = nodeIndex(ctx, thisVal);
	if (doc && idx >= 0 && argc)
		doc->removeAttr(idx, toLower(jsString(ctx, argv[0])));
	return JS_UNDEFINED;
}

static JSValue domHasAttribute(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0 || !argc) return JS_NewBool(ctx, 0);
	const std::string name = toLower(jsString(ctx, argv[0]));
	return JS_NewBool(ctx, doc->nodes[(size_t)idx].hasAttr(name.c_str()));
}

static const char* stylePropertyName(int magic)
{
	static const char* names[] = { "cssText", "color", "background-color", "display", "width", "height",
		"margin", "padding", "font-size", "font-weight", "text-align", "border", "opacity",
		"position", "top", "left", "right", "bottom", "overflow", "line-height", "min-width", "max-width" };
	return magic >= 0 && magic < (int)(sizeof(names) / sizeof(names[0])) ? names[magic] : "";
}

static std::string readStyleProperty(const Node& node, const std::string& wanted)
{
	const char* style = node.getAttr("style");
	if (!style) return "";
	const std::string text(style);
	size_t p = 0;
	while (p < text.size())
	{
		const size_t end = text.find(';', p);
		const std::string declaration = text.substr(p, end == std::string::npos ? end : end - p);
		const size_t colon = declaration.find(':');
		if (colon != std::string::npos && toLower(strTrim(declaration.substr(0, colon))) == wanted)
			return strTrim(declaration.substr(colon + 1));
		if (end == std::string::npos) break;
		p = end + 1;
	}
	return "";
}

static std::string writeStyleProperty(const Node& node, const std::string& wanted,
	const std::string& value, bool remove)
{
	std::string result;
	const char* existing = node.getAttr("style");
	const std::string text = existing ? existing : "";
	bool found = false;
	size_t p = 0;
	while (p < text.size())
	{
		const size_t end = text.find(';', p);
		const std::string declaration = strTrim(text.substr(p, end == std::string::npos ? end : end - p));
		const size_t colon = declaration.find(':');
		const std::string name = colon == std::string::npos ? "" : toLower(strTrim(declaration.substr(0, colon)));
		if (!name.empty() && name == wanted)
		{
			found = true;
			if (!remove && !value.empty())
			{
				if (!result.empty()) result += "; ";
				result += wanted + ": " + value;
			}
		}
		else if (!declaration.empty())
		{
			if (!result.empty()) result += "; ";
			result += declaration;
		}
		if (end == std::string::npos) break;
		p = end + 1;
	}
	if (!found && !remove && !value.empty())
	{
		if (!result.empty()) result += "; ";
		result += wanted + ": " + value;
	}
	return result;
}

static JSValue stylePropertyGet(JSContext* ctx, JSValueConst thisVal, int magic)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0) return JS_NewString(ctx, "");
	const char* property = stylePropertyName(magic);
	if (magic == 0)
	{
		const char* cssText = doc->nodes[(size_t)idx].getAttr("style");
		return JS_NewString(ctx, cssText ? cssText : "");
	}
	const std::string value = readStyleProperty(doc->nodes[(size_t)idx], property);
	return JS_NewStringLen(ctx, value.data(), value.size());
}

static JSValue stylePropertySet(JSContext* ctx, JSValueConst thisVal, JSValueConst value, int magic)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0) return JS_UNDEFINED;
	if (magic == 0)
	{
		std::string text = jsString(ctx, value);
		if (text.size() > 2048) return JS_ThrowRangeError(ctx, "style.cssText exceeds the page limit");
		doc->setAttr(idx, "style", text);
		return JS_UNDEFINED;
	}
	const std::string name = stylePropertyName(magic);
	const std::string text = jsString(ctx, value);
	if (text.size() > 256 || text.find_first_of("{}") != std::string::npos)
		return JS_ThrowRangeError(ctx, "Invalid inline style value");
	const std::string result = writeStyleProperty(doc->nodes[(size_t)idx], name, text, text.empty());
	doc->setAttr(idx, "style", result);
	return JS_UNDEFINED;
}

static JSValue styleSetProperty(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0 || argc < 2) return JS_UNDEFINED;
	const std::string name = toLower(strTrim(jsString(ctx, argv[0])));
	const std::string value = jsString(ctx, argv[1]);
	if (name.empty() || name.size() > 64 || value.size() > 256 ||
		name.find_first_not_of("abcdefghijklmnopqrstuvwxyz-_") != std::string::npos ||
		value.find_first_of("{}") != std::string::npos)
		return JS_ThrowTypeError(ctx, "Invalid style declaration");
	const std::string result = writeStyleProperty(doc->nodes[(size_t)idx], name, value, value.empty());
	doc->setAttr(idx, "style", result);
	return JS_UNDEFINED;
}

static JSValue styleGetProperty(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0 || !argc) return JS_NewString(ctx, "");
	const std::string name = toLower(strTrim(jsString(ctx, argv[0])));
	const std::string value = readStyleProperty(doc->nodes[(size_t)idx], name);
	return JS_NewStringLen(ctx, value.data(), value.size());
}

static JSValue styleRemoveProperty(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0 || !argc) return JS_NewString(ctx, "");
	const std::string name = toLower(strTrim(jsString(ctx, argv[0])));
	const std::string old = readStyleProperty(doc->nodes[(size_t)idx], name);
	doc->setAttr(idx, "style", writeStyleProperty(doc->nodes[(size_t)idx], name, "", true));
	return JS_NewStringLen(ctx, old.data(), old.size());
}

static const JSCFunctionListEntry styleProperties[] = {
	JS_CGETSET_MAGIC_DEF("cssText", stylePropertyGet, stylePropertySet, 0),
	JS_CGETSET_MAGIC_DEF("color", stylePropertyGet, stylePropertySet, 1),
	JS_CGETSET_MAGIC_DEF("backgroundColor", stylePropertyGet, stylePropertySet, 2),
	JS_CGETSET_MAGIC_DEF("display", stylePropertyGet, stylePropertySet, 3),
	JS_CGETSET_MAGIC_DEF("width", stylePropertyGet, stylePropertySet, 4),
	JS_CGETSET_MAGIC_DEF("height", stylePropertyGet, stylePropertySet, 5),
	JS_CGETSET_MAGIC_DEF("margin", stylePropertyGet, stylePropertySet, 6),
	JS_CGETSET_MAGIC_DEF("padding", stylePropertyGet, stylePropertySet, 7),
	JS_CGETSET_MAGIC_DEF("fontSize", stylePropertyGet, stylePropertySet, 8),
	JS_CGETSET_MAGIC_DEF("fontWeight", stylePropertyGet, stylePropertySet, 9),
	JS_CGETSET_MAGIC_DEF("textAlign", stylePropertyGet, stylePropertySet, 10),
	JS_CGETSET_MAGIC_DEF("border", stylePropertyGet, stylePropertySet, 11),
	JS_CGETSET_MAGIC_DEF("opacity", stylePropertyGet, stylePropertySet, 12),
	JS_CGETSET_MAGIC_DEF("position", stylePropertyGet, stylePropertySet, 13),
	JS_CGETSET_MAGIC_DEF("top", stylePropertyGet, stylePropertySet, 14),
	JS_CGETSET_MAGIC_DEF("left", stylePropertyGet, stylePropertySet, 15),
	JS_CGETSET_MAGIC_DEF("right", stylePropertyGet, stylePropertySet, 16),
	JS_CGETSET_MAGIC_DEF("bottom", stylePropertyGet, stylePropertySet, 17),
	JS_CGETSET_MAGIC_DEF("overflow", stylePropertyGet, stylePropertySet, 18),
	JS_CGETSET_MAGIC_DEF("lineHeight", stylePropertyGet, stylePropertySet, 19),
	JS_CGETSET_MAGIC_DEF("minWidth", stylePropertyGet, stylePropertySet, 20),
	JS_CGETSET_MAGIC_DEF("maxWidth", stylePropertyGet, stylePropertySet, 21),
};

static JSValue wrapStyle(JSContext* ctx, int idx)
{
	JSValue object = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, object, "__pixelNode", JS_NewInt32(ctx, idx));
	JS_SetPropertyFunctionList(ctx, object, styleProperties,
		(int)(sizeof(styleProperties) / sizeof(styleProperties[0])));
	JS_SetPropertyStr(ctx, object, "setProperty", JS_NewCFunction(ctx, styleSetProperty, "setProperty", 2));
	JS_SetPropertyStr(ctx, object, "getPropertyValue", JS_NewCFunction(ctx, styleGetProperty, "getPropertyValue", 1));
	JS_SetPropertyStr(ctx, object, "removeProperty", JS_NewCFunction(ctx, styleRemoveProperty, "removeProperty", 1));
	return object;
}

static std::vector<std::string> classTokens(const char* classes)
{
	std::vector<std::string> tokens;
	if (!classes) return tokens;
	const std::string value(classes);
	size_t p = 0;
	while (p < value.size())
	{
		while (p < value.size() && isspace((unsigned char)value[p])) ++p;
		const size_t end = value.find_first_of(" \t\r\n\f", p);
		if (p < value.size()) tokens.push_back(value.substr(p, end == std::string::npos ? end : end - p));
		if (end == std::string::npos) break;
		p = end + 1;
	}
	return tokens;
}

static std::string joinedClasses(const std::vector<std::string>& tokens)
{
	std::string result;
	for (const std::string& token : tokens)
	{
		if (!result.empty()) result.push_back(' ');
		result += token;
	}
	return result;
}

static bool validClassToken(const std::string& token)
{
	return !token.empty() && token.find_first_of(" \t\r\n\f") == std::string::npos;
}

static JSValue classListContains(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0 || !argc) return JS_NewBool(ctx, 0);
	const std::string token = jsString(ctx, argv[0]);
	return JS_NewBool(ctx, validClassToken(token) && hasClass(doc->nodes[(size_t)idx], token));
}

static JSValue classListAdd(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0) return JS_UNDEFINED;
	std::vector<std::string> tokens = classTokens(doc->nodes[(size_t)idx].getAttr("class"));
	for (int i = 0; i < argc; ++i)
	{
		std::string token = jsString(ctx, argv[i]);
		if (!validClassToken(token)) return JS_ThrowTypeError(ctx, "Invalid class token");
		if (std::find(tokens.begin(), tokens.end(), token) == tokens.end()) tokens.push_back(std::move(token));
	}
	doc->setAttr(idx, "class", joinedClasses(tokens));
	return JS_UNDEFINED;
}

static JSValue classListRemove(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0) return JS_UNDEFINED;
	std::vector<std::string> tokens = classTokens(doc->nodes[(size_t)idx].getAttr("class"));
	for (int i = 0; i < argc; ++i)
	{
		const std::string token = jsString(ctx, argv[i]);
		if (!validClassToken(token)) return JS_ThrowTypeError(ctx, "Invalid class token");
		tokens.erase(std::remove(tokens.begin(), tokens.end(), token), tokens.end());
	}
	doc->setAttr(idx, "class", joinedClasses(tokens));
	return JS_UNDEFINED;
}

static JSValue classListToggle(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0 || !argc) return JS_NewBool(ctx, 0);
	const std::string token = jsString(ctx, argv[0]);
	if (!validClassToken(token)) return JS_ThrowTypeError(ctx, "Invalid class token");
	std::vector<std::string> tokens = classTokens(doc->nodes[(size_t)idx].getAttr("class"));
	auto it = std::find(tokens.begin(), tokens.end(), token);
	const bool present = it != tokens.end();
	const bool force = argc > 1 && !JS_IsUndefined(argv[1]) ? JS_ToBool(ctx, argv[1]) != 0 : !present;
	if (force && !present) tokens.push_back(token);
	else if (!force && present) tokens.erase(std::remove(tokens.begin(), tokens.end(), token), tokens.end());
	doc->setAttr(idx, "class", joinedClasses(tokens));
	return JS_NewBool(ctx, force);
}

static JSValue classListString(JSContext* ctx, JSValueConst thisVal, int, JSValueConst*)
{
	Document* doc = boundDocument(ctx); int idx = nodeIndex(ctx, thisVal);
	if (!doc || idx < 0) return JS_NewString(ctx, "");
	const char* value = doc->nodes[(size_t)idx].getAttr("class");
	return JS_NewString(ctx, value ? value : "");
}

static JSValue wrapClassList(JSContext* ctx, int idx)
{
	JSValue object = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, object, "__pixelNode", JS_NewInt32(ctx, idx));
	JS_SetPropertyStr(ctx, object, "contains", JS_NewCFunction(ctx, classListContains, "contains", 1));
	JS_SetPropertyStr(ctx, object, "add", JS_NewCFunction(ctx, classListAdd, "add", 1));
	JS_SetPropertyStr(ctx, object, "remove", JS_NewCFunction(ctx, classListRemove, "remove", 1));
	JS_SetPropertyStr(ctx, object, "toggle", JS_NewCFunction(ctx, classListToggle, "toggle", 2));
	JS_SetPropertyStr(ctx, object, "toString", JS_NewCFunction(ctx, classListString, "toString", 0));
	return object;
}

static const JSCFunctionListEntry nodeProperties[] = {
	JS_CGETSET_MAGIC_DEF("textContent", domNodeGet, domNodeSet, 0),
	JS_CGETSET_MAGIC_DEF("innerHTML", domNodeGet, domNodeSet, 1),
	JS_CGETSET_MAGIC_DEF("id", domNodeGet, domNodeSet, 2),
	JS_CGETSET_MAGIC_DEF("className", domNodeGet, domNodeSet, 3),
	JS_CGETSET_MAGIC_DEF("tagName", domNodeGet, nullptr, 4),
	JS_CGETSET_MAGIC_DEF("value", domNodeGet, domNodeSet, 5),
	JS_CGETSET_MAGIC_DEF("href", domNodeGet, domNodeSet, 6),
	JS_CGETSET_MAGIC_DEF("name", domNodeGet, domNodeSet, 11),
	JS_CGETSET_MAGIC_DEF("checked", domNodeGet, domNodeSet, 12),
	JS_CGETSET_MAGIC_DEF("disabled", domNodeGet, domNodeSet, 13),
	JS_CGETSET_MAGIC_DEF("classList", domNodeGet, nullptr, 14),
	JS_CGETSET_MAGIC_DEF("nextSibling", domNodeGet, nullptr, 15),
	JS_CGETSET_MAGIC_DEF("previousSibling", domNodeGet, nullptr, 16),
	JS_CGETSET_MAGIC_DEF("parentElement", domNodeGet, nullptr, 17),
	JS_CGETSET_MAGIC_DEF("childElementCount", domNodeGet, nullptr, 18),
	JS_CGETSET_MAGIC_DEF("innerText", domNodeGet, domNodeSet, 0),
	JS_CGETSET_MAGIC_DEF("style", domNodeGet, nullptr, 19),
	JS_CGETSET_MAGIC_DEF("form", domNodeGet, nullptr, 20),
	JS_CGETSET_MAGIC_DEF("type", domNodeGet, domNodeSet, 21),
	JS_CGETSET_MAGIC_DEF("selected", domNodeGet, domNodeSet, 22),
	JS_CGETSET_MAGIC_DEF("required", domNodeGet, domNodeSet, 23),
	JS_CGETSET_MAGIC_DEF("readOnly", domNodeGet, domNodeSet, 24),
	JS_CGETSET_MAGIC_DEF("parentNode", domNodeGet, nullptr, 7),
	JS_CGETSET_MAGIC_DEF("firstChild", domNodeGet, nullptr, 8),
	JS_CGETSET_MAGIC_DEF("childNodes", domNodeGet, nullptr, 9),
	JS_CGETSET_MAGIC_DEF("children", domNodeGet, nullptr, 10),
	JS_CFUNC_DEF("appendChild", 1, domAppendChild),
	JS_CFUNC_DEF("insertBefore", 2, domInsertBefore),
	JS_CFUNC_DEF("removeChild", 1, domRemoveChild),
	JS_CFUNC_DEF("remove", 0, domRemove),
	JS_CFUNC_DEF("addEventListener", 2, eventTargetAdd),
	JS_CFUNC_DEF("removeEventListener", 2, eventTargetRemove),
	JS_CFUNC_DEF("setAttribute", 2, domSetAttribute),
	JS_CFUNC_DEF("getAttribute", 1, domGetAttribute),
	JS_CFUNC_DEF("removeAttribute", 1, domRemoveAttribute),
	JS_CFUNC_DEF("hasAttribute", 1, domHasAttribute),
};

static JSValue wrapNode(JSContext* ctx, int index)
{
	if (index < 0) return JS_NULL;
	JSValue object = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, object, "__pixelNode", JS_NewInt32(ctx, index));
	JS_SetPropertyFunctionList(ctx, object, nodeProperties,
		(int)(sizeof(nodeProperties) / sizeof(nodeProperties[0])));
	return object;
}

static JSValue documentGetElementById(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	if (!doc || !argc) return JS_NULL;
	const std::string id = jsString(ctx, argv[0]);
	for (size_t i = 0; i < doc->nodes.size(); ++i)
	{
		if (!nodeConnected(*doc, (int)i)) continue;
		const char* nodeId = doc->nodes[i].getAttr("id");
		if (nodeId && id == nodeId) return wrapNode(ctx, (int)i);
	}
	return JS_NULL;
}

static JSValue documentQuerySelector(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	if (!doc || !argc) return JS_NULL;
	return wrapNode(ctx, findSelector(*doc, jsString(ctx, argv[0]), true));
}

static JSValue documentQuerySelectorAll(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	JSValue array = JS_NewArray(ctx);
	if (!doc || !argc) return array;
	const std::string selector = jsString(ctx, argv[0]);
	uint32_t n = 0;
	for (size_t i = 0; i < doc->nodes.size(); ++i)
		if (nodeConnected(*doc, (int)i) && selectorMatches(*doc, (int)i, selector))
			JS_SetPropertyUint32(ctx, array, n++, wrapNode(ctx, (int)i));
	return array;
}

static JSValue documentGetElementsByTagName(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	JSValue array = JS_NewArray(ctx);
	if (!doc || !argc) return array;
	const std::string tag = toLower(jsString(ctx, argv[0]));
	uint32_t count = 0;
	for (size_t i = 0; i < doc->nodes.size(); ++i)
		if (nodeConnected(*doc, (int)i) && doc->nodes[i].isElement() && (tag == "*" || doc->nodes[i].tag == tag))
			JS_SetPropertyUint32(ctx, array, count++, wrapNode(ctx, (int)i));
	return array;
}

static JSValue documentGetElementsByClassName(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	JSValue array = JS_NewArray(ctx);
	if (!doc || !argc) return array;
	const std::string wanted = jsString(ctx, argv[0]);
	uint32_t count = 0;
	for (size_t i = 0; i < doc->nodes.size(); ++i)
		if (nodeConnected(*doc, (int)i) && hasClass(doc->nodes[i], wanted))
			JS_SetPropertyUint32(ctx, array, count++, wrapNode(ctx, (int)i));
	return array;
}

static JSValue documentCreateElement(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	if (!doc || !argc || doc->nodes.size() >= NW_MAX_DOM_NODES) return JS_NULL;
	int idx = doc->createElement(-1, toLower(jsString(ctx, argv[0])));
	return wrapNode(ctx, idx);
}

static JSValue documentCreateTextNode(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	Document* doc = boundDocument(ctx);
	if (!doc || !argc || doc->nodes.size() >= NW_MAX_DOM_NODES) return JS_NULL;
	int idx = doc->createText(-1, jsString(ctx, argv[0]));
	return wrapNode(ctx, idx);
}

static JSValue domNoop(JSContext*, JSValueConst, int, JSValueConst*)
{
	return JS_UNDEFINED;
}

static uint32_t nextTimerId = 1;

static JSValue scheduleCallback(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic)
{
	if (!argc || !JS_IsFunction(ctx, argv[0])) return JS_NewInt32(ctx, 0);
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue timers = JS_GetPropertyStr(ctx, global, "__pixelTimers");
	if (!JS_IsArray(ctx, timers)) { JS_FreeValue(ctx, timers); timers = JS_NewArray(ctx); }
	const uint32_t count = jsArrayLength(ctx, timers);
	if (count >= 32)
	{
		JS_FreeValue(ctx, timers); JS_FreeValue(ctx, global);
		return JS_NewInt32(ctx, 0);
	}
	int32_t delay = magic == 2 ? 16 : 0;
	if (magic != 2 && argc > 1) JS_ToInt32(ctx, &delay, argv[1]);
	if (delay < 1) delay = 1;
	if (delay > 60000) delay = 60000;
	const uint32_t id = nextTimerId++;
	JSValue item = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, item, "id", JS_NewUint32(ctx, id));
	JS_SetPropertyStr(ctx, item, "due", JS_NewFloat64(ctx, (double)(osGetTime() + (uint32_t)delay)));
	JS_SetPropertyStr(ctx, item, "interval", JS_NewInt32(ctx, magic == 1 ? delay : 0));
	JS_SetPropertyStr(ctx, item, "callback", JS_DupValue(ctx, argv[0]));
	JS_SetPropertyUint32(ctx, timers, count, item);
	JS_SetPropertyStr(ctx, global, "__pixelTimers", timers);
	JS_FreeValue(ctx, global);
	return JS_NewUint32(ctx, id);
}

static JSValue clearScheduledCallback(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
	if (!argc) return JS_UNDEFINED;
	uint32_t wanted = 0; JS_ToUint32(ctx, &wanted, argv[0]);
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue timers = JS_GetPropertyStr(ctx, global, "__pixelTimers");
	uint32_t count = JS_IsArray(ctx, timers) ? jsArrayLength(ctx, timers) : 0;
	for (uint32_t i = 0; i < count; ++i)
	{
		JSValue item = JS_GetPropertyUint32(ctx, timers, i);
		JSValue idValue = JS_GetPropertyStr(ctx, item, "id"); uint32_t id = 0;
		JS_ToUint32(ctx, &id, idValue);
		JS_FreeValue(ctx, idValue); JS_FreeValue(ctx, item);
		if (id != wanted) continue;
		for (uint32_t j = i + 1; j < count; ++j)
			JS_SetPropertyUint32(ctx, timers, j - 1, JS_GetPropertyUint32(ctx, timers, j));
		JS_SetPropertyStr(ctx, timers, "length", JS_NewUint32(ctx, count - 1));
		break;
	}
	JS_FreeValue(ctx, timers); JS_FreeValue(ctx, global);
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry documentFunctions[] = {
	JS_CFUNC_DEF("getElementById", 1, documentGetElementById),
	JS_CFUNC_DEF("querySelector", 1, documentQuerySelector),
	JS_CFUNC_DEF("querySelectorAll", 1, documentQuerySelectorAll),
	JS_CFUNC_DEF("getElementsByTagName", 1, documentGetElementsByTagName),
	JS_CFUNC_DEF("getElementsByClassName", 1, documentGetElementsByClassName),
	JS_CFUNC_DEF("createElement", 1, documentCreateElement),
	JS_CFUNC_DEF("createTextNode", 1, documentCreateTextNode),
	JS_CFUNC_DEF("addEventListener", 2, eventTargetAdd),
	JS_CFUNC_DEF("removeEventListener", 2, eventTargetRemove),
};

static const JSCFunctionListEntry consoleFunctions[] = {
	JS_CFUNC_DEF("log", 1, domNoop),
	JS_CFUNC_DEF("info", 1, domNoop),
	JS_CFUNC_DEF("warn", 1, domNoop),
	JS_CFUNC_DEF("error", 1, domNoop),
};
}

bool jsPageContextCreate(JSRuntime*& runtime, JSContext*& context)
{
	runtime = nullptr;
	context = nullptr;
	runtime = JS_NewRuntime2(&kQuickJsAllocator, nullptr);
	if (!runtime) return false;
	ExecutionBudget* budget = new (std::nothrow) ExecutionBudget();
	if (!budget)
	{
		JS_FreeRuntime(runtime);
		runtime = nullptr;
		return false;
	}
	JS_SetRuntimeOpaque(runtime, budget);
	JS_SetInterruptHandler(runtime, interruptJavaScript, budget);
	JS_SetMemoryLimit(runtime, kJsHeapLimit);
	JS_SetMaxStackSize(runtime, kJsStackLimit);
	context = JS_NewContext(runtime);
	if (context) return true;
	JS_FreeRuntime(runtime);
	runtime = nullptr;
	return false;
}

void jsPageBindDocument(JSContext* context, Document* document, const char* pageUrl,
	JSPageBinding* binding)
{
	if (!context || !document) return;
	JSValue global = JS_GetGlobalObject(context);
	if (binding)
	{
		binding->document = document;
		std::string url = pageUrl ? pageUrl : "";
		binding->url = url;
		const size_t scheme = url.find("://");
		if (scheme == std::string::npos) binding->origin.clear();
		else
		{
			const size_t end = url.find_first_of("/?#", scheme + 3);
			std::string authority = url.substr(0, end == std::string::npos ? end : end);
			for (char& ch : authority) ch = (char)std::tolower((unsigned char)ch);
			binding->origin = authority;
		}
	}
	JS_SetContextOpaque(context, binding);
	JSValue jsDocument = JS_NewObject(context);
	JS_SetPropertyFunctionList(context, jsDocument, documentFunctions,
		(int)(sizeof(documentFunctions) / sizeof(documentFunctions[0])));
	int body = document->firstByTag(document->root, "body");
	int html = document->firstByTag(document->root, "html");
	JS_SetPropertyStr(context, jsDocument, "body", wrapNode(context, body));
	JS_SetPropertyStr(context, jsDocument, "documentElement", wrapNode(context, html));
	const std::string title = document->title();
	JS_SetPropertyStr(context, jsDocument, "title", JS_NewStringLen(context, title.data(), title.size()));
	JS_SetPropertyStr(context, jsDocument, "readyState", JS_NewString(context, "complete"));
	JS_SetPropertyStr(context, jsDocument, "URL", JS_NewString(context, pageUrl ? pageUrl : ""));
	JSCFunctionType cookieGetter; cookieGetter.getter = documentCookieGet;
	JSCFunctionType cookieSetter; cookieSetter.setter = documentCookieSet;
	JSAtom cookieAtom = JS_NewAtom(context, "cookie");
	JS_DefinePropertyGetSet(context, jsDocument, cookieAtom,
		JS_NewCFunction2(context, cookieGetter.generic, "get cookie", 0, JS_CFUNC_getter, 0),
		JS_NewCFunction2(context, cookieSetter.generic, "set cookie", 1, JS_CFUNC_setter, 0), JS_PROP_CONFIGURABLE);
	JS_FreeAtom(context, cookieAtom);
	JS_SetPropertyStr(context, jsDocument, "__pixelTarget", JS_NewString(context, "document"));
	JS_SetPropertyStr(context, global, "document", jsDocument);
	JSValue console = JS_NewObject(context);
	JS_SetPropertyFunctionList(context, console, consoleFunctions,
		(int)(sizeof(consoleFunctions) / sizeof(consoleFunctions[0])));
	JS_SetPropertyStr(context, global, "console", console);
	JS_SetPropertyStr(context, global, "window", JS_DupValue(context, global));
	JS_SetPropertyStr(context, global, "self", JS_DupValue(context, global));
	JS_SetPropertyStr(context, global, "__pixelTarget", JS_NewString(context, "window"));
	JS_SetPropertyStr(context, global, "__pixelTimers", JS_NewArray(context));
	JS_SetPropertyStr(context, global, "addEventListener",
		JS_NewCFunction(context, eventTargetAdd, "addEventListener", 2));
	JS_SetPropertyStr(context, global, "removeEventListener",
		JS_NewCFunction(context, eventTargetRemove, "removeEventListener", 2));
	JSValue navigator = JS_NewObject(context);
	JS_SetPropertyStr(context, navigator, "userAgent",
		JS_NewString(context, "Pixelnaute/" NW_VERSION_STRING " (Nintendo 3DS)"));
	JS_SetPropertyStr(context, navigator, "platform", JS_NewString(context, "Nintendo 3DS"));
	JS_SetPropertyStr(context, navigator, "language", JS_NewString(context, "fr-FR"));
	JS_SetPropertyStr(context, navigator, "onLine", JS_NewBool(context, 1));
	JS_SetPropertyStr(context, global, "navigator", navigator);
	if (binding)
	{
		JS_SetPropertyStr(context, global, "localStorage", createStorage(context, false));
		JS_SetPropertyStr(context, global, "sessionStorage", createStorage(context, true));
	}
	JSValue location = JS_NewObject(context);
	JSCFunctionType hrefGetter; hrefGetter.getter = locationHrefGet;
	JSCFunctionType hrefSetter; hrefSetter.setter = locationHrefSet;
	JSAtom hrefAtom = JS_NewAtom(context, "href");
	JS_DefinePropertyGetSet(context, location, hrefAtom,
		JS_NewCFunction2(context, hrefGetter.generic, "get href", 0, JS_CFUNC_getter, 0),
		JS_NewCFunction2(context, hrefSetter.generic, "set href", 1, JS_CFUNC_setter, 0), JS_PROP_CONFIGURABLE);
	JS_FreeAtom(context, hrefAtom);
	const char* locationProperties[] = { "protocol", "origin", "host", "hostname", "port", "pathname", "search", "hash" };
	for (int i = 0; i < 8; ++i)
	{
		JSAtom atom = JS_NewAtom(context, locationProperties[i]);
		JSCFunctionType componentGetter; componentGetter.getter_magic = locationComponentGet;
		JS_DefinePropertyGetSet(context, location, atom,
			JS_NewCFunction2(context, componentGetter.generic, locationProperties[i], 0, JS_CFUNC_getter_magic, i + 1),
			JS_UNDEFINED, JS_PROP_CONFIGURABLE);
		JS_FreeAtom(context, atom);
	}
	JS_SetPropertyStr(context, location, "assign", JS_NewCFunction(context, locationAssign, "assign", 1));
	JS_SetPropertyStr(context, location, "replace", JS_NewCFunction(context, locationReplace, "replace", 1));
	JS_SetPropertyStr(context, location, "reload", JS_NewCFunction(context, locationReload, "reload", 0));
	JS_SetPropertyStr(context, global, "location", location);
	JS_SetPropertyStr(context, global, "getComputedStyle", JS_NewCFunction(context, getComputedStyle, "getComputedStyle", 1));
	JSValue history = JS_NewObject(context);
	JS_SetPropertyStr(context, history, "go", JS_NewCFunction(context, historyGo, "go", 1));
	JS_SetPropertyStr(context, history, "back", JS_NewCFunction(context, historyBack, "back", 0));
	JS_SetPropertyStr(context, history, "forward", JS_NewCFunction(context, historyForward, "forward", 0));
	JS_SetPropertyStr(context, history, "state", JS_NULL);
	JS_SetPropertyStr(context, global, "history", history);
	JSValue crypto = JS_NewObject(context);
	JS_SetPropertyStr(context, crypto, "getRandomValues",
		JS_NewCFunction(context, cryptoGetRandomValues, "getRandomValues", 1));
	JS_SetPropertyStr(context, global, "crypto", crypto);
	JS_SetPropertyStr(context, global, "setTimeout",
		JS_NewCFunctionMagic(context, scheduleCallback, "setTimeout", 2, JS_CFUNC_generic_magic, 0));
	JS_SetPropertyStr(context, global, "setInterval",
		JS_NewCFunctionMagic(context, scheduleCallback, "setInterval", 2, JS_CFUNC_generic_magic, 1));
	JS_SetPropertyStr(context, global, "requestAnimationFrame",
		JS_NewCFunctionMagic(context, scheduleCallback, "requestAnimationFrame", 1, JS_CFUNC_generic_magic, 2));
	JS_SetPropertyStr(context, global, "clearTimeout",
		JS_NewCFunction(context, clearScheduledCallback, "clearTimeout", 1));
	JS_SetPropertyStr(context, global, "clearInterval",
		JS_NewCFunction(context, clearScheduledCallback, "clearInterval", 1));
	JS_SetPropertyStr(context, global, "cancelAnimationFrame",
		JS_NewCFunction(context, clearScheduledCallback, "cancelAnimationFrame", 1));
	JSValue fetchPromises = JS_GetPropertyStr(context, global, "__pixelFetchPromises");
	if (JS_IsUndefined(fetchPromises)) JS_SetPropertyStr(context, global, "__pixelFetchPromises", JS_NewArray(context));
	JS_FreeValue(context, fetchPromises);
	JSValue xhrs = JS_GetPropertyStr(context, global, "__pixelXHRs");
	if (JS_IsUndefined(xhrs)) JS_SetPropertyStr(context, global, "__pixelXHRs", JS_NewArray(context));
	JS_FreeValue(context, xhrs);
	JS_SetPropertyStr(context, global, "fetch", JS_NewCFunction(context, pageFetch, "fetch", 2));
	JSCFunctionType xhrCtor; xhrCtor.constructor = xhrConstructor;
	JS_SetPropertyStr(context, global, "XMLHttpRequest",
		JS_NewCFunction2(context, xhrCtor.generic, "XMLHttpRequest", 0, JS_CFUNC_constructor, 0));
	JSCFunctionType urlCtor; urlCtor.constructor = urlConstructor;
	JS_SetPropertyStr(context, global, "URL",
		JS_NewCFunction2(context, urlCtor.generic, "URL", 1, JS_CFUNC_constructor, 0));
	JSCFunctionType paramsCtor; paramsCtor.constructor = urlSearchParamsConstructor;
	JS_SetPropertyStr(context, global, "URLSearchParams",
		JS_NewCFunction2(context, paramsCtor.generic, "URLSearchParams", 1, JS_CFUNC_constructor, 0));
	static const char compatibilityBootstrap[] =
		"if(typeof matchMedia==='undefined')window.matchMedia=function(q){return {matches:false,media:q,addListener:function(){},removeListener:function(){},addEventListener:function(){},removeEventListener:function(){},dispatchEvent:function(){return false}}};"
		"function __PixelObserver(cb){this.cb=cb;this.targets=[]}__PixelObserver.prototype.observe=function(t){if(this.targets.indexOf(t)<0)this.targets.push(t)};__PixelObserver.prototype.unobserve=function(t){var i=this.targets.indexOf(t);if(i>=0)this.targets.splice(i,1)};__PixelObserver.prototype.disconnect=function(){this.targets.length=0};"
		"if(typeof IntersectionObserver==='undefined')window.IntersectionObserver=__PixelObserver;"
		"if(typeof MutationObserver==='undefined')window.MutationObserver=__PixelObserver;"
		"if(typeof ResizeObserver==='undefined')window.ResizeObserver=__PixelObserver;"
		"if(typeof queueMicrotask==='undefined')window.queueMicrotask=function(cb){setTimeout(cb,0)};"
		"if(typeof structuredClone==='undefined')window.structuredClone=function(v){return JSON.parse(JSON.stringify(v))};"
		"if(typeof TextEncoder==='undefined'){window.TextEncoder=function(){};TextEncoder.prototype.encode=function(s){var b=unescape(encodeURIComponent(String(s))),a=new Uint8Array(b.length);for(var i=0;i<b.length;i++)a[i]=b.charCodeAt(i);return a};}"
		"if(typeof TextDecoder==='undefined'){window.TextDecoder=function(){this.encoding='utf-8'};TextDecoder.prototype.decode=function(a){var b='';if(a)for(var i=0;i<a.length;i++)b+=String.fromCharCode(a[i]);try{return decodeURIComponent(escape(b))}catch(e){return b}};}"
		"if(typeof btoa==='undefined')window.btoa=function(s){var c='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=',o='',i=0;s=String(s);for(;i<s.length;){var a=s.charCodeAt(i++),b=s.charCodeAt(i++),d=s.charCodeAt(i++);if(a>255||b>255||d>255)throw new TypeError('Invalid character');var x=a>>2,y=((a&3)<<4)|(b>>4),z=((b&15)<<2)|(d>>6),w=d&63;if(isNaN(b))z=w=64;else if(isNaN(d))w=64;o+=c.charAt(x)+c.charAt(y)+c.charAt(z)+c.charAt(w)}return o};"
		"if(typeof atob==='undefined')window.atob=function(s){var c='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=',o='',i=0;s=String(s).replace(/[\\t\\n\\f\\r ]/g,'');if(s.length%4!==0)throw new TypeError('Invalid character');for(;i<s.length;){var x=c.indexOf(s.charAt(i++)),y=c.indexOf(s.charAt(i++)),z=c.indexOf(s.charAt(i++)),w=c.indexOf(s.charAt(i++));if(x<0||x>=64||y<0||y>=64||z<0||w<0)throw new TypeError('Invalid character');o+=String.fromCharCode((x<<2)|(y>>4));if(z<64)o+=String.fromCharCode(((y&15)<<4)|(z>>2));if(w<64)o+=String.fromCharCode(((z&3)<<6)|w)}return o};"
		"if(typeof Intl==='undefined')window.Intl={};if(!Intl.NumberFormat)Intl.NumberFormat=function(){return {format:function(v){return String(v)},resolvedOptions:function(){return {locale:'fr-FR'}}}};if(!Intl.DateTimeFormat)Intl.DateTimeFormat=function(){return {format:function(v){return String(v)},resolvedOptions:function(){return {locale:'fr-FR',timeZone:'UTC'}}}};";
	JSValue compat = JS_Eval(context, compatibilityBootstrap,
		sizeof(compatibilityBootstrap) - 1, "<compatibility>", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(compat))
	{
		JSValue exception = JS_GetException(context);
		JS_FreeValue(context, exception);
	}
	JS_FreeValue(context, compat);
	JS_FreeValue(context, global);
}

void jsPageTick(JSRuntime* runtime, JSContext* context)
{
	if (!runtime || !context || !jsPageExecutionBegin(runtime, false)) return;
	JSValue global = JS_GetGlobalObject(context);
	JSValue timers = JS_GetPropertyStr(context, global, "__pixelTimers");
	if (!JS_IsArray(context, timers))
	{
		JS_FreeValue(context, timers); JS_FreeValue(context, global);
		jsPageExecutionEnd(runtime); return;
	}
	const uint64_t now = osGetTime();
	int callbacks = 0;
	for (uint32_t i = 0; i < jsArrayLength(context, timers) && callbacks < 2; )
	{
		JSValue item = JS_GetPropertyUint32(context, timers, i);
		JSValue dueValue = JS_GetPropertyStr(context, item, "due"); double due = 0;
		JS_ToFloat64(context, &due, dueValue); JS_FreeValue(context, dueValue);
		if (due > (double)now) { JS_FreeValue(context, item); ++i; continue; }
		JSValue intervalValue = JS_GetPropertyStr(context, item, "interval"); int32_t interval = 0;
		JS_ToInt32(context, &interval, intervalValue); JS_FreeValue(context, intervalValue);
		JSValue callback = JS_GetPropertyStr(context, item, "callback");
		if (interval > 0)
		{
			JS_SetPropertyStr(context, item, "due", JS_NewFloat64(context, (double)(now + (uint32_t)interval)));
			JS_SetPropertyUint32(context, timers, i, JS_DupValue(context, item)); ++i;
		}
		else
		{
			const uint32_t count = jsArrayLength(context, timers);
			for (uint32_t j = i + 1; j < count; ++j)
				JS_SetPropertyUint32(context, timers, j - 1, JS_GetPropertyUint32(context, timers, j));
			JS_SetPropertyStr(context, timers, "length", JS_NewUint32(context, count - 1));
		}
		JSValue result = JS_Call(context, callback, JS_UNDEFINED, 0, nullptr);
		if (JS_IsException(result)) { JSValue ex = JS_GetException(context); JS_FreeValue(context, ex); }
		else executePendingJobs(runtime, context);
		JS_FreeValue(context, result); JS_FreeValue(context, callback); JS_FreeValue(context, item);
		++callbacks;
	}
	JS_FreeValue(context, timers); JS_FreeValue(context, global);
	jsPageExecutionEnd(runtime);
}

void jsPageResolveFetch(JSRuntime* runtime, JSContext* context, uint32_t id,
	const char* url, const char* body, size_t bodyLength, const char* contentType,
	int status, const char* error)
{
	if (!runtime || !context || !jsPageExecutionBegin(runtime, false)) return;
	JSValue global = JS_GetGlobalObject(context);
	JSValue entries = JS_GetPropertyStr(context, global, "__pixelFetchPromises");
	JSValue record = JS_GetPropertyUint32(context, entries, id);
	JSValue settle = JS_GetPropertyStr(context, record, error && *error ? "reject" : "resolve");
	JSValue argument = JS_UNDEFINED;
	if (error && *error)
	{
		argument = JS_NewError(context);
		JS_SetPropertyStr(context, argument, "message", JS_NewString(context, error));
	}
	else
	{
		argument = JS_NewObject(context);
		JS_SetPropertyStr(context, argument, "status", JS_NewInt32(context, status));
		JS_SetPropertyStr(context, argument, "ok", JS_NewBool(context, status >= 200 && status < 300));
		JS_SetPropertyStr(context, argument, "url", JS_NewString(context, url ? url : ""));
		JS_SetPropertyStr(context, argument, "__pixelBody", JS_NewStringLen(context, body ? body : "", body ? bodyLength : 0));
		JS_SetPropertyStr(context, argument, "contentType", JS_NewString(context, contentType ? contentType : ""));
		JS_SetPropertyStr(context, argument, "text", JS_NewCFunction(context, responseText, "text", 0));
		JS_SetPropertyStr(context, argument, "json", JS_NewCFunction(context, responseJson, "json", 0));
		JSValue headers = JS_NewObject(context);
		JS_SetPropertyStr(context, headers, "contentType", JS_NewString(context, contentType ? contentType : ""));
		JS_SetPropertyStr(context, headers, "get", JS_NewCFunction(context, responseHeaderGet, "get", 1));
		JS_SetPropertyStr(context, argument, "headers", headers);
	}
	if (!JS_IsUndefined(settle))
	{
		JSValue result = JS_Call(context, settle, JS_UNDEFINED, 1, &argument);
		JS_FreeValue(context, result);
	}
	JS_SetPropertyUint32(context, entries, id, JS_UNDEFINED);
	executePendingJobs(runtime, context);
	JS_FreeValue(context, argument); JS_FreeValue(context, settle); JS_FreeValue(context, record);
	JS_FreeValue(context, entries); JS_FreeValue(context, global);
	jsPageExecutionEnd(runtime);
}

void jsPageResolveXHR(JSRuntime* runtime, JSContext* context, uint32_t id,
	const char* url, const char* body, size_t bodyLength, const char* contentType,
	int status, const char* error)
{
	if (!runtime || !context || !jsPageExecutionBegin(runtime, false)) return;
	JSValue global = JS_GetGlobalObject(context), entries = JS_GetPropertyStr(context, global, "__pixelXHRs");
	JSValue xhr = JS_GetPropertyUint32(context, entries, id);
	JSValue abortedValue = JS_GetPropertyStr(context, xhr, "__pixelAborted");
	const bool aborted = JS_ToBool(context, abortedValue) != 0; JS_FreeValue(context, abortedValue);
	if (!aborted && !JS_IsUndefined(xhr) && !JS_IsNull(xhr))
	{
		JS_SetPropertyStr(context, xhr, "status", JS_NewInt32(context, error && *error ? 0 : status));
		JS_SetPropertyStr(context, xhr, "responseText", JS_NewStringLen(context, body ? body : "", body ? bodyLength : 0));
		JS_SetPropertyStr(context, xhr, "responseURL", JS_NewString(context, url ? url : ""));
		JS_SetPropertyStr(context, xhr, "__pixelContentType", JS_NewString(context, contentType ? contentType : ""));
		if (error && *error)
		{
			JS_SetPropertyStr(context, xhr, "readyState", JS_NewInt32(context, 4));
			callXhrHandler(context, xhr, "onreadystatechange"); callXhrHandler(context, xhr, "onerror");
		}
		else
		{
			for (int ready = 2; ready <= 4; ++ready)
			{
				JS_SetPropertyStr(context, xhr, "readyState", JS_NewInt32(context, ready));
				callXhrHandler(context, xhr, "onreadystatechange");
				JSValue stopped = JS_GetPropertyStr(context, xhr, "__pixelAborted");
				const bool nowAborted = JS_ToBool(context, stopped) != 0; JS_FreeValue(context, stopped);
				if (nowAborted) break;
			}
			JSValue stopped = JS_GetPropertyStr(context, xhr, "__pixelAborted");
			const bool nowAborted = JS_ToBool(context, stopped) != 0; JS_FreeValue(context, stopped);
			if (!nowAborted) callXhrHandler(context, xhr, "onload");
		}
	}
	JS_SetPropertyUint32(context, entries, id, JS_UNDEFINED);
	JS_FreeValue(context, xhr); JS_FreeValue(context, entries); JS_FreeValue(context, global);
	jsPageExecutionEnd(runtime);
}

static bool dispatchListenersAt(JSContext* context, JSRuntime* runtime, JSValueConst list,
	const std::string& type, const std::string& key, JSValueConst currentTarget,
	JSValue event, bool capture, bool targetPhase)
{
	const uint32_t length = jsArrayLength(context, list);
	for (uint32_t i = 0; i < length; ++i)
	{
		JSValue item = JS_GetPropertyUint32(context, list, i);
		JSValue itemType = JS_GetPropertyStr(context, item, "type");
		JSValue itemTarget = JS_GetPropertyStr(context, item, "target");
		JSValue itemCapture = JS_GetPropertyStr(context, item, "capture");
		JSValue removed = JS_GetPropertyStr(context, item, "removed");
		const bool matches = jsString(context, itemType) == type &&
			jsString(context, itemTarget) == key &&
			(JS_ToBool(context, itemCapture) != 0) == capture &&
			!JS_ToBool(context, removed);
		JS_FreeValue(context, itemType);
		JS_FreeValue(context, itemTarget);
		JS_FreeValue(context, itemCapture);
		JS_FreeValue(context, removed);
		if (matches)
		{
			JSValue callback = JS_GetPropertyStr(context, item, "callback");
			JSValue once = JS_GetPropertyStr(context, item, "once");
			if (JS_ToBool(context, once))
				JS_SetPropertyStr(context, item, "removed", JS_NewBool(context, 1));
			JS_FreeValue(context, once);
			JS_SetPropertyStr(context, event, "currentTarget", JS_DupValue(context, currentTarget));
			JSValue result = JS_Call(context, callback, currentTarget, 1, &event);
			if (JS_IsException(result))
			{
				JSValue exception = JS_GetException(context);
				JS_FreeValue(context, exception);
			}
			else executePendingJobs(runtime, context);
			JS_FreeValue(context, result);
			JS_FreeValue(context, callback);
			JSValue immediate = JS_GetPropertyStr(context, event, "__pixelImmediate");
			const bool stopImmediate = JS_ToBool(context, immediate) != 0;
			JS_FreeValue(context, immediate);
			if (stopImmediate) { JS_FreeValue(context, item); return false; }
		}
		JS_FreeValue(context, item);
	}
	(void)targetPhase;
	return true;
}

bool jsPageDispatchEvent(JSContext* context, const char* type, const char* target,
	const char* keyValue, int x, int y)
{
	if (!context || !type || !target) return true;
	JSRuntime* runtime = JS_GetRuntime(context);
	if (!jsPageExecutionBegin(runtime, false)) return true;
	JSValue global = JS_GetGlobalObject(context);
	const std::string eventType = toLower(type);
	const std::string key = toLower(target);
	const bool once = eventType == "domcontentloaded" || eventType == "load";
	const std::string sentKey = std::string("__pixelSent_") + key + "_" + eventType;
	JSValue sent = JS_GetPropertyStr(context, global, sentKey.c_str());
	const bool alreadySent = once && JS_ToBool(context, sent) != 0;
	JS_FreeValue(context, sent);
	if (alreadySent) { JS_FreeValue(context, global); jsPageExecutionEnd(runtime); return true; }
	if (once) JS_SetPropertyStr(context, global, sentKey.c_str(), JS_NewBool(context, 1));
	JSValue list = JS_GetPropertyStr(context, global, "__pixelListeners");
	JSValue targetValue = JS_UNDEFINED;
	std::vector<int> path;
	Document* doc = boundDocument(context);
	if (key.rfind("node:", 0) == 0)
	{
		const int idx = atoi(key.c_str() + 5);
		if (doc && idx >= 0 && (size_t)idx < doc->nodes.size())
		{
			targetValue = wrapNode(context, idx);
			for (int node = idx; node >= 0 && (size_t)node < doc->nodes.size(); node = doc->nodes[(size_t)node].parent)
				path.push_back(node);
		}
		else targetValue = JS_NULL;
	}
	else targetValue = JS_GetPropertyStr(context, global, target);
	if (!JS_IsArray(context, list) || JS_IsNull(targetValue))
	{
		JS_FreeValue(context, targetValue); JS_FreeValue(context, list); JS_FreeValue(context, global);
		jsPageExecutionEnd(runtime); return true;
	}
	const bool bubbles = eventType != "focus" && eventType != "blur" &&
		eventType != "load" && eventType != "domcontentloaded" && eventType != "invalid";
	const bool cancelable = eventType == "click" || eventType == "submit" ||
		eventType == "input" || eventType == "change" || eventType == "keydown" ||
		eventType == "keyup" || eventType == "pointerdown" || eventType == "pointerup";
	JSValue event = JS_NewObject(context);
	JS_SetPropertyStr(context, event, "type", JS_NewString(context, type));
	JS_SetPropertyStr(context, event, "target", JS_DupValue(context, targetValue));
	if (keyValue)
	{
		const int keyCode = (unsigned char)keyValue[0];
		JS_SetPropertyStr(context, event, "key", JS_NewString(context, keyValue));
		JS_SetPropertyStr(context, event, "code", JS_NewString(context, keyValue));
		JS_SetPropertyStr(context, event, "keyCode", JS_NewInt32(context, keyCode));
		JS_SetPropertyStr(context, event, "which", JS_NewInt32(context, keyCode));
	}
	if (x >= 0 && y >= 0)
	{
		JS_SetPropertyStr(context, event, "clientX", JS_NewInt32(context, x));
		JS_SetPropertyStr(context, event, "clientY", JS_NewInt32(context, y));
		JS_SetPropertyStr(context, event, "pageX", JS_NewInt32(context, x));
		JS_SetPropertyStr(context, event, "pageY", JS_NewInt32(context, y));
	}
	JS_SetPropertyStr(context, event, "bubbles", JS_NewBool(context, bubbles));
	JS_SetPropertyStr(context, event, "cancelable", JS_NewBool(context, cancelable));
	JS_SetPropertyStr(context, event, "defaultPrevented", JS_NewBool(context, 0));
	JS_SetPropertyStr(context, event, "__pixelCancelable", JS_NewBool(context, cancelable));
	JS_SetPropertyStr(context, event, "preventDefault", JS_NewCFunction(context, eventPreventDefault, "preventDefault", 0));
	JS_SetPropertyStr(context, event, "stopPropagation", JS_NewCFunction(context, eventStopPropagation, "stopPropagation", 0));
	if (key.rfind("node:", 0) == 0 && !path.empty())
	{
		// Capture: window -> document -> root -> parent.
		for (int outer = 0; outer < 2; ++outer)
		{
			const char* outerKey = outer == 0 ? "window" : "document";
			JSValue current = JS_GetPropertyStr(context, global, outerKey);
			JS_SetPropertyStr(context, event, "eventPhase", JS_NewInt32(context, 1));
			dispatchListenersAt(context, runtime, list, eventType, outerKey, current, event, true, false);
			JS_FreeValue(context, current);
			JSValue stop = JS_GetPropertyStr(context, event, "__pixelStop");
			const bool stopped = JS_ToBool(context, stop) != 0; JS_FreeValue(context, stop);
			if (stopped) goto dispatch_done;
		}
		for (size_t p = path.size(); p-- > 1; )
		{
			const std::string ancestorKey = "node:" + std::to_string(path[p]);
			JSValue current = wrapNode(context, path[p]);
			JS_SetPropertyStr(context, event, "eventPhase", JS_NewInt32(context, 1));
			dispatchListenersAt(context, runtime, list, eventType, ancestorKey, current, event, true, false);
			JS_FreeValue(context, current);
			JSValue stop = JS_GetPropertyStr(context, event, "__pixelStop");
			const bool stopped = JS_ToBool(context, stop) != 0; JS_FreeValue(context, stop);
			if (stopped) goto dispatch_done;
		}
		JS_SetPropertyStr(context, event, "eventPhase", JS_NewInt32(context, 2));
		JS_SetPropertyStr(context, event, "__pixelImmediate", JS_NewBool(context, 0));
		dispatchListenersAt(context, runtime, list, eventType, key, targetValue, event, true, true);
		JSValue immediate = JS_GetPropertyStr(context, event, "__pixelImmediate");
		if (!JS_ToBool(context, immediate))
		{
			JS_SetPropertyStr(context, event, "__pixelImmediate", JS_NewBool(context, 0));
			dispatchListenersAt(context, runtime, list, eventType, key, targetValue, event, false, true);
		}
		JS_FreeValue(context, immediate);
		if (bubbles)
		{
			JSValue stop = JS_GetPropertyStr(context, event, "__pixelStop");
			bool stopped = JS_ToBool(context, stop) != 0; JS_FreeValue(context, stop);
			for (size_t p = 1; p < path.size() && !stopped; ++p)
			{
				const std::string ancestorKey = "node:" + std::to_string(path[p]);
				JSValue current = wrapNode(context, path[p]);
				JS_SetPropertyStr(context, event, "eventPhase", JS_NewInt32(context, 3));
				JS_SetPropertyStr(context, event, "__pixelImmediate", JS_NewBool(context, 0));
				dispatchListenersAt(context, runtime, list, eventType, ancestorKey, current, event, false, false);
				JS_FreeValue(context, current);
				JSValue stop = JS_GetPropertyStr(context, event, "__pixelStop");
				stopped = JS_ToBool(context, stop) != 0; JS_FreeValue(context, stop);
			}
			if (!stopped)
			{
				const char* outerKeys[] = { "document", "window" };
				for (const char* outerKey : outerKeys)
				{
					JSValue current = JS_GetPropertyStr(context, global, outerKey);
					JS_SetPropertyStr(context, event, "eventPhase", JS_NewInt32(context, 3));
					dispatchListenersAt(context, runtime, list, eventType, outerKey, current, event, false, false);
					JS_FreeValue(context, current);
					JSValue stop = JS_GetPropertyStr(context, event, "__pixelStop");
					stopped = JS_ToBool(context, stop) != 0; JS_FreeValue(context, stop);
					if (stopped) break;
				}
			}
		}
	}
	else
	{
		JS_SetPropertyStr(context, event, "eventPhase", JS_NewInt32(context, 2));
		dispatchListenersAt(context, runtime, list, eventType, key, targetValue, event, true, true);
		dispatchListenersAt(context, runtime, list, eventType, key, targetValue, event, false, true);
	}
dispatch_done:
	compactEventListeners(context, list);
	JSValue prevented = JS_GetPropertyStr(context, event, "defaultPrevented");
	const bool allowed = !JS_ToBool(context, prevented);
	JS_FreeValue(context, prevented);
	JS_FreeValue(context, event); JS_FreeValue(context, targetValue);
	JS_FreeValue(context, list); JS_FreeValue(context, global);
	jsPageExecutionEnd(runtime);
	return allowed;
}

void jsPageContextDestroy(JSRuntime*& runtime, JSContext*& context)
{
	if (context)
	{
		JS_FreeContext(context);
		context = nullptr;
	}
	if (runtime)
	{
		delete static_cast<ExecutionBudget*>(JS_GetRuntimeOpaque(runtime));
		JS_FreeRuntime(runtime);
		runtime = nullptr;
	}
}

bool jsPageExecutionBegin(JSRuntime* runtime, bool chargePageBudget)
{
	if (!runtime) return false;
	ExecutionBudget* budget = static_cast<ExecutionBudget*>(JS_GetRuntimeOpaque(runtime));
	if (!budget || budget->active ||
		(chargePageBudget && budget->pageElapsedMs >= kJsPageBudgetMs))
		return false;
	const uint64_t now = osGetTime();
	const uint64_t remaining = chargePageBudget ?
		kJsPageBudgetMs - budget->pageElapsedMs : kJsScriptBudgetMs;
	const uint64_t slice = remaining < kJsScriptBudgetMs ? remaining : kJsScriptBudgetMs;
	budget->executionStartedMs = now;
	budget->executionDeadlineMs = now + slice;
	budget->interrupted = false;
	budget->chargePageBudget = chargePageBudget;
	budget->active = true;
	return true;
}

bool jsPageExecutionEnd(JSRuntime* runtime)
{
	if (!runtime) return false;
	ExecutionBudget* budget = static_cast<ExecutionBudget*>(JS_GetRuntimeOpaque(runtime));
	if (!budget || !budget->active) return false;
	const uint64_t elapsed = osGetTime() - budget->executionStartedMs;
	if (budget->chargePageBudget) budget->pageElapsedMs += elapsed;
	budget->active = false;
	return budget->interrupted;
}

bool jsPageEval(JSRuntime* runtime, JSContext* context,
	const char* source, size_t length, const char* filename)
{
	if (!runtime || !context || !source || !jsPageExecutionBegin(runtime))
		return false;

	JSValue result = JS_Eval(context, source, length,
		filename ? filename : "<inline>", JS_EVAL_TYPE_GLOBAL);
	bool ok = !JS_IsException(result);
	JS_FreeValue(context, result);
	if (!ok)
	{
		JSValue exception = JS_GetException(context);
		JS_FreeValue(context, exception);
	}

	// Promise callbacks queued during the script belong to the same bounded
	// execution slice. Leave the queue untouched if the runtime was interrupted.
	if (ok) ok = executePendingJobs(runtime, context);
	if (jsPageExecutionEnd(runtime)) ok = false;
	return ok;
}
