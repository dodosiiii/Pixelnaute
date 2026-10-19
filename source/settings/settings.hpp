#pragma once
//=============================================================================
// settings.hpp - Parametres du navigateur (persistants sur SD)
//=============================================================================
#include "../common.hpp"
#include <string>

struct WebSettings
{
	bool    loadImages   = false;    // charger les images (desactive par defaut, en developpement)
	bool    enableJS     = true;     // runtime QuickJS experimental (execution des pages non integree)
	bool    verifyTLS    = true;     // verifier d'abord, puis repli TLS automatique si necessaire
	std::string searchEngine = "duckduckgo-lite";
	std::string homePage     = "";   // page d'accueil (vide = page N3DS Web)
	int     maxPageHeight = NW_MAX_PAGE_HEIGHT;
	int     maxHtmlBytes  = NW_MAX_HTML_BYTES;

	// Exceptions de certificat (domaines autorises "Continuer quand meme")
	std::vector<std::string> tlsExceptions;

	bool isTlsException(const std::string& host) const
	{
		for (const auto& h : tlsExceptions)
			if (h == host) return true;
		return false;
	}

	void addTlsException(const std::string& host)
	{
		for (const auto& h : tlsExceptions)
			if (h == host) return;
		tlsExceptions.push_back(host);
		if (tlsExceptions.size() > 16)
			tlsExceptions.erase(tlsExceptions.begin());
	}
};

// Charge depuis la SD. Retourne false si aucun fichier (defauts).
bool settingsLoad(WebSettings& s);
// Sauvegarde sur la SD.
bool settingsSave(const WebSettings& s);
