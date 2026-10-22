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
	Script,
	Fetch,
	Download,
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
	bool certError = false;
	bool tooBig = false;
	bool cancelled = false;
	std::string downloadPath;
	std::string etag;
	std::string lastModified;
	bool notModified = false;
};

#define NW_DEFAULT_USER_AGENT "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1 Pixelnaute/" NW_VERSION_STRING

struct HttpOptions
{
	std::string userAgent = NW_DEFAULT_USER_AGENT;
	std::string accept = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.5";
	std::string referer;
	std::string method = "GET";
	std::string postData;
	std::string csrfToken;
	size_t maxBytes = NW_MAX_HTML_BYTES; // 0 = taille illimitee (downloads)
	int timeoutMs = 30000;
	bool verifyTls = true; // false seulement si l'utilisateur le desactive dans les reglages
	bool convertTextEncoding = true; // desactiver pour les corps binaires (images)
	FILE* outputFile = nullptr; // optionnel : ecriture en flux du corps HTTP
	uint64_t resumeFrom = 0; // reprise d'un fichier deja ecrit sur la SD
	std::string ifNoneMatch;
	std::string ifModifiedSince;
};


// Effectue une requete GET synchrone avec redirections.
// Retourne false si le reseau n'est pas disponible (pas de connexion).
// 'cancelFlag' (non nul) : si pointe vers une valeur non nulle, la requete
// s'arrete proprement au prochain bloc.
bool httpGet(const std::string& url, const HttpOptions& opt, NetResponse& out,
	const std::atomic<int>* cancelFlag, std::atomic<float>* progressOut,
	std::atomic<size_t>* bytesOut = nullptr);

bool httpGetCurl(const std::string& url, const HttpOptions& opt, NetResponse& out,
	const std::atomic<int>* cancelFlag, std::atomic<float>* progressOut,
	std::atomic<size_t>* bytesOut = nullptr);

// Accès au cookie-jar partagé pour document.cookie.
std::string httpCookieHeaderForUrl(const std::string& url);
bool httpSetCookieForUrl(const std::string& url, const std::string& cookie);


// Initialise / finalise le service HTTP (a appeler au demarrage / a la sortie).
bool httpServiceInit();
void httpServiceExit();
