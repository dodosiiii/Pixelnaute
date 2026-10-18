#include "updater.hpp"
#include "../network/http.hpp"
#include <stdio.h>

namespace
{
	const char* kLatestReleaseUrl =
		"https://api.github.com/repos/dodosiiii/Pixelnaute/releases/latest";
	const char* kAssetPrefix =
		"https://github.com/dodosiiii/Pixelnaute/releases/download/";
	const size_t kMaxReleaseJsonBytes = 256 * 1024;
	const size_t kMaxUpdateBytes = 16 * 1024 * 1024;

	static void skipWhitespace(const std::string& text, size_t& pos)
	{
		while (pos < text.size() &&
			(text[pos] == ' ' || text[pos] == '\t' ||
			 text[pos] == '\r' || text[pos] == '\n'))
			++pos;
	}

	static bool parseJsonString(const std::string& json, size_t& pos,
		std::string& value)
	{
		if (pos >= json.size() || json[pos] != '"') return false;
		++pos;
		value.clear();
		while (pos < json.size())
		{
			const char c = json[pos++];
			if (c == '"') return true;
			if (c != '\\')
			{
				value += c;
				continue;
			}
			if (pos >= json.size()) return false;
			const char escaped = json[pos++];
			switch (escaped)
			{
				case '"': value += '"'; break;
				case '\\': value += '\\'; break;
				case '/': value += '/'; break;
				case 'b': value += '\b'; break;
				case 'f': value += '\f'; break;
				case 'n': value += '\n'; break;
				case 'r': value += '\r'; break;
				case 't': value += '\t'; break;
				default: return false;
			}
		}
		return false;
	}

	static bool jsonStringField(const std::string& json,
		const std::string& key, size_t from, std::string& value, size_t* end)
	{
		const std::string needle = "\"" + key + "\"";
		size_t pos = from;
		while ((pos = json.find(needle, pos)) != std::string::npos)
		{
			size_t cursor = pos + needle.size();
			skipWhitespace(json, cursor);
			if (cursor >= json.size() || json[cursor] != ':')
			{
				pos += needle.size();
				continue;
			}
			++cursor;
			skipWhitespace(json, cursor);
			if (!parseJsonString(json, cursor, value)) return false;
			if (end) *end = cursor;
			return true;
		}
		return false;
	}

	struct Version
	{
		unsigned major;
		unsigned minor;
		unsigned patch;
	};

	static bool parseVersion(const std::string& text, Version& version)
	{
		version.major = 0;
		version.minor = 0;
		version.patch = 0;
		size_t pos = 0;
		while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
			text[pos] == '\r' || text[pos] == '\n'))
			++pos;
		if (pos < text.size() && (text[pos] == 'v' || text[pos] == 'V'))
			++pos;

		unsigned* parts[] = { &version.major, &version.minor, &version.patch };
		int count = 0;
		while (count < 3 && pos < text.size())
		{
			const size_t start = pos;
			unsigned value = 0;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
			{
				const unsigned digit = (unsigned)(text[pos] - '0');
				if (value > (9999 - digit) / 10) return false;
				value = value * 10 + digit;
				++pos;
			}
			if (pos == start) break;
			*parts[count++] = value;
			if (pos < text.size() && text[pos] == '.')
			{
				++pos;
			}
			else
			{
				break;
			}
		}
		return count >= 2;
	}

	static bool versionIsNewer(const std::string& tag)
	{
		Version latest{}, current{};
		if (!parseVersion(tag, latest) ||
			!parseVersion(NW_VERSION_STRING, current))
			return false;
		if (latest.major != current.major) return latest.major > current.major;
		if (latest.minor != current.minor) return latest.minor > current.minor;
		return latest.patch > current.patch;
	}

	static bool is3dsxPath(const std::string& path)
	{
		if (path.size() < 5) return false;
		const size_t ext = path.size() - 5;
		return toLower(path.substr(ext)) == ".3dsx";
	}

	static bool getAssetUrl(const std::string& json, std::string& url)
	{
		size_t pos = 0;
		std::string candidate;
		while (jsonStringField(json, "browser_download_url", pos, candidate, &pos))
		{
			if (strStartsWith(candidate, kAssetPrefix) &&
				candidate.size() >= 5 &&
				candidate.compare(candidate.size() - 5, 5, ".3dsx") == 0)
			{
				url = candidate;
				return true;
			}
		}
		return false;
	}

	static bool writeFile(const std::string& path, const std::string& contents)
	{
		FILE* file = fopen(path.c_str(), "wb");
		if (!file) return false;
		const bool written = fwrite(contents.data(), 1, contents.size(), file) ==
			contents.size();
		const bool flushed = fflush(file) == 0;
		const bool closed = fclose(file) == 0;
		return written && flushed && closed;
	}
}

void ReleaseUpdater::configure(const char* selfPath)
{
	LightLock_Lock(&lock_);
	selfPath_ = selfPath ? selfPath : "";
	LightLock_Unlock(&lock_);
}

bool ReleaseUpdater::startCheck()
{
	LightLock_Lock(&lock_);
	if (workerRunning_)
	{
		LightLock_Unlock(&lock_);
		return false;
	}
	if (!is3dsxPath(selfPath_))
	{
		state_ = UpdateState::Failed;
		message_ = "Lancez la version .3dsx pour la mettre a jour.";
		LightLock_Unlock(&lock_);
		return false;
	}

	cancelFlag_ = 0;
	progress_ = 0.0f;
	state_ = UpdateState::Checking;
	message_ = "Recherche d'une mise a jour...";
	workerRunning_ = true;
	LightLock_Unlock(&lock_);

	Thread thread = threadCreate(threadFunc, this, 256 * 1024, 0x30, 0, true);
	if (!thread)
	{
		LightLock_Lock(&lock_);
		workerRunning_ = false;
		state_ = UpdateState::Failed;
		message_ = "Impossible de demarrer la verification.";
		LightLock_Unlock(&lock_);
		return false;
	}
	threadDetach(thread);
	return true;
}

void ReleaseUpdater::shutdown()
{
	LightLock_Lock(&lock_);
	cancelFlag_ = 1;
	LightLock_Unlock(&lock_);
	for (;;)
	{
		LightLock_Lock(&lock_);
		const bool running = workerRunning_;
		LightLock_Unlock(&lock_);
		if (!running) break;
		svcSleepThread(10000000LL);
	}
}

UpdateState ReleaseUpdater::state() const
{
	LightLock_Lock(&lock_);
	const UpdateState result = state_;
	LightLock_Unlock(&lock_);
	return result;
}

float ReleaseUpdater::progress() const
{
	LightLock_Lock(&lock_);
	const float result = state_ == UpdateState::Downloading
		? transferProgress_.load(std::memory_order_relaxed) : progress_;
	LightLock_Unlock(&lock_);
	return result;
}

std::string ReleaseUpdater::message() const
{
	LightLock_Lock(&lock_);
	const std::string result = message_;
	LightLock_Unlock(&lock_);
	return result;
}

bool ReleaseUpdater::active() const
{
	LightLock_Lock(&lock_);
	const bool result = workerRunning_;
	LightLock_Unlock(&lock_);
	return result;
}

void ReleaseUpdater::setStatus(UpdateState state, float progress,
	const std::string& message)
{
	LightLock_Lock(&lock_);
	state_ = state;
	progress_ = fclamp(progress, 0.0f, 1.0f);
	message_ = message;
	LightLock_Unlock(&lock_);
}

void ReleaseUpdater::threadFunc(void* arg)
{
	((ReleaseUpdater*)arg)->worker();
}

void ReleaseUpdater::worker()
{
	HttpOptions options;
	options.userAgent = "Pixelnaute/" NW_VERSION_STRING " (Nintendo 3DS)";
	options.accept = "application/vnd.github+json";
	options.maxBytes = kMaxReleaseJsonBytes;
	options.verifyTls = true;
	options.allowTlsBypass = false;

	std::atomic<float> requestProgress{ 0.0f };
	NetResponse release;
	if (!httpGet(kLatestReleaseUrl, options, release, &cancelFlag_,
			&requestProgress))
	{
		setStatus(UpdateState::Failed, 0.0f,
			release.error.empty() ? "Echec de la verification GitHub." : release.error);
		goto finished;
	}
	if (!release.ok)
	{
		setStatus(UpdateState::Failed, 0.0f, "Reponse GitHub invalide.");
		goto finished;
	}

	{
		std::string tag;
		Version parsedTag{};
		if (!jsonStringField(release.body, "tag_name", 0, tag, nullptr) ||
			!parseVersion(tag, parsedTag))
		{
			setStatus(UpdateState::Failed, 0.0f, "Version GitHub invalide.");
			goto finished;
		}
		if (!versionIsNewer(tag))
		{
			setStatus(UpdateState::UpToDate, 1.0f, "Version a jour.");
			goto finished;
		}

		std::string assetUrl;
		if (!getAssetUrl(release.body, assetUrl))
		{
			setStatus(UpdateState::Failed, 0.0f, "Fichier .3dsx absent de la release.");
			goto finished;
		}

		setStatus(UpdateState::Downloading, 0.0f, "Telechargement de " + tag + "...");
		options.accept = "*/*";
		options.maxBytes = kMaxUpdateBytes;
		NetResponse asset;
		transferProgress_.store(0.0f, std::memory_order_relaxed);
		if (!httpGet(assetUrl, options, asset, &cancelFlag_, &transferProgress_))
		{
			setStatus(UpdateState::Failed, 0.0f,
				asset.error.empty() ? "Echec du telechargement." : asset.error);
			goto finished;
		}
		if (!asset.ok || asset.body.size() < 100000 ||
			asset.body.compare(0, 4, "3DSX") != 0)
		{
			setStatus(UpdateState::Failed, 0.0f, "Le fichier telecharge est invalide.");
			goto finished;
		}
		{
			const std::string temporary = selfPath_ + ".update";
			const std::string backup = selfPath_ + ".bak";
			if (!writeFile(temporary, asset.body))
			{
				remove(temporary.c_str());
				setStatus(UpdateState::Failed, 0.0f, "Impossible d'ecrire la mise a jour.");
				goto finished;
			}

			remove(backup.c_str());
			if (rename(selfPath_.c_str(), backup.c_str()) != 0)
			{
				remove(temporary.c_str());
				setStatus(UpdateState::Failed, 0.0f,
					"Impossible de sauvegarder l'ancienne version.");
				goto finished;
			}
			if (rename(temporary.c_str(), selfPath_.c_str()) != 0)
			{
				const int restored = rename(backup.c_str(), selfPath_.c_str());
				remove(temporary.c_str());
				setStatus(UpdateState::Failed, 0.0f, restored == 0
					? "Installation echouee; ancienne version restauree."
					: "Installation echouee; ancienne version dans .bak.");
				goto finished;
			}
		}
		setStatus(UpdateState::Installed, 1.0f,
			"Mise a jour installee; fermez puis relancez l'application.");
	}

finished:
	LightLock_Lock(&lock_);
	workerRunning_ = false;
	LightLock_Unlock(&lock_);
}
