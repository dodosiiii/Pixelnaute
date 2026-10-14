#include "image_store.hpp"
#include "../network/http.hpp"
#include "stb_image.h"

#include <string.h>
#include <limits.h>
#include "../diag/diag.hpp"

static const long long kMaxDecodedImagePixels = 500000LL;

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
		if (img.url == url && img.valid) return &img;
	return nullptr;
}

static std::string resolveUrl(const std::string& base, const std::string& ref)
{
	if (strStartsWith(ref, "data:")) return ""; // data URIs non gerees
	return urlJoin(base, ref);
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

	diagStage("image decode");
	stbi_uc* pixels = stbi_load_from_memory(bytes, (int)data.size(),
		&w, &h, &channels, 4);
	if (!pixels || w <= 0 || h <= 0)
	{
		lastError_ = std::string("decodage impossible (") +
			(stbi_failure_reason() ? stbi_failure_reason() : "?") + ")";
		if (pixels) stbi_image_free(pixels);
		return false;
	}

	diagStage("image downscale");
	downscaleTo565(pixels, w, h, 4, out);
	stbi_image_free(pixels);
	return true;
}

//--------------------------------------------------------------------------
const StoredImage* ImageStore::fetch(const std::string& url,
	const std::string& referer)
{
	lastError_.clear();
	if (url.empty()) { lastError_ = "adresse vide"; return nullptr; }

	// Deja en cache ?
	const StoredImage* existing = find(url);
	if (existing) return existing;

	if ((int)images_.size() >= IMG_MAX_COUNT)
	{
		lastError_ = "cache plein";
		return nullptr;
	}

	const std::string full = resolveUrl(referer.empty() ? url : referer, url);
	if (full.empty()) { lastError_ = "adresse non resolue"; return nullptr; }
	if (!strStartsWith(full, "http://") && !strStartsWith(full, "https://"))
	{
		lastError_ = "schema non gere";
		return nullptr;
	}

	HttpOptions opt;
	opt.userAgent = "Mozilla/5.0 (Nintendo 3DS)";
	opt.accept = "image/*";
	opt.verifyTls = false;
	opt.timeoutMs = 6000;
	// Les images sont volontairement bornees : au-dela, ce n'est pas une
	// vignette mais un fond d'ecran, et cela n'apporte rien ici.
	opt.maxBytes = 512 * 1024;

	NetResponse resp;
	if (!httpGet(full, opt, resp, nullptr, nullptr) || !resp.ok)
	{
		lastError_ = resp.error.empty() ? "telechargement impossible" : resp.error;
		return nullptr;
	}
	if (resp.body.empty()) { lastError_ = "reponse vide"; return nullptr; }

	StoredImage img;
	img.url = url;
	if (!decode(resp.body, img))
		return nullptr;

	images_.push_back(std::move(img));
	return &images_.back();
}
