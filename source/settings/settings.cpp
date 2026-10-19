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
	const int textScale = atoi(kv.get("textScalePercent", "100").c_str());
	s.textScalePercent = textScale >= 85 && textScale <= 115 ? textScale : 100;
	const int theme = atoi(kv.get("uiTheme", "0").c_str());
	s.uiTheme = theme >= 0 && theme <= 2 ? theme : 0;
	s.language = kv.get("language", "en");
	if (s.language != "en" && s.language != "fr" &&
		s.language != "it" && s.language != "es")
		s.language = "en";
	s.searchEngine = kv.get("searchEngine", defaultSearchEngine());
	s.homePage     = kv.get("homePage", "");
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
	kv.set("textScalePercent", itos((long)(s.textScalePercent >= 85 &&
		s.textScalePercent <= 115 ? s.textScalePercent : 100)));
	kv.set("uiTheme", itos((long)(s.uiTheme >= 0 && s.uiTheme <= 2 ? s.uiTheme : 0)));
	kv.set("language", s.language == "fr" || s.language == "it" ||
		s.language == "es" ? s.language : "en");
	kv.set("searchEngine", s.searchEngine);
	kv.set("homePage", s.homePage);
	return kv.save(NW_SD_SETTINGS);
}
