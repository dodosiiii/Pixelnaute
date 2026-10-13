#pragma once
//=============================================================================
// downloads.hpp - Sauvegarde des fichiers telecharges sur la carte SD
//=============================================================================
#include "../common.hpp"
#include "../storage/storage.hpp"
#include <string>

// Le contenu (body) est-il traitable comme une page a afficher ?
bool isHtmlContent(const std::string& contentType, const std::string& body);

// Sauvegarde le body sous un nom de fichier unique dans sdmc:/N3DSWeb/downloads.
// outPath : chemin complet cree en cas de succes.
// Retourne false si l'ecriture a echoue (disque plein, dossier absent...).
bool downloadsSave(const std::string& body, const std::string& filename,
	std::string& outPath);