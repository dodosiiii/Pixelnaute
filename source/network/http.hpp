#pragma once
//=============================================================================
// http.hpp - Client HTTP (via le service systeme httpc de la console)
//=============================================================================
#include "../common.hpp"
#include <atomic>
#include <string>

struct NetResponse
{
	bool ok = false;
	int status = 0;
	std::string url;         // URL finale apres redirections
	std::string body;
	std::string contentType;
	std::string error;       // message utilisateur (francais)
	bool sslError = false;
	bool tooBig = false;
	bool cancelled = false;
	bool sslBypassUsed = false;
};

struct HttpOptions
{
	std::string userAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 16_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.5 Mobile/15E148 Safari/604.1 Pixelnaute/0.2";
	std::string accept = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.5";
	size_t maxBytes = NW_MAX_HTML_BYTES; // 0 = taille illimitee (downloads)
	int timeoutMs = 30000;
	bool verifyTls = false;  // verification des certificats (faux par defaut pour autoriser Let's Encrypt / Web moderne)
	bool allowTlsBypass = false; // autorise le bypass apres echec (choix utilisateur)
};

// Effectue une requete GET synchrone avec redirections.
// Retourne false si le reseau n'est pas disponible (pas de connexion).
// 'cancelFlag' (non nul) : si pointe vers une valeur non nulle, la requete
// s'arrete proprement au prochain bloc.
bool httpGet(const std::string& url, const HttpOptions& opt, NetResponse& out,
	const volatile int* cancelFlag, std::atomic<float>* progressOut,
	std::atomic<size_t>* bytesOut = nullptr);

bool httpGetCurl(const std::string& url, const HttpOptions& opt, NetResponse& out,
	const volatile int* cancelFlag, std::atomic<float>* progressOut,
	std::atomic<size_t>* bytesOut = nullptr);


// Initialise / finalise le service HTTP (a appeler au demarrage / a la sortie).
bool httpServiceInit();
void httpServiceExit();