#include "http.hpp"
#include "../storage/storage.hpp"
#include <3ds.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>

//--------------------------------------------------------------------------
static bool isRedirectWindow(int code)
{
	return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
}

static std::string resolveRedirect(const std::string& base, const std::string& ref)
{
	return urlJoin(base, ref);
}

// Boucle complete d'une requete (avec redirections) selon la politique TLS.
//--------------------------------------------------------------------------
[[maybe_unused]] static bool httpGetInternal(const std::string& url0, const HttpOptions& opt,
	const std::atomic<int>* cancelFlag, float* progressOut, NetResponse& out,
	std::atomic<size_t>* bytesOut = nullptr)
{
	std::string url = url0;
	httpcContext ctx;

	for (int redirect = 0; redirect < NW_MAX_REDIRECTS; redirect++)
	{
		out.url = url; // URL finale (mise a jour a chaque redirection)
		Result r = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url.c_str(), 1);
		if (R_FAILED(r))
		{
			out.error = "Impossible d'ouvrir la connexion";
			return false;
		}

		bool isHttps = strStartsWith(url, "https://");
		if (isHttps)
		{
			for (int cert = (int)SSLC_DefaultRootCert_Nintendo_CA; cert <= (int)SSLC_DefaultRootCert_DigiCert_EV; cert++)
				httpcAddDefaultCert(&ctx, (SSLC_DefaultRootCert)cert);

			u32 sslOpts = SSLCOPT_Default;
			if (!opt.verifyTls)
				sslOpts = SSLCOPT_DisableVerify;

			httpcSetSSLOpt(&ctx, sslOpts);
		}

		httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
		httpcAddRequestHeaderField(&ctx, "User-Agent", opt.userAgent.c_str());
		httpcAddRequestHeaderField(&ctx, "Accept", opt.accept.c_str());
		httpcAddRequestHeaderField(&ctx, "Accept-Language", "fr,en;q=0.8");
		httpcAddRequestHeaderField(&ctx, "Connection", "close");
		// Desactiver la compression gzip : le service httpc de la 3DS ne
		// decompresse pas automatiquement, et un corps gzippe rend le status
		// code illisible (httpcGetResponseStatusCode renvoie une erreur).
		httpcAddRequestHeaderField(&ctx, "Accept-Encoding", "identity");

		std::string httpcRef = opt.referer;
		if (httpcRef.empty() && opt.method == "POST") httpcRef = url;
		if (!httpcRef.empty())
			httpcAddRequestHeaderField(&ctx, "Referer", httpcRef.c_str());
		if (opt.method == "POST")
		{
			size_t pe = httpcRef.find("://");
			if (pe != std::string::npos)
			{
				size_t he = httpcRef.find('/', pe + 3);
				std::string og = (he != std::string::npos) ? httpcRef.substr(0, he) : httpcRef;
				httpcAddRequestHeaderField(&ctx, "Origin", og.c_str());
			}
		}

		r = httpcBeginRequest(&ctx);
		if (R_FAILED(r))
		{
			httpcCloseContext(&ctx);
			const bool isSslErr = (r == (Result)0xD8A0A03C) ||
				(((r >> 10) & 0xFF) == 0x28 && (r & 0x3FF) == 0x3C) ||
				(((r >> 10) & 0xFF) == 0x20);

			if (isHttps && isSslErr)
			{
				out.sslError = true;
				out.error = "Connexion securisee impossible (0x" + toHex(r) + ")";
				return false;
			}
			out.error = "Erreur reseau (0x" + toHex(r) + ")";
			return false;
		}

		u32 status = 0;
		{
			// Certains serveurs prennent un peu de temps a envoyer les headers.
			// On retente jusqu'a 60 fois avec 50 ms (jusqu'a 3 secondes).
			Result rs = HTTPC_RESULTCODE_DOWNLOADPENDING;
			for (int attempt = 0; attempt < 60 && rs == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING; attempt++)
			{
				if (cancelFlag && *cancelFlag)
				{
					httpcCloseContext(&ctx);
					out.cancelled = true;
					out.error = "Telechargement annule";
					return false;
				}
				rs = httpcGetResponseStatusCode(&ctx, &status);
				if (rs == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING)
					svcSleepThread(50000000LL); // 50 ms
			}
			if (R_FAILED(rs))
			{
				httpcCloseContext(&ctx);
				const bool isSslErr = (rs == (Result)0xD8A0A03C) ||
					(((rs >> 10) & 0xFF) == 0x28 && (rs & 0x3FF) == 0x3C) ||
					(((rs >> 10) & 0xFF) == 0x20);

				if (isHttps && isSslErr)
				{
					out.sslError = true;
					out.error = "Connexion securisee impossible (0x" + toHex(rs) + ")";
					return false;
				}
				out.error = "Reponse illisible (0x" + toHex(rs) + ")";
				return false;
			}
		}
		out.status = (int)status;

		char ct[256] = { 0 };
		if (R_SUCCEEDED(httpcGetResponseHeader(&ctx, "Content-Type", ct, sizeof(ct))))
			out.contentType = ct;

		if (isRedirectWindow((int)status))
		{
			char loc[2048] = { 0 };
			if (R_SUCCEEDED(httpcGetResponseHeader(&ctx, "Location", loc, sizeof(loc))))
			{
				httpcCloseContext(&ctx);
				url = resolveRedirect(url, loc);
				continue;
			}
		}

		// --- corps ---
		u32 total = 0, done = 0;
		httpcGetDownloadSizeState(&ctx, &done, &total);

		char buf[NW_HTTP_CHUNK];
		u32 read = 0;
		std::string body;
		size_t received = 0;
		if (total > 0 && total <= 8 * 1024 * 1024) body.reserve(total);

		Result r2 = 0;
		for (;;)
		{
			r2 = httpcDownloadData(&ctx, (u8*)buf, sizeof(buf), &read);
			if (read > 0)
			{
				if (opt.maxBytes && received + read > opt.maxBytes)
				{
					if (opt.outputFile)
					{
						httpcCloseContext(&ctx);
						out.tooBig = true;
						out.error = "Fichier trop volumineux";
						return false;
					}
					if (body.size() < opt.maxBytes)
					{
						size_t take = opt.maxBytes - body.size();
						body.append(buf, take);
					}
					httpcCloseContext(&ctx);
					out.tooBig = true;
					out.body = std::move(body);
					out.ok = true;
					if (progressOut) *progressOut = 1.0f;
					return true;
				}
				if (opt.outputFile)
				{
					if (fwrite(buf, 1, read, opt.outputFile) != read)
					{
						httpcCloseContext(&ctx);
						out.error = "Erreur pendant l'ecriture du telechargement";
						return false;
					}
				}
				else body.append(buf, read);
				received += read;
				if (bytesOut) bytesOut->store(received, std::memory_order_relaxed);
			}
			httpcGetDownloadSizeState(&ctx, &done, &total);
			if (progressOut)
				*progressOut = total > 0 ? fclamp((float)done / (float)total, 0.0f, 1.0f) : 0.0f;
			if (cancelFlag && *cancelFlag)
			{
				httpcCloseContext(&ctx);
				out.cancelled = true;
				out.error = "Telechargement annule";
				return false;
			}
			if (r2 == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) continue;
			break;
		}

		httpcCloseContext(&ctx);

		if (R_FAILED(r2) && !out.cancelled)
		{
			out.error = "Erreur pendant le telechargement (0x" + toHex(r2) + ")";
			return false;
		}

		out.body = std::move(body);
		if ((int)status >= 400)
		{
			out.error = "Erreur HTTP " + itos((long)status);
			if (!out.body.empty())
			{
				out.ok = true;
				if (progressOut) *progressOut = 1.0f;
				return true;
			}
			return false;
		}
		out.ok = true;
		if (progressOut) *progressOut = 1.0f;
		return true;
	}

	out.error = "Trop de redirections";
	return false;
}

//--------------------------------------------------------------------------
static CURLSH* g_curlShare = nullptr;
static LightLock g_curlInitLock;
static LightLock g_curlShareLock;
static LightLock g_curlEasyLock;
static CURL* g_curlEasy = nullptr;
static bool g_curlShareInit = false;
static bool g_curlLocksReady = false;

bool httpServiceInit()
{
	if (!g_curlLocksReady)
	{
		LightLock_Init(&g_curlInitLock);
		LightLock_Init(&g_curlShareLock);
		LightLock_Init(&g_curlEasyLock);
		g_curlLocksReady = true;
	}
	return R_SUCCEEDED(httpcInit(0));
}

static void curlShareLockCallback(CURL*, curl_lock_data, curl_lock_access, void*)
{
	LightLock_Lock(&g_curlShareLock);
}

static void curlShareUnlockCallback(CURL*, curl_lock_data, void*)
{
	LightLock_Unlock(&g_curlShareLock);
}

static void ensureCurlShare()
{
	if (g_curlLocksReady) LightLock_Lock(&g_curlInitLock);
	if (!g_curlShareInit)
	{
		g_curlShare = curl_share_init();
		if (g_curlShare)
		{
			curl_share_setopt(g_curlShare, CURLSHOPT_LOCKFUNC, curlShareLockCallback);
			curl_share_setopt(g_curlShare, CURLSHOPT_UNLOCKFUNC, curlShareUnlockCallback);
			curl_share_setopt(g_curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
			curl_share_setopt(g_curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
			curl_share_setopt(g_curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
		}
		g_curlShareInit = true;
	}
	if (g_curlLocksReady) LightLock_Unlock(&g_curlInitLock);
}

static void cleanupCurlShare()
{
	if (g_curlLocksReady) LightLock_Lock(&g_curlInitLock);
	if (g_curlEasy)
	{
		curl_easy_cleanup(g_curlEasy);
		g_curlEasy = nullptr;
	}
	if (g_curlShareInit)
	{
		if (g_curlShare)
		{
			curl_share_cleanup(g_curlShare);
			g_curlShare = nullptr;
		}
		g_curlShareInit = false;
	}
	if (g_curlLocksReady) LightLock_Unlock(&g_curlInitLock);
}

static bool cookieMatchesUrl(const std::string& domainField, const std::string& pathField,
	bool tailmatch, bool secure, long expires, const std::string& url)
{
	const size_t schemeEnd = url.find("://"); if (schemeEnd == std::string::npos) return false;
	const bool isSecure = url.compare(0, schemeEnd, "https") == 0;
	const size_t hostStart = schemeEnd + 3, hostEnd = url.find_first_of("/:?#", hostStart);
	std::string host = url.substr(hostStart, hostEnd == std::string::npos ? hostEnd : hostEnd - hostStart);
	for (char& ch : host) ch = (char)tolower((unsigned char)ch);
	std::string domain = domainField; while (!domain.empty() && domain.front() == '.') domain.erase(domain.begin());
	for (char& ch : domain) ch = (char)tolower((unsigned char)ch);
	const bool domainOk = host == domain || (tailmatch && host.size() > domain.size() &&
		host.compare(host.size() - domain.size(), domain.size(), domain) == 0 && host[host.size() - domain.size() - 1] == '.');
	std::string path = "/";
	if (hostEnd != std::string::npos && url[hostEnd] == '/') { size_t end = url.find_first_of("?#", hostEnd); path = url.substr(hostEnd, end == std::string::npos ? end : end - hostEnd); }
	const bool pathOk = path.compare(0, pathField.size(), pathField) == 0 &&
		(path.size() == pathField.size() || (!pathField.empty() && pathField.back() == '/') || path[pathField.size()] == '/');
	return domainOk && pathOk && (!secure || isSecure) && (expires == 0 || expires >= (long)time(nullptr));
}

std::string httpCookieHeaderForUrl(const std::string& url)
{
	ensureCurlShare(); if (!g_curlShare) return std::string();
	CURL* h = curl_easy_init(); if (!h) return std::string();
	curl_easy_setopt(h, CURLOPT_URL, url.c_str()); curl_easy_setopt(h, CURLOPT_SHARE, g_curlShare);
	curl_easy_setopt(h, CURLOPT_COOKIEFILE, NW_SD_COOKIES);
	curl_easy_setopt(h, CURLOPT_COOKIELIST, "RELOAD");
	curl_slist* cookies = nullptr; std::string result;
	if (curl_easy_getinfo(h, CURLINFO_COOKIELIST, &cookies) == CURLE_OK)
	{
		for (curl_slist* item = cookies; item; item = item->next)
		{
			std::string row = item->data ? item->data : "";
			const bool httpOnly = row.rfind("#HttpOnly_", 0) == 0;
			if (httpOnly) continue;
			std::string fields[7]; size_t p = 0; int n = 0;
			while (n < 6) { size_t tab = row.find('\t', p); if (tab == std::string::npos) break; fields[n++] = row.substr(p, tab - p); p = tab + 1; }
			if (n != 6) continue;
			fields[6] = row.substr(p);
			if (!cookieMatchesUrl(fields[0], fields[2], fields[1] == "TRUE", fields[3] == "TRUE", atol(fields[4].c_str()), url)) continue;
			if (!result.empty()) result += "; ";
			result += fields[5] + "=" + fields[6];
		}
	}
	curl_slist_free_all(cookies); curl_easy_cleanup(h); return result;
}

bool httpSetCookieForUrl(const std::string& url, const std::string& cookie)
{
	if (url.find("://") == std::string::npos || cookie.empty() || cookie.size() > 4096) return false;
	ensureCurlShare(); if (!g_curlShare) return false;
	CURL* h = curl_easy_init(); if (!h) return false;
	curl_easy_setopt(h, CURLOPT_URL, url.c_str()); curl_easy_setopt(h, CURLOPT_SHARE, g_curlShare);
	curl_easy_setopt(h, CURLOPT_COOKIEFILE, NW_SD_COOKIES);
	const std::string setCookie = "Set-Cookie: " + cookie;
	const CURLcode rc = curl_easy_setopt(h, CURLOPT_COOKIELIST, setCookie.c_str());
	curl_easy_setopt(h, CURLOPT_COOKIEJAR, NW_SD_COOKIES);
	curl_easy_cleanup(h);
	return rc == CURLE_OK;
}

void httpServiceExit()
{
	cleanupCurlShare();
	httpcExit();
}

//--------------------------------------------------------------------------
// Implementation HTTP(S) via libcurl + mbedtls
//--------------------------------------------------------------------------

struct CurlCtx
{
	NetResponse* out;
	size_t maxBytes;
	const std::atomic<int>* cancel;
	std::atomic<float>* progress;
	std::atomic<size_t>* bytes;
	FILE* outputFile;
	size_t received;
	bool tooBig;
	std::string* etag;
	std::string* lastModified;
};

static size_t onHeader(char* data, size_t size, size_t count, void* userData)
{
	const size_t length = size * count;
	CurlCtx* ctx = static_cast<CurlCtx*>(userData);
	std::string line(data, length);
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
	if (strStartsWith(line, "HTTP/"))
	{
		if (ctx->etag) ctx->etag->clear();
		if (ctx->lastModified) ctx->lastModified->clear();
		return length;
	}
	const size_t colon = line.find(':');
	if (colon == std::string::npos) return length;
	std::string name = toLower(strTrim(line.substr(0, colon)));
	std::string value = strTrim(line.substr(colon + 1));
	if (name == "etag" && ctx->etag) *ctx->etag = value;
	else if (name == "last-modified" && ctx->lastModified) *ctx->lastModified = value;
	return length;
}

static size_t onBody(char* p, size_t sz, size_t n, void* ud)
{
	CurlCtx* c = (CurlCtx*)ud;
	const size_t len = sz * n;
	if (c->maxBytes && c->received + len > c->maxBytes)
	{
		c->tooBig = true;
		if (c->received < c->maxBytes)
		{
			size_t take = c->maxBytes - c->received;
			if (c->outputFile) fwrite(p, 1, take, c->outputFile);
			else c->out->body.append(p, take);
			c->received += take;
			if (c->bytes)
				c->bytes->store(c->received, std::memory_order_relaxed);
		}
		return 0; // stoppe le telechargement pour eviter le debordement memoire
	}
	if (c->outputFile)
	{
		if (fwrite(p, 1, len, c->outputFile) != len) return 0;
	}
	else c->out->body.append(p, len);
	c->received += len;
	if (c->bytes)
		c->bytes->store(c->received, std::memory_order_relaxed);
	return len;
}

static int onProgress(void* ud, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
{
	CurlCtx* c = (CurlCtx*)ud;
	if (c->progress && total > 0)
		c->progress->store(fclamp((float)now / (float)total, 0.0f, 1.0f),
			std::memory_order_relaxed);
	return (c->cancel && *c->cancel) ? 1 : 0; // 1 = annuler
}

bool httpGetCurl(const std::string& url, const HttpOptions& opt, NetResponse& out,
	const std::atomic<int>* cancelFlag, std::atomic<float>* progressOut,
	std::atomic<size_t>* bytesOut)
{
	ensureCurlShare();
	LightLock_Lock(&g_curlEasyLock);
	if (!g_curlEasy) g_curlEasy = curl_easy_init();
	CURL* h = g_curlEasy;
	if (!h)
	{
		LightLock_Unlock(&g_curlEasyLock);
		out.error = "curl indisponible";
		return false;
	}
	// reset oublie les options de la requete precedente mais conserve le pool
	// de connexions TCP du handle pour les visites suivantes.
	curl_easy_reset(h);

	CurlCtx ctx{ &out, opt.maxBytes, cancelFlag, progressOut, bytesOut,
		opt.outputFile, (size_t)opt.resumeFrom, false, &out.etag, &out.lastModified };

	curl_slist* hdr = curl_slist_append(nullptr, ("Accept: " + opt.accept).c_str());
	hdr = curl_slist_append(hdr, "Accept-Language: fr,en;q=0.8");
	if (!opt.ifNoneMatch.empty())
		hdr = curl_slist_append(hdr, ("If-None-Match: " + opt.ifNoneMatch).c_str());
	if (!opt.ifModifiedSince.empty())
		hdr = curl_slist_append(hdr, ("If-Modified-Since: " + opt.ifModifiedSince).c_str());

	std::string ref = opt.referer;
	if (ref.empty() && opt.method == "POST")
		ref = url;

	if (opt.method == "POST")
	{
		curl_easy_setopt(h, CURLOPT_POST, 1L);
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, opt.postData.c_str());
		curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)opt.postData.size());
		hdr = curl_slist_append(hdr, "Content-Type: application/x-www-form-urlencoded");

		// En-tetes Fetch Metadata (protection CSRF standardisee moderne)
		hdr = curl_slist_append(hdr, "Sec-Fetch-Dest: document");
		hdr = curl_slist_append(hdr, "Sec-Fetch-Mode: navigate");
		hdr = curl_slist_append(hdr, "Sec-Fetch-Site: same-origin");
		hdr = curl_slist_append(hdr, "Sec-Fetch-User: ?1");
	}

	// En-tete Origin (indispensable pour la verification CSRF de la plupart des frameworks)
	std::string originSrc = !ref.empty() ? ref : url;
	size_t protoEnd = originSrc.find("://");
	if (protoEnd != std::string::npos)
	{
		size_t hostEnd = originSrc.find('/', protoEnd + 3);
		std::string origin = (hostEnd != std::string::npos) ? originSrc.substr(0, hostEnd) : originSrc;
		if (opt.method == "POST" || !opt.referer.empty())
			hdr = curl_slist_append(hdr, ("Origin: " + origin).c_str());
	}

	// En-tetes personnalises CSRF si un jeton a ete extrait de la page
	if (!opt.csrfToken.empty())
	{
		hdr = curl_slist_append(hdr, ("X-CSRF-Token: " + opt.csrfToken).c_str());
		hdr = curl_slist_append(hdr, ("X-CSRFToken: " + opt.csrfToken).c_str());
		hdr = curl_slist_append(hdr, ("X-XSRF-Token: " + opt.csrfToken).c_str());
	}

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	if (opt.resumeFrom > 0)
		curl_easy_setopt(h, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)opt.resumeFrom);
	curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
	curl_easy_setopt(h, CURLOPT_USERAGENT, opt.userAgent.c_str());
	if (!ref.empty())
		curl_easy_setopt(h, CURLOPT_REFERER, ref.c_str());

	if (g_curlShare)
		curl_easy_setopt(h, CURLOPT_SHARE, g_curlShare);

	// Persistance des cookies sur carte SD (cookies de session, jetons CSRF)
	curl_easy_setopt(h, CURLOPT_COOKIEFILE, NW_SD_COOKIES);
	curl_easy_setopt(h, CURLOPT_COOKIEJAR, NW_SD_COOKIES);
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(h, CURLOPT_MAXREDIRS, (long)NW_MAX_REDIRECTS);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 8L);
	curl_easy_setopt(h, CURLOPT_TIMEOUT, (long)((opt.timeoutMs + 999) / 1000));
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, onBody);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &ctx);
	curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, onHeader);
	curl_easy_setopt(h, CURLOPT_HEADERDATA, &ctx);
	curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, onProgress);
	curl_easy_setopt(h, CURLOPT_XFERINFODATA, &ctx);
	if (opt.verifyTls)
		curl_easy_setopt(h, CURLOPT_CAINFO, "romfs:/cacert.pem");
	else
	{
		curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	const CURLcode rc = curl_easy_perform(h);

	long status = 0;
	char* ct = nullptr;
	char* eff = nullptr;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_getinfo(h, CURLINFO_CONTENT_TYPE, &ct);
	curl_easy_getinfo(h, CURLINFO_EFFECTIVE_URL, &eff);

	out.status = (int)status;
	out.notModified = status == 304;
	out.contentType = ct ? ct : "";
	out.url = eff ? eff : url; // <- l'URL finale, enfin renseignee
	curl_easy_setopt(h, CURLOPT_COOKIELIST, "FLUSH");

	curl_slist_free_all(hdr);
	LightLock_Unlock(&g_curlEasyLock);

	if (opt.convertTextEncoding && !out.body.empty())
		out.body = convertEncodingToUtf8(out.body, out.contentType, opt.convertTextEncoding);

	if (ctx.tooBig)
	{
		out.tooBig = true;
		if (!out.body.empty() || ctx.received > 0)
		{
			out.ok = true;
			if (progressOut) progressOut->store(1.0f, std::memory_order_relaxed);
			return true;
		}
		out.error = "Page trop lourde pour la 3DS";
		return false;
	}

	if (rc == CURLE_ABORTED_BY_CALLBACK)
	{
		out.cancelled = true;
		out.error = "Annule";
		return false;
	}

	if (rc == CURLE_SSL_CONNECT_ERROR || rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT_BADFILE)
	{
		out.sslError = true;
		out.certError = rc == CURLE_PEER_FAILED_VERIFICATION;
		out.error = curl_easy_strerror(rc);
		return false;
	}

	if (rc != CURLE_OK && rc != CURLE_WRITE_ERROR)
	{
		out.error = std::string("Erreur reseau : ") + curl_easy_strerror(rc);
		return false;
	}

	if (status >= 400)
	{
		out.error = "Erreur HTTP " + itos(status);
		if (!out.body.empty() || ctx.received > 0)
		{
			out.ok = true;
			if (progressOut) progressOut->store(1.0f, std::memory_order_relaxed);
			return true;
		}
		return false;
	}

	out.ok = true;
	if (progressOut) progressOut->store(1.0f, std::memory_order_relaxed);
	return true;
}

//--------------------------------------------------------------------------
// API publique
//--------------------------------------------------------------------------
bool httpGet(const std::string& url0, const HttpOptions& opt, NetResponse& out,
	const std::atomic<int>* cancelFlag, std::atomic<float>* progressOut,
	std::atomic<size_t>* bytesOut)
{
	out = NetResponse();
	if (progressOut) progressOut->store(0.0f, std::memory_order_relaxed);
	if (bytesOut) bytesOut->store(0, std::memory_order_relaxed);

	const std::string url = strTrim(url0);
	const std::string lowerUrl = toLower(url);
	const size_t schemeLength = strStartsWith(lowerUrl, "https://") ? 8 :
		(strStartsWith(lowerUrl, "http://") ? 7 : 0);
	size_t hostEnd = url.find_first_of("/?#", schemeLength);
	if (hostEnd == std::string::npos) hostEnd = url.size();
	bool malformed = schemeLength == 0 || hostEnd == schemeLength;
	for (char c : url)
		if ((unsigned char)c <= 0x20 || c == '\\' || c == '<' || c == '>' ||
			c == '"' || c == '\'')
			malformed = true;
	if (malformed)
	{
		out.url = url;
		out.error = "Adresse invalide : une URL complete en http:// ou https:// est requise.";
		return false;
	}

	return httpGetCurl(url0, opt, out, cancelFlag, progressOut, bytesOut);
}
