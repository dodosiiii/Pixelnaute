#include "http.hpp"
#include <3ds.h>
#include <string.h>

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

		if ((int)status >= 400)
		{
			httpcCloseContext(&ctx);
			out.error = "Erreur HTTP " + itos((long)status);
			return false;
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
					httpcCloseContext(&ctx);
					out.tooBig = true;
					out.error = "Page trop lourde pour la memoire de la 3DS (" +
						itos((long)(opt.maxBytes / 1024)) + " Ko max)";
					return false;
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

void httpServiceExit()
{
	httpcExit();
}

//--------------------------------------------------------------------------
// Implementation HTTP(S) via libcurl + mbedtls
//--------------------------------------------------------------------------
#include <curl/curl.h>

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
		return 0;
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

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
	curl_easy_setopt(h, CURLOPT_USERAGENT, opt.userAgent.c_str());
	curl_easy_setopt(h, CURLOPT_COOKIEFILE, "");
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(h, CURLOPT_MAXREDIRS, (long)NW_MAX_REDIRECTS);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 8L);
	curl_easy_setopt(h, CURLOPT_TIMEOUT, (long)((opt.timeoutMs + 999) / 1000));
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

	if (ctx.tooBig)
	{
		out.tooBig = true;
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

	if (rc != CURLE_OK)
	{
		out.error = std::string("Erreur reseau : ") + curl_easy_strerror(rc);
		return false;
	}

	if (status >= 400)
	{
		out.error = "Erreur HTTP " + itos(status);
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