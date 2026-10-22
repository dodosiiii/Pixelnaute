#include "updater.hpp"
#include "../network/http.hpp"
#include <mbedtls/sha256.h>
#include <algorithm>
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

	static bool getAssetUrl(const std::string& json, const char* extension,
		std::string& url, std::string& digest)
	{
		size_t pos = 0;
		std::string candidate;
		size_t valueEnd = 0;
		while (jsonStringField(json, "browser_download_url", pos, candidate, &valueEnd))
		{
			const size_t extensionLength = strlen(extension);
			if (strStartsWith(candidate, kAssetPrefix) &&
				candidate.size() >= extensionLength &&
				candidate.compare(candidate.size() - extensionLength,
					extensionLength, extension) == 0)
			{
				url = candidate;
				const size_t objectStart = json.rfind('{', valueEnd);
				const size_t objectEnd = json.find('}', valueEnd);
				if (objectStart == std::string::npos || objectEnd == std::string::npos) return false;
				const std::string object = json.substr(objectStart, objectEnd - objectStart + 1);
				return jsonStringField(object, "digest", 0, digest, nullptr) &&
					strStartsWith(digest, "sha256:") && digest.size() == 71;
			}
			pos = valueEnd;
		}
		return false;
	}

	static bool sha256File(const std::string& path, std::string& hex)
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f) return false;
		mbedtls_sha256_context ctx;
		mbedtls_sha256_init(&ctx);
		bool ok = mbedtls_sha256_starts_ret(&ctx, 0) == 0;
		unsigned char buffer[8192], digest[32];
		while (ok)
		{
			const size_t n = fread(buffer, 1, sizeof(buffer), f);
			if (n && mbedtls_sha256_update_ret(&ctx, buffer, n) != 0) ok = false;
			if (n < sizeof(buffer)) { if (ferror(f)) ok = false; break; }
		}
		if (ok) ok = mbedtls_sha256_finish_ret(&ctx, digest) == 0;
		mbedtls_sha256_free(&ctx);
		fclose(f);
		if (!ok) return false;
		static const char digits[] = "0123456789abcdef";
		hex = "sha256:";
		for (unsigned char byte : digest) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
		return true;
	}

}

void ReleaseUpdater::configure(const char* selfPath, bool ciaTitle)
{
	LightLock_Lock(&lock_);
	selfPath_ = selfPath ? selfPath : "";
	ciaMode_ = ciaTitle;
	LightLock_Unlock(&lock_);
}

bool ReleaseUpdater::startCheck()
{
	LightLock_Lock(&lock_);
	if (workerRunning_ || state_ == UpdateState::AwaitingConfirmation)
	{
		LightLock_Unlock(&lock_);
		return false;
	}
	if (!ciaMode_)
	{
		state_ = UpdateState::Failed;
		message_ = "La mise a jour integree est reservee a la version CIA.";
		LightLock_Unlock(&lock_);
		return false;
	}

	cancelFlag_ = 0;
	progress_ = 0.0f;
	pendingTag_.clear();
	approvedTag_.clear();
	releaseNotes_.clear();
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

bool ReleaseUpdater::approveInstall()
{
	LightLock_Lock(&lock_);
	if (workerRunning_ || state_ != UpdateState::AwaitingConfirmation || pendingTag_.empty())
	{
		LightLock_Unlock(&lock_);
		return false;
	}
	approvedTag_ = pendingTag_;
	cancelFlag_ = 0;
	progress_ = 0.0f;
	state_ = UpdateState::Checking;
	message_ = "Verification de la release avant telechargement...";
	workerRunning_ = true;
	LightLock_Unlock(&lock_);

	Thread thread = threadCreate(threadFunc, this, 256 * 1024, 0x30, 0, true);
	if (!thread)
	{
		LightLock_Lock(&lock_);
		workerRunning_ = false;
		state_ = UpdateState::AwaitingConfirmation;
		message_ = "Impossible de demarrer l'installation.";
		LightLock_Unlock(&lock_);
		return false;
	}
	threadDetach(thread);
	return true;
}

void ReleaseUpdater::cancelPendingInstall()
{
	LightLock_Lock(&lock_);
	if (!workerRunning_ && state_ == UpdateState::AwaitingConfirmation)
	{
		pendingTag_.clear();
		approvedTag_.clear();
		releaseNotes_.clear();
		state_ = UpdateState::Idle;
		message_ = "Installation ignoree.";
	}
	LightLock_Unlock(&lock_);
}

std::string ReleaseUpdater::ciaDownloadPath() const
{
	LightLock_Lock(&lock_);
	const std::string result = ciaDownloadPath_;
	LightLock_Unlock(&lock_);
	return result;
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

std::string ReleaseUpdater::releaseNotes() const
{
	LightLock_Lock(&lock_);
	const std::string result = releaseNotes_;
	LightLock_Unlock(&lock_);
	return result;
}

std::string ReleaseUpdater::pendingVersion() const
{
	LightLock_Lock(&lock_);
	const std::string result = pendingTag_;
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

bool ReleaseUpdater::restartRequired() const
{
	LightLock_Lock(&lock_);
	const bool result = ciaMode_ && state_ == UpdateState::Installed && !workerRunning_;
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
	std::string temporary;
	std::string backup;
	HttpOptions options;
	options.userAgent = "Pixelnaute/" NW_VERSION_STRING " (Nintendo 3DS)";
	options.accept = "application/vnd.github+json";
	options.maxBytes = kMaxReleaseJsonBytes;
	options.verifyTls = true;

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
		std::string expectedDigest;
		const bool ciaMode = ciaMode_;
		if (!getAssetUrl(release.body, ciaMode ? ".cia" : ".3dsx",
			assetUrl, expectedDigest))
		{
			setStatus(UpdateState::Failed, 0.0f,
				ciaMode ? "Fichier .cia ou empreinte SHA-256 absent de la release."
					: "Fichier .3dsx ou empreinte SHA-256 absent de la release.");
			goto finished;
		}
		std::string notes;
		jsonStringField(release.body, "body", 0, notes, nullptr);
		if (notes.size() > 2048) notes.resize(2048);
		bool approved = false;
		LightLock_Lock(&lock_);
		approved = approvedTag_ == tag;
		if (approved)
		{
			approvedTag_.clear();
			pendingTag_.clear();
			releaseNotes_.clear();
		}
		else
		{
			pendingTag_ = tag;
			releaseNotes_ = notes;
		}
		LightLock_Unlock(&lock_);
		if (!approved)
		{
			setStatus(UpdateState::AwaitingConfirmation, 0.0f,
				"Version " + tag + " disponible : lisez les notes avant installation.");
			goto finished;
		}

		setStatus(UpdateState::Downloading, 0.0f, "Telechargement de " + tag + "...");
		options.accept = "*/*";
		options.maxBytes = kMaxUpdateBytes;
		temporary = ciaMode ? "sdmc:/N3DSWeb/N3DSWeb-update.cia.part" : selfPath_ + ".update";
		backup = ciaMode ? "sdmc:/N3DSWeb/N3DSWeb-update.cia" : selfPath_ + ".bak";
		FILE* updateFile = fopen(temporary.c_str(), "wb");
		if (!updateFile)
		{
			setStatus(UpdateState::Failed, 0.0f, "Impossible de creer le fichier de mise a jour.");
			goto finished;
		}
		options.outputFile = updateFile;
		NetResponse asset;
		transferProgress_.store(0.0f, std::memory_order_relaxed);
		const bool transferred = httpGet(assetUrl, options, asset, &cancelFlag_, &transferProgress_);
		const bool flushed = fflush(updateFile) == 0;
		const bool closed = fclose(updateFile) == 0;
		options.outputFile = nullptr;
		if (!transferred || !flushed || !closed)
		{
			remove(temporary.c_str());
			setStatus(UpdateState::Failed, 0.0f,
				asset.error.empty() ? "Echec du telechargement." : asset.error);
			goto finished;
		}
		FILE* checkFile = fopen(temporary.c_str(), "rb");
		char magic[4] = {};
		bool validAsset = checkFile != nullptr;
		if (checkFile && !ciaMode)
			validAsset = fread(magic, 1, sizeof(magic), checkFile) == sizeof(magic) &&
				memcmp(magic, "3DSX", sizeof(magic)) == 0;
		long fileSize = -1;
		if (checkFile) { fseek(checkFile, 0, SEEK_END); fileSize = ftell(checkFile); fclose(checkFile); }
		std::string actualDigest;
		if (!asset.ok || fileSize < 100000 || !validAsset ||
			!sha256File(temporary, actualDigest) || actualDigest != expectedDigest)
		{
			remove(temporary.c_str());
			setStatus(UpdateState::Failed, 0.0f, "Fichier invalide ou empreinte SHA-256 incorrecte.");
			goto finished;
		}
		if (ciaMode)
		{
			remove(backup.c_str());
			if (rename(temporary.c_str(), backup.c_str()) != 0)
			{
				remove(temporary.c_str());
				setStatus(UpdateState::Failed, 0.0f,
					"CIA verifie, mais impossible de le sauvegarder sur la carte SD.");
				goto finished;
			}
			setStatus(UpdateState::Installing, 0.0f,
				"CIA verifie. Demarrage de l'installation...");
			std::string installError;
			bool installed = false;
			if (R_FAILED(amInit()))
			{
				installError = "Service am:net indisponible.";
			}
			else
			{
				FILE* ciaFile = fopen(backup.c_str(), "rb");
				if (!ciaFile)
					installError = "Impossible de relire le CIA telecharge.";
				else if (fseek(ciaFile, 0, SEEK_END) != 0)
				{
					fclose(ciaFile);
					ciaFile = nullptr;
					installError = "Impossible de mesurer le CIA.";
				}
				long ciaSize = ciaFile ? ftell(ciaFile) : -1;
				if (ciaFile && (ciaSize < 100000 || fseek(ciaFile, 0, SEEK_SET) != 0))
				{
					fclose(ciaFile);
					ciaFile = nullptr;
					installError = "La taille du CIA est invalide.";
				}
				Handle installHandle = 0;
				Result installResult = ciaFile
					? AM_StartCiaInstall(MEDIATYPE_SD, &installHandle) : -1;
				if (ciaFile && R_FAILED(installResult))
				{
					char detail[96];
					snprintf(detail, sizeof(detail), "Impossible de demarrer l'installation (0x%08lX).", (unsigned long)installResult);
					installError = detail;
				}
				if (ciaFile && R_SUCCEEDED(installResult))
				{
					uint64_t offset = 0;
					unsigned char buffer[32 * 1024];
					bool writeOk = true;
					while (writeOk && offset < (uint64_t)ciaSize)
					{
						if (cancelFlag_)
						{
							installError = "Installation annulee.";
						writeOk = false;
						break;
						}
						const size_t chunk = (size_t)std::min<uint64_t>(sizeof(buffer), (uint64_t)ciaSize - offset);
						if (fread(buffer, 1, chunk, ciaFile) != chunk)
						{
							installError = "Lecture incomplete du CIA.";
							writeOk = false;
							break;
						}
						u32 bytesWritten = 0;
						installResult = FSFILE_Write(installHandle, &bytesWritten,
							offset, buffer, (u32)chunk, FS_WRITE_FLUSH);
						if (R_FAILED(installResult) || bytesWritten != chunk)
						{
							char detail[96];
							snprintf(detail, sizeof(detail), "Ecriture CIA echouee (0x%08lX).", (unsigned long)installResult);
							installError = detail;
							writeOk = false;
							break;
						}
						offset += bytesWritten;
						const float ratio = (float)offset / (float)ciaSize;
						setStatus(UpdateState::Installing, ratio,
							"Installation du CIA " + itos((long)(ratio * 100.0f)) + "%...");
					}
					fclose(ciaFile);
					if (writeOk)
					{
						installResult = AM_FinishCiaInstall(installHandle);
						if (R_SUCCEEDED(installResult)) installed = true;
						else
						{
							char detail[96];
							snprintf(detail, sizeof(detail), "Finalisation CIA echouee (0x%08lX).", (unsigned long)installResult);
							installError = detail;
						}
					}
					if (!installed) AM_CancelCIAInstall(installHandle);
				}
				else if (ciaFile)
					fclose(ciaFile);
				amExit();
			}
			if (!installed)
			{
				setStatus(UpdateState::Failed, 0.0f, installError +
					" Le CIA verifie reste sur la carte SD.");
				goto finished;
			}
			remove(backup.c_str());
			setStatus(UpdateState::Installed, 1.0f,
				"Mise a jour installee. Redemarrage...");
			goto finished;
		}
		{
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
