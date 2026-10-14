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
	std::string userAgent = "Pixelnaute/" NW_VERSION_STRING " (Nintendo 3DS; homebrew browser)";
	std::string accept = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.5";
	size_t maxBytes = NW_MAX_HTML_BYTES; // 0 = taille illimitee (downloads)
	int timeoutMs = 30000;
	bool verifyTls = true;  // verifier avant le repli TLS automatique
	bool allowTlsBypass = true; // repli sans verification si la validation echoue
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