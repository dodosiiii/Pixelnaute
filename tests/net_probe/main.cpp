//=============================================================================
// tests/net_probe/main.cpp - Sonde reseau et recuperation de pages reelles
//
// Deux fonctions :
//   1. verifier que la pile reseau fonctionne (soc + httpc) ;
//   2. enregistrer le HTML de plusieurs pages reelles dans des fichiers, afin
//      de pouvoir mettre au point le rendu hors ligne.
//
// Resultat : sdmc:/netprobe.txt et sdmc:/page_<n>.html
//=============================================================================
#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <string>

// Taille maximale enregistree par page : le moteur de rendu est de toute
// facon limite a 2 Mio, et une page coupee montre deja sa structure.
static const size_t kMaxSaved = 400 * 1024;

static FILE* g_log = nullptr;

static void note(const std::string& text)
{
	if (!g_log) g_log = fopen("sdmc:/netprobe.txt", "a");
	if (!g_log) return;
	fprintf(g_log, "%s\n", text.c_str());
	fflush(g_log);
}

static std::string num(long v)
{
	char b[32];
	snprintf(b, sizeof(b), "%ld", v);
	return b;
}

//--------------------------------------------------------------------------
static bool fetch(const char* url, std::string& body, long& status,
	std::string& contentType, std::string& error)
{
	httpcContext ctx;
	status = 0;
	std::string current = url;

	for (int hop = 0; hop < 5; hop++)
	{
		Result r = httpcOpenContext(&ctx, HTTPC_METHOD_GET, current.c_str(), 1);
		if (R_FAILED(r))
		{
			error = "httpcOpenContext 0x" + num((long)(unsigned)r);
			return false;
		}
		httpcSetSSLOpt(&ctx, SSLCOPT_Default);
		httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
		httpcAddRequestHeaderField(&ctx, "User-Agent",
			"Mozilla/5.0 (Nintendo 3DS; Like Gecko)");
		httpcAddRequestHeaderField(&ctx, "Accept", "text/html,application/xhtml+xml");
		httpcAddRequestHeaderField(&ctx, "Accept-Language", "fr,en;q=0.8");
		httpcAddRequestHeaderField(&ctx, "Connection", "close");

		r = httpcBeginRequest(&ctx);
		if (R_FAILED(r))
		{
			httpcCloseContext(&ctx);
			error = "httpcBeginRequest 0x" + num((long)(unsigned)r);
			return false;
		}

		u32 code = 0;
		httpcGetResponseStatusCode(&ctx, &code);
		status = (long)code;

		char ct[256] = { 0 };
		httpcGetResponseHeader(&ctx, "Content-Type", ct, sizeof(ct));
		contentType = ct;

		if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308)
		{
			char loc[2048] = { 0 };
			if (R_SUCCEEDED(httpcGetResponseHeader(&ctx, "Location", loc, sizeof(loc))) && loc[0])
			{
				httpcCloseContext(&ctx);
				if (strncmp(loc, "http", 4) == 0) current = loc;
				else
				{
					size_t s = current.find("://");
					size_t h = current.find('/', s + 3);
					std::string base = (h == std::string::npos) ? current : current.substr(0, h);
					current = base + (loc[0] == '/' ? "" : "/") + loc;
				}
				continue;
			}
		}

		char buf[16384];
		u32 read = 0;
		for (;;)
		{
			Result r2 = httpcDownloadData(&ctx, (u8*)buf, sizeof(buf), &read);
			if (read > 0)
			{
				if (body.size() >= kMaxSaved) break;
				size_t room = kMaxSaved - body.size();
				body.append(buf, read < room ? read : room);
			}
			if (r2 == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) continue;
			break;
		}
		httpcCloseContext(&ctx);
		return code < 400;
	}
	error = "trop de redirections";
	return false;
}

//--------------------------------------------------------------------------
int main()
{
	gfxInitDefault();
	fsInit();
	remove("sdmc:/netprobe.txt");
	// Nettoie les pages precedentes.
	for (int i = 1; i <= 9; i++)
	{
		std::string p = "sdmc:/page_" + num(i) + ".html";
		remove(p.c_str());
	}
	note("--- sonde reseau ---");

	Result socR = socInit((u32*)memalign(0x1000, 0x100000), 0x100000);
	note("socInit 0x" + num((long)(unsigned)socR));
	note("httpcInit 0x" + num((long)(unsigned)httpcInit(0)));

	// Pages reelles a enregistrer pour mettre au point le rendu.
	struct Target { const char* url; const char* label; };
	Target targets[] = {
		{ "https://lite.duckduckgo.com/lite/?q=nintendo+3ds", "DuckDuckGo Lite recherche" },
		{ "https://fr.m.wikipedia.org/wiki/Nintendo_3DS",     "Wikipedia FR" },
		{ "https://example.com/",                             "example.com" },
		{ "https://www.gamebrew.org/wiki/List_of_3DS_homebrew_applications",
		                                                      "Gamebrew liste" },
	};

	int saved = 0;
	for (const auto& t : targets)
	{
		std::string body, ct, err;
		long status = 0;
		const bool ok = fetch(t.url, body, status, ct, err);
		note(std::string(t.label) + " : statut=" + num(status) +
			" taille=" + num((long)body.size()) +
			" ct=" + (ct.empty() ? "(vide)" : ct) +
			" " + (ok ? "OK" : err));

		if (ok && body.size() > 200)
		{
			saved++;
			std::string path = "sdmc:/page_" + num(saved) + ".html";
			FILE* f = fopen(path.c_str(), "wb");
			if (f)
			{
				fwrite(body.data(), 1, body.size(), f);
				fclose(f);
				note("  -> " + path);
			}
		}
	}

	note("--- fin ---");
	if (g_log) fclose(g_log);

	httpcExit();
	socExit();
	fsExit();
	gfxExit();
	return 0;
}
