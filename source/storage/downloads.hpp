#pragma once
//=============================================================================
// downloads.hpp - Sauvegarde des fichiers telecharges sur la carte SD
//=============================================================================
#include "../common.hpp"
#include "../storage/storage.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct DownloadFileInfo
{
	std::string path;
	std::string name;
	std::string extension;
	std::string modified;
	uint64_t size = 0;
	uint64_t modifiedAt = 0;
};

// Le contenu (body) est-il traitable comme une page a afficher ?
bool isHtmlContent(const std::string& contentType, const std::string& body);

// Liste les fichiers du dossier de telechargement, du plus recent au plus ancien.
bool downloadsListFiles(std::vector<DownloadFileInfo>& files,
	std::string& error);

// Sauvegarde le body sous un nom de fichier unique dans sdmc:/N3DSWeb/downloads.
// outPath : chemin complet cree en cas de succes.
// Retourne false si l'ecriture a echoue (disque plein, dossier absent...).
bool downloadsSave(const std::string& body, const std::string& filename,
	std::string& outPath);