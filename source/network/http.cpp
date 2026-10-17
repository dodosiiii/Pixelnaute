#include "http.hpp"
#include <3ds.h>
#include <string.h>
#include <curl/curl.h>

//--------------------------------------------------------------------------
static bool isRedirectWindow(int code)
{
	return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
}

static bool isCompleteUrl(const std::string& u)
{
	return strStartsWith(u, "http://") || strStartsWith(u, "https://");
}

static std::string resolveRedirect(const std::string& base, const std::string& ref)
{
	return urlJoin(base, ref);
}

//--------------------------------------------------------------------------
// Conversion de codepage Windows-1252 / ISO-8859-1 vers UTF-8
//--------------------------------------------------------------------------
static uint32_t cp1252ToUnicode(uint8_t b)
{
	if (b < 0x80) return b;
	if (b >= 0xA0) return b;
	static const uint16_t kCp1252[32] = {
		0x20AC, 0x003F, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x003F, 0x017D, 0x003F,
		0x003F, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x003F, 0x017E, 0x0178
	};
	return kCp1252[b - 0x80];
}

static bool isValidUtf8(const std::string& s)
{
	size_t i = 0, n = s.size();
	while (i < n)
	{
		uint8_t c = (uint8_t)s[i++];
		if (c < 0x80) continue;
		int extra = 0;
		if ((c & 0xE0) == 0xC0) extra = 1;
		else if ((c & 0xF0) == 0xE0) extra = 2;
		else if ((c & 0xF8) == 0xF0) extra = 3;
		else return false;
		if (i + extra > n) return false;
		for (int k = 0; k < extra; k++)
		{
			uint8_t follow = (uint8_t)s[i++];
			if ((follow & 0xC0) != 0x80) return false;
		}
	}
	return true;
}

std::string convertEncodingToUtf8(const std::string& input, const std::string& contentType)
{
	if (input.empty()) return input;

	std::string ct = toLower(contentType);
	bool explicitLatin = (ct.find("iso-8859") != std::string::npos ||
	                      ct.find("windows-1252") != std::string::npos ||
	                      ct.find("latin1") != std::string::npos ||
	                      ct.find("cp1252") != std::string::npos);

	if (!explicitLatin && input.size() > 0)
	{
		size_t headLen = input.size() < 2048 ? input.size() : 2048;
		std::string head = toLower(input.substr(0, headLen));
		if (head.find("iso-8859") != std::string::npos ||
		    head.find("windows-1252") != std::string::npos ||
		    head.find("latin1") != std::string::npos)
		{
			explicitLatin = true;
		}
	}

	if (!explicitLatin && isValidUtf8(input))
		return input;

	std::string out;
	out.reserve(input.size() + input.size() / 4);
	for (size_t i = 0; i < input.size(); i++)
	{
		uint8_t b = (uint8_t)input[i];
		if (b < 0x80)
			out += (char)b;
		else
		{
			uint32_t cp = cp1252ToUnicode(b);
			utf8Append(out, cp);
		}
	}
	return out;
}

//--------------------------------------------------------------------------
// Boucle complete d'une requete (avec redirections) selon la politique TLS.
//--------------------------------------------------------------------------
static bool httpGetInternal(const std::string& url0, const HttpOptions& opt,
	const volatile int* cancelFlag, float* progressOut, NetResponse& out,
	bool bypassTls)
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
			if (bypassTls || !opt.verifyTls)
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

		r = httpcBeginRequest(&ctx);
		if (R_FAILED(r))
		{
			httpcCloseContext(&ctx);
			const bool isSslErr = (r == (Result)0xD8A0A03C) ||
				(((r >> 10) & 0xFF) == 0x28 && (r & 0x3FF) == 0x3C) ||
				(((r >> 10) & 0xFF) == 0x20);

			if (isHttps && opt.verifyTls && opt.allowTlsBypass && !bypassTls && isSslErr)
			{
				// Repli automatique avec bypass
				bypassTls = true;
				out.sslBypassUsed = true;
				continue;
			}
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

				if (isHttps && opt.verifyTls && opt.allowTlsBypass && !bypassTls && isSslErr)
				{
					// Repli automatique avec bypass
					bypassTls = true;
					out.sslBypassUsed = true;
					continue;
				}
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
		if (total > 0 && total <= 8 * 1024 * 1024) body.reserve(total);

		Result r2 = 0;
		for (;;)
		{
			r2 = httpcDownloadData(&ctx, (u8*)buf, sizeof(buf), &read);
			if (read > 0)
			{
				if (opt.maxBytes && body.size() + read > opt.maxBytes)
				{
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
				body.append(buf, read);
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
bool httpServiceInit()
{
	return R_SUCCEEDED(httpcInit(0));
}

static CURLSH* g_curlShare = nullptr;
static LightLock g_curlShareLock;
static bool g_curlShareInit = false;

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
	if (!g_curlShareInit)
	{
		LightLock_Init(&g_curlShareLock);
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
}

static void cleanupCurlShare()
{
	if (g_curlShareInit)
	{
		if (g_curlShare)
		{
			curl_share_cleanup(g_curlShare);
			g_curlShare = nullptr;
		}
		g_curlShareInit = false;
	}
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
	const volatile int* cancel;
	std::atomic<float>* progress;
	std::atomic<size_t>* bytes;
	bool tooBig;
};

static size_t onBody(char* p, size_t sz, size_t n, void* ud)
{
	CurlCtx* c = (CurlCtx*)ud;
	const size_t len = sz * n;
	if (c->maxBytes && c->out->body.size() + len > c->maxBytes)
	{
		c->tooBig = true;
		if (c->out->body.size() < c->maxBytes)
		{
			size_t take = c->maxBytes - c->out->body.size();
			c->out->body.append(p, take);
			if (c->bytes)
				c->bytes->store(c->out->body.size(), std::memory_order_relaxed);
		}
		return 0; // stoppe le telechargement pour eviter le debordement memoire
	}
	c->out->body.append(p, len);
	if (c->bytes)
		c->bytes->store(c->out->body.size(), std::memory_order_relaxed);
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
	const volatile int* cancelFlag, std::atomic<float>* progressOut,
	std::atomic<size_t>* bytesOut)
{
	CURL* h = curl_easy_init();
	if (!h)
	{
		out.error = "curl indisponible";
		return false;
	}

	CurlCtx ctx{ &out, opt.maxBytes, cancelFlag, progressOut, bytesOut, false };

	curl_slist* hdr = curl_slist_append(nullptr, ("Accept: " + opt.accept).c_str());
	hdr = curl_slist_append(hdr, "Accept-Language: fr,en;q=0.8");
	if (opt.method == "POST")
	{
		curl_easy_setopt(h, CURLOPT_POST, 1L);
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, opt.postData.c_str());
		curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)opt.postData.size());
		hdr = curl_slist_append(hdr, "Content-Type: application/x-www-form-urlencoded");
	}

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
	curl_easy_setopt(h, CURLOPT_USERAGENT, opt.userAgent.c_str());
	if (!opt.referer.empty())
		curl_easy_setopt(h, CURLOPT_REFERER, opt.referer.c_str());

	ensureCurlShare();
	if (g_curlShare)
		curl_easy_setopt(h, CURLOPT_SHARE, g_curlShare);

	curl_easy_setopt(h, CURLOPT_COOKIEFILE, "");
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(h, CURLOPT_MAXREDIRS, (long)NW_MAX_REDIRECTS);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 8L);
	curl_easy_setopt(h, CURLOPT_TIMEOUT, (long)((opt.timeoutMs + 999) / 1000));
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, onBody);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &ctx);
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
	out.contentType = ct ? ct : "";
	out.url = eff ? eff : url; // <- l'URL finale, enfin renseignee

	curl_slist_free_all(hdr);
	curl_easy_cleanup(h);

	if (!out.body.empty())
		out.body = convertEncodingToUtf8(out.body, out.contentType);

	if (ctx.tooBig)
	{
		out.tooBig = true;
		if (!out.body.empty())
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
		if (!out.body.empty())
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
	const volatile int* cancelFlag, std::atomic<float>* progressOut,
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

	if (!httpGetCurl(url0, opt, out, cancelFlag, progressOut, bytesOut))
	{
		if (out.sslError && opt.allowTlsBypass && opt.verifyTls)
		{
			// Retry sans verification TLS
			HttpOptions optNoVerify = opt;
			optNoVerify.verifyTls = false;
			NetResponse out2;
			if (httpGetCurl(url0, optNoVerify, out2, cancelFlag, progressOut, bytesOut))
			{
				out2.sslBypassUsed = true;
				out = out2;
				return true;
			}
			out.sslError = false;
			out.error = "Connexion securisee impossible.";
			out.sslBypassUsed = true;
		}
		return false;
	}
	return true;
}