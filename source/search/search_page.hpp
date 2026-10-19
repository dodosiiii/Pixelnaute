#pragma once
//=============================================================================
// search_page.hpp - Reecriture des pages de resultats de recherche
//
// Les moteurs "sans JavaScript" (DuckDuckGo Lite, etc.) renvoient leurs
// resultats dans des TABLEAUX imbriques. Le moteur de mise en page du
// navigateur ne gere les tableaux que de facon tres sommaire : le resultat est
// un mur de texte ou l'on ne distingue ni les titres ni les liens.
//
// Ce module detecte une page de resultats et la reecrit en HTML simple, adapte
// a un ecran de telephone : un bloc par resultat, avec le titre, l'adresse et
// le resume. La page obtenue est ensuite mise en page par le moteur habituel,
// qui la rend correctement puisqu'il n'y a plus de tableau.
//
// Aucune analyse JavaScript n'est faite : on se contente de lire le HTML, qui
// contient deja tout le necessaire sur ces moteurs.
//=============================================================================
#include <string>

// Vrai si le document ressemble a une page de resultats reconnue.
bool searchPageDetected(const std::string& html);

// Vrai uniquement pour une page contenant des marqueurs explicites de defi.
bool searchPageIsVerificationChallenge(const std::string& html);

// Reecrit la page de resultats. Renvoie false si la page n'est pas reconnue,
// auquel cas l'appelant doit utiliser le HTML d'origine.
bool searchPageRewrite(const std::string& html, std::string& out,
	const std::string& responseUrl = std::string());

// Nombre de resultats extraits par le dernier appel reussi (diagnostic).
int searchPageLastResultCount();

// Tests unitaires (compilables sur PC).
int searchPageSelfTest();
