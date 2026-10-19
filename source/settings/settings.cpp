#include "settings.hpp"
#include "../storage/storage.hpp"
#include "../search/search.hpp"

//--------------------------------------------------------------------------
// Format simple "cle=valeur" (StorageKV)
//--------------------------------------------------------------------------
static bool readBool(const std::string& v)
{
	return v == "1" || v == "true" || v == "oui" || v == "yes";
}

bool settingsLoad(WebSettings& s)
{
	StorageKV kv;
	if (!kv.load(NW_SD_SETTINGS))
	{
		WebSettings defaults;
		settingsSave(defaults); // cree le fichier avec les defauts
		return false;
	}

	s.loadImages   = readBool(kv.get("loadImages", "0"));
	s.enableJS     = readBool(kv.get("enableJS", "1"));
	// Migration unique : activer le runtime par defaut pour les installations
	// existantes. L'utilisateur pourra ensuite le desactiver dans Reglages.
	if (kv.get("jsDefaultEnabledV1", "") != "1")
	{
		s.enableJS = true;
		kv.set("enableJS", "1");
		kv.set("jsDefaultEnabledV1", "1");
		kv.save(NW_SD_SETTINGS);
	}
	s.verifyTLS    = readBool(kv.get("verifyTLS", "1"));
	s.searchEngine = kv.get("searchEngine", defaultSearchEngine());
	s.homePage     = kv.get("homePage", "");
	s.tlsExceptions.clear();
	for (int i = 0; i < 16; i++)
	{
		std::string v = kv.get("tlsExp" + itos(i), "");
		if (!v.empty()) s.tlsExceptions.push_back(v);
	}
	if (!searchEngineKnown(s.searchEngine))
	{
		s.searchEngine = defaultSearchEngine();
		settingsSave(s);
	}
	return true;
}

bool settingsSave(const WebSettings& s)
{
	StorageKV kv;
	kv.set("loadImages", s.loadImages ? "1" : "0");
	kv.set("enableJS", s.enableJS ? "1" : "0");
	kv.set("jsDefaultEnabledV1", "1");
	kv.set("verifyTLS", s.verifyTLS ? "1" : "0");
	kv.set("searchEngine", s.searchEngine);
	kv.set("homePage", s.homePage);
	for (size_t i = 0; i < s.tlsExceptions.size() && i < 16; i++)
		kv.set("tlsExp" + itos((long)i), s.tlsExceptions[i]);
	return kv.save(NW_SD_SETTINGS);
}
