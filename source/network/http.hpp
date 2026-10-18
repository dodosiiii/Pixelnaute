#pragma once
//=============================================================================
// http.hpp - Client HTTP (via le service systeme httpc de la console)
//=============================================================================
#include "../common.hpp"
#include "encoding.hpp"
#include <atomic>
#include <string>

enum class NetRequestType : uint8_t
{
	Page,
	Stylesheet,
	Image,
};

struct NetResponse
{
	bool ok = false;
	NetRequestType requestType = NetRequestType::Page;
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

#define NW_DEFAULT_USER_AGENT "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1 Pixelnaute/" NW_VERSION_STRING

struct HttpOptions
{
	std::string userAgent = NW_DEFAULT_USER_AGENT;
	std::string accept = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.5";
	std::string referer;
	std::string method = "GET";
	std::string postData;
	size_t maxBytes = NW_MAX_HTML_BYTES; // 0 = taille illimitee (downloads)
	int timeoutMs = 30000;
	bool verifyTls = true;  // verifier avant le repli TLS automatique
	bool allowTlsBypass = true; // repli sans verification si la validation echoue
	bool convertTextEncoding = true; // desactiver pour les corps binaires (images)
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

