#pragma once
//=============================================================================
// settings.hpp - Parametres du navigateur (persistants sur SD)
//=============================================================================
#include "../common.hpp"
#include <string>

struct WebSettings
{
	bool    loadImages   = false;    // charger les images (desactive par defaut, en developpement)
	bool    blockTrackers = true;
	bool    lightUrls = true;
	bool    darkPages = false;
	bool    largePage = false;    // mise en page 640 px reduite sur l'ecran 400 px
	bool    enableJS     = true;     // runtime QuickJS experimental (execution des pages non integree)
	bool    verifyTLS    = true;     // verification TLS activee par defaut
	int     textScalePercent = 100;
	int     uiTheme = 1;
	std::string language = "en";
	std::string searchEngine = "duckduckgo-lite";
	std::string homePage     = "";   // page d'accueil (vide = page N3DS Web)
	int     maxPageHeight = NW_MAX_PAGE_HEIGHT;
	int     maxHtmlBytes  = NW_MAX_HTML_BYTES;
};

// Charge depuis la SD. Retourne false si aucun fichier (defauts).
bool settingsLoad(WebSettings& s);
// Sauvegarde sur la SD.
bool settingsSave(const WebSettings& s);
