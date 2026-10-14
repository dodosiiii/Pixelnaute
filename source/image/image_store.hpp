#pragma once
//=============================================================================
// image_store.hpp - Telechargement et decodage des images des pages
//
// Les images sont le principal element qui manquait a l'affichage : logos des
// sites, vignettes, illustrations. Sans elles, une page ressemble a un mur de
// texte.
//
// Le principe :
//   - l'adresse est resolue contre l'URL de la page ;
//   - l'image est telechargee (httpc), decodee par stb_image (PNG, JPEG, GIF,
//     WebP), reduite aux dimensions maximales, puis convertie en RGB565 ;
//   - elle est gardee dans un cache en RAM, borne en nombre et en memoire.
//
// Le cache est volontairement petit : la console a une memoire limitee et le
// rendu affiche au plus quelques vignettes par page.
//=============================================================================
#include "../common.hpp"
#include <string>
#include <vector>

// Dimensions maximales d'une image conservee. Au-dela elle est reduite, ce
// qui borne la memoire (96 x 72 pixels = 13,8 Ko en RGB565).
#define IMG_MAX_WIDTH  96
#define IMG_MAX_HEIGHT 72
// Nombre maximal d'images gardees.
#define IMG_MAX_COUNT  12

struct StoredImage
{
	std::string url;
	int width = 0;
	int height = 0;
	std::vector<u16> pixels;   // RGB565, width * height
	bool valid = false;
};

class ImageStore
{
public:
	ImageStore();
	void clear();

	// Image deja en cache, ou nullptr.
	const StoredImage* find(const std::string& url) const;

	// Recupere l'image (reseau + decodage) et la met en cache.
	// Renvoie nullptr en cas d'echec. `referer` sert a resoudre les adresses
	// relatives.
	const StoredImage* fetch(const std::string& url, const std::string& referer);

	int count() const { return (int)images_.size(); }
	size_t bytesUsed() const;

	// Statistiques du dernier echec, pour le diagnostic.
	const std::string& lastError() const { return lastError_; }

private:
	std::vector<StoredImage> images_;
	std::string lastError_;

	// Decode une reponse image et la convertit en RGB565 reduit.
	bool decode(const std::string& data, StoredImage& out);
};
