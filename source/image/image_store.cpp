#include "image_store.hpp"
#include "stb_image.h"

#include <string.h>
#include <limits.h>
#include "../diag/diag.hpp"

// Charge STB en RGBA : borner le tampon decode a 320x240 (300 Ko max).
static const long long kMaxDecodedImagePixels = 320LL * 240LL;

ImageStore::ImageStore()
{
	images_.reserve(IMG_MAX_COUNT);
}

//--------------------------------------------------------------------------
void ImageStore::clear()
{
	images_.clear();
	lastError_.clear();
}

size_t ImageStore::bytesUsed() const
{
	size_t total = 0;
	for (const auto& img : images_)
		total += img.pixels.size() * sizeof(u16);
	return total;
}

const StoredImage* ImageStore::find(const std::string& url) const
{
	for (const auto& img : images_)
		if (img.url == url && img.valid)
		{
			img.lastUse = ++useCounter_;
			return &img;
		}
	return nullptr;
}

bool ImageStore::contains(const std::string& url) const
{
	for (const auto& img : images_)
		if (img.url == url && img.valid) return true;
	return false;
}

void ImageStore::evictLeastRecentlyUsed()
{
	if (images_.empty()) return;
	auto oldest = images_.begin();
	for (auto it = images_.begin() + 1; it != images_.end(); ++it)
		if (it->lastUse < oldest->lastUse) oldest = it;
	images_.erase(oldest);
}

//--------------------------------------------------------------------------
// Reduit l'image (plus proche voisin, suffisant pour une vignette) et la
// convertit en RGB565.
//--------------------------------------------------------------------------
static void downscaleTo565(const stbi_uc* src, int sw, int sh, int channels,
	StoredImage& out)
{
	int dw = sw, dh = sh;
	if (dw > IMG_MAX_WIDTH)
	{
		dh = dh * IMG_MAX_WIDTH / dw;
		dw = IMG_MAX_WIDTH;
	}
	if (dh > IMG_MAX_HEIGHT)
	{
		dw = dw * IMG_MAX_HEIGHT / dh;
		dh = IMG_MAX_HEIGHT;
	}
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;

	out.width = dw;
	out.height = dh;
	out.pixels.assign((size_t)dw * (size_t)dh, 0);

	for (int y = 0; y < dh; y++)
	{
		const int sy = y * sh / dh;
		for (int x = 0; x < dw; x++)
		{
			const int sx = x * sw / dw;
			const stbi_uc* p = src + ((size_t)sy * sw + sx) * channels;
			u8 r = p[0], g = (channels > 1 ? p[1] : p[0]), b = (channels > 2 ? p[2] : p[0]);
			const u8 a = (channels > 3) ? p[3] : 255;
			// Un fond transparent est rendu en gris tres sombre : la page est
			// sombre, un aplat noir se fond mieux qu'un aplat blanc.
			if (a < 16) { r = 32; g = 30; b = 32; }
			out.pixels[(size_t)y * dw + x] =
				(u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		}
	}
	out.valid = true;
}

//--------------------------------------------------------------------------
bool ImageStore::decode(const std::string& data, StoredImage& out)
{
	if (data.empty()) return false;
	if (data.size() > (size_t)INT_MAX)
	{
		lastError_ = "image trop volumineuse";
		return false;
	}

	int w = 0, h = 0, channels = 0;
	const stbi_uc* bytes = reinterpret_cast<const stbi_uc*>(data.data());
	if (!stbi_info_from_memory(bytes, (int)data.size(), &w, &h, &channels) ||
		w <= 0 || h <= 0)
	{
		lastError_ = "dimensions d'image illisibles";
		return false;
	}
	if ((long long)w * (long long)h > kMaxDecodedImagePixels)
	{
		lastError_ = "image trop grande pour le budget memoire de la New 3DS";
		return false;
	}
	// Le corps source reste vivant pendant le decodage et stb_image alloue un
	// tampon RGBA temporaire. Conserver 4 Mio au tas application pour le DOM,
	// le rendu et le reseau au lieu de laisser une image faire echouer newlib.
#ifdef __3DS__
	const size_t freeHeap = diagApplicationHeapFree();
	const size_t reserve = 4u * 1024u * 1024u;
	if (freeHeap < reserve + 64u * 1024u)
	{
		lastError_ = "memoire insuffisante pour decoder cette image";
		return false;
	}
	const size_t availablePixels = (freeHeap - reserve) / 4u;
	if ((size_t)w > availablePixels / (size_t)h)
	{
		lastError_ = "image ignoree pour conserver une marge memoire";
		return false;
	}
#endif

#ifdef __3DS__
	diagStage("image decode");
#endif
	stbi_uc* pixels = stbi_load_from_memory(bytes, (int)data.size(),
		&w, &h, &channels, 4);
	if (!pixels || w <= 0 || h <= 0)
	{
		lastError_ = std::string("decodage impossible (") +
			(stbi_failure_reason() ? stbi_failure_reason() : "?") + ")";
		if (pixels) stbi_image_free(pixels);
		return false;
	}

#ifdef __3DS__
	diagStage("image downscale");
#endif
	downscaleTo565(pixels, w, h, 4, out);
	stbi_image_free(pixels);
	return true;
}

//--------------------------------------------------------------------------
const StoredImage* ImageStore::storeDownloaded(const std::string& url,
	const std::string& data)
{
	lastError_.clear();
	if (url.empty()) { lastError_ = "adresse vide"; return nullptr; }

	// Deja en cache ?
	const StoredImage* existing = find(url);
	if (existing) return existing;

	if (data.empty()) { lastError_ = "reponse vide"; return nullptr; }

	StoredImage img;
	img.url = url;
	if (!decode(data, img))
		return nullptr;
	const size_t imageBytes = img.pixels.size() * sizeof(u16);
	if (imageBytes > IMG_CACHE_MAX_BYTES)
	{
		lastError_ = "image trop grande pour le cache";
		return nullptr;
	}
	while (!images_.empty() &&
		((int)images_.size() >= IMG_MAX_COUNT || bytesUsed() + imageBytes > IMG_CACHE_MAX_BYTES))
		evictLeastRecentlyUsed();
	img.lastUse = ++useCounter_;

	images_.push_back(std::move(img));
	return &images_.back();
}
