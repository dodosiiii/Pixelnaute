#pragma once
//=============================================================================
// search.hpp - Moteurs de recherche accessibles sans API ni JavaScript
//
// Toutes les requetes sont de simples GET dont la reponse HTML est analysee
// par le moteur de rendu du navigateur. Aucune API, aucun JavaScript.
//=============================================================================
#include "../common.hpp"
#include <string>
#include <vector>

// Un moteur disponible dans les reglages.
struct SearchEngineInfo
{
	const char* key;      // identifiant persistant (settings.cfg)
	const char* name;     // nom affiche
	const char* host;     // domaine principal (affichage / detection)
	bool        mobile;   // variante allegee, recommandee sur New 3DS
};

// Construit l'URL du moteur pour une requete (query brute).
// engine : cle de moteur ("duckduckgo-lite" par defaut).
// Si la cle est inconnue, le moteur par defaut est utilise.
std::string searchUrl(const std::string& query, const std::string& engine);

// Liste des moteurs proposes dans les reglages.
const std::vector<SearchEngineInfo>& searchEngines();

// Vrai si la cle correspond a un moteur connu.
bool searchEngineKnown(const std::string& engine);

// Cle du moteur par defaut, si le reglage est vide ou inconnu.
const char* defaultSearchEngine();

// Nom affichable d'un moteur ("DuckDuckGo Lite").
const char* searchEngineDisplayName(const std::string& engine);

// Etiquette courte pour un bouton de la page d'accueil ("DuckDuckGo").
const char* searchEngineShortName(const std::string& engine);

// Fait passer au moteur suivant de la liste (reglages).
const char* searchEngineNext(const std::string& engine);

// Vrai si la saisie ressemble a une adresse (domaine, IP) et non a une
// recherche. Utilise pour choisir entre naviguer et chercher.
bool looksLikeAddress(const std::string& input);

// Transforme une saisie de la barre d'adresse en URL a charger :
// adresse complete, domaine nu, ou recherche sur le moteur choisi.
std::string resolveInput(const std::string& input, const std::string& engine);

// Tests unitaires (compilables sur PC).
int searchSelfTest();
