#pragma once
//=============================================================================
// storage.hpp - Acces SD : dossiers, fichiers cle/valeur, historique
//=============================================================================
#include "../common.hpp"
#include <string>
#include <vector>

// Chemins de base sur la carte SD.
#define NW_SD_ROOT    "sdmc:/N3DSWeb"
#define NW_SD_CACHE   "sdmc:/N3DSWeb/cache"
#define NW_SD_DOWN    "sdmc:/N3DSWeb/downloads"
#define NW_SD_SETTINGS "sdmc:/N3DSWeb/settings.cfg"
#define NW_SD_HISTORY "sdmc:/N3DSWeb/history.txt"
#define NW_SD_BOOKMARKS "sdmc:/N3DSWeb/bookmarks.txt"
#define NW_SD_COOKIES "sdmc:/N3DSWeb/cookies.txt"

//--------------------------------------------------------------------------
// Fichier cle=valeur (settings, exceptions...)
//--------------------------------------------------------------------------
class StorageKV
{
public:
	std::vector<std::pair<std::string, std::string>> entries;

	bool load(const std::string& path);                 // false si absent
	bool save(const std::string& path) const;

	std::string get(const std::string& key, const std::string& def = "") const;
	void set(const std::string& key, const std::string& value);
};

//--------------------------------------------------------------------------
// Historique (une ligne par entree : URL<TAB>titre<TAB>timestamp)
//--------------------------------------------------------------------------
class History
{
public:
	struct Entry { std::string url, title; };

	std::vector<Entry> entries;

	bool load();       // depuis la SD
	bool save() const;
	void add(const std::string& url, const std::string& title); // dedupe + cap
	void clear();
};

//--------------------------------------------------------------------------
// Favoris
//--------------------------------------------------------------------------
class Bookmarks
{
public:
	struct Entry { std::string url, title; };

	std::vector<Entry> entries;

	bool load();
	bool save() const;
	void add(const std::string& url, const std::string& title);
	bool remove(const std::string& url);
	void clear();
};

//--------------------------------------------------------------------------
// Initialisation : cree le dossier N3DSWeb + sous-dossiers.
//--------------------------------------------------------------------------
bool storageInit();

// Debut d'un nouveau nom de fichier unique dans /N3DSWeb/downloads.
std::string storageUniqueDownloadName(const std::string& filename);