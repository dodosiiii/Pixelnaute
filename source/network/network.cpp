#include "network.hpp"
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <curl/curl.h>

static uint64_t downloadUrlKey(const std::string& url)
{
	uint64_t hash = 1469598103934665603ULL;
	for (unsigned char c : url) { hash ^= c; hash *= 1099511628211ULL; }
	return hash;
}


bool NetworkManager::begin()
{
	if (!httpInit_)
		httpInit_ = httpServiceInit();

	if (!socInit_)
	{
		socMem_ = (u32*)memalign(0x1000, 0x100000);
		if (socMem_ && R_SUCCEEDED(socInit((u32*)socMem_, 0x100000)))
			socInit_ = true;
	}

	if (socInit_ && !curlInit_)
		curlInit_ = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;

	return socInit_ && curlInit_;
}

void NetworkManager::end()
{
	cancel();
	for (;;)
	{
		LightLock_Lock(&lock_);
		const bool running = workerRunning_;
		LightLock_Unlock(&lock_);
		if (!running) break;
		svcSleepThread(10000000LL);
	}
	if (httpInit_) { httpServiceExit(); httpInit_ = false; }
	if (socInit_)
	{
		if (curlInit_)
		{
			curl_global_cleanup();
			curlInit_ = false;
		}
		socExit();
		socInit_ = false;
	}
	if (socMem_)
	{
		free(socMem_);
		socMem_ = nullptr;
	}
}


bool NetworkManager::busy() const
{
	LightLock_Lock(&lock_);
	const bool result = state_ == NetState::Connecting || state_ == NetState::Receiving;
	LightLock_Unlock(&lock_);
	return result;
}

NetState NetworkManager::state() const
{
	LightLock_Lock(&lock_);
	const NetState result = state_;
	LightLock_Unlock(&lock_);
	return result;
}

float NetworkManager::progress() const
{
	return progress_.load(std::memory_order_relaxed);
}

size_t NetworkManager::bytesReceived() const
{
	return bytes_.load(std::memory_order_relaxed);
}

//--------------------------------------------------------------------------
bool NetworkManager::start(const std::string& url, size_t maxBytes, bool verifyTls,
	const std::string& method, const std::string& postData,
	NetRequestType requestType, int timeoutMs, const std::string& referer,
	const std::string& csrfToken, const std::string& downloadPath,
	const std::string& ifNoneMatch, const std::string& ifModifiedSince)
{
	if (!socInit_ || !curlInit_)
	{
		if (!begin()) return false;
	}

	LightLock_Lock(&lock_);
	Request request;
	request.url = url;
	request.maxBytes = maxBytes;
	request.verifyTls = verifyTls;
	request.method = method;
	request.postData = postData;
	request.requestType = requestType;
	request.timeoutMs = timeoutMs;
	request.referer = referer;
	request.csrfToken = csrfToken;
	request.downloadPath = downloadPath;
	request.ifNoneMatch = ifNoneMatch;
	request.ifModifiedSince = ifModifiedSince;
	requestQueue_.push_back(std::move(request));
	if (workerRunning_ || resultReady_)
	{
		LightLock_Unlock(&lock_);
		return true;
	}
	workerRunning_ = true;
	cancelFlag_ = 0;
	progress_.store(0.0f, std::memory_order_relaxed);
	bytes_.store(0, std::memory_order_relaxed);
	state_ = NetState::Connecting;
	LightLock_Unlock(&lock_);

	Thread t = threadCreate(threadFunc, this, 256 * 1024, 0x30, 0, true);
	if (t)
		threadDetach(t);
	else
	{
		LightLock_Lock(&lock_);
		workerRunning_ = false;
		requestQueue_.clear();
		state_ = NetState::Idle;
		LightLock_Unlock(&lock_);
		return false;
	}
	return true;
}

void NetworkManager::cancel()
{
	LightLock_Lock(&lock_);
	requestQueue_.clear();
	cancelFlag_ = 1;
	if (!workerRunning_)
	{
		resultReady_ = false;
		result_ = NetResponse();
		state_ = NetState::Idle;
	}
	LightLock_Unlock(&lock_);
}

//--------------------------------------------------------------------------
bool NetworkManager::takeResult(NetResponse& out)
{
	LightLock_Lock(&lock_);
	if (!resultReady_)
	{
		LightLock_Unlock(&lock_);
		return false;
	}
	out = result_;
	resultReady_ = false;
	result_ = NetResponse();
	if (!requestQueue_.empty() && !workerRunning_)
	{
		workerRunning_ = true;
		cancelFlag_ = 0;
		progress_.store(0.0f, std::memory_order_relaxed);
		bytes_.store(0, std::memory_order_relaxed);
		state_ = NetState::Connecting;
		LightLock_Unlock(&lock_);
		Thread t = threadCreate(threadFunc, this, 256 * 1024, 0x30, 0, true);
		if (t) threadDetach(t);
		else { LightLock_Lock(&lock_); workerRunning_ = false; requestQueue_.clear(); state_ = NetState::Idle; LightLock_Unlock(&lock_); }
		return true;
	}
	state_ = NetState::Idle;
	LightLock_Unlock(&lock_);
	return true;
}

//--------------------------------------------------------------------------
void NetworkManager::threadFunc(void* arg)
{
	NetworkManager* nm = (NetworkManager*)arg;
	nm->worker();
}

void NetworkManager::worker()
{
	for (;;)
	{
		LightLock_Lock(&lock_);
		if (requestQueue_.empty())
		{
			workerRunning_ = false;
			if (!resultReady_) state_ = NetState::Idle;
			LightLock_Unlock(&lock_);
			return;
		}
		const Request request = requestQueue_.front();
		requestQueue_.pop_front();
		cancelFlag_ = 0;
		state_ = NetState::Receiving;
		LightLock_Unlock(&lock_);

		HttpOptions opt;
		opt.maxBytes = request.maxBytes;
		opt.verifyTls = request.verifyTls;
		opt.method = request.method;
		opt.postData = request.postData;
		opt.timeoutMs = request.timeoutMs;
		opt.referer = request.referer;
		opt.csrfToken = request.csrfToken;
		opt.ifNoneMatch = request.ifNoneMatch;
		opt.ifModifiedSince = request.ifModifiedSince;
		opt.convertTextEncoding = request.requestType != NetRequestType::Image;
		if (request.requestType == NetRequestType::Image)
			opt.accept = "image/jpeg,image/png,image/gif;q=0.8,*/*;q=0.2";

		std::string partialPath;
		FILE* downloadFile = nullptr;
		NetResponse resp;
		bool ok = false;
		if (request.requestType == NetRequestType::Download)
		{
			partialPath = request.downloadPath + ".part";
			const std::string keyPath = partialPath + ".urlkey";
			char storedKey[32] = {};
			FILE* keyFile = fopen(keyPath.c_str(), "rb");
			const size_t keyLength = keyFile ? fread(storedKey, 1, sizeof(storedKey) - 1, keyFile) : 0;
			if (keyFile) fclose(keyFile);
			char currentKey[24];
			snprintf(currentKey, sizeof(currentKey), "%016llx", (unsigned long long)downloadUrlKey(request.url));
			if (keyLength != 16 || strncmp(storedKey, currentKey, 16) != 0)
			{
				remove(partialPath.c_str());
				remove(keyPath.c_str());
			}
			struct stat partialInfo{};
			uint64_t partialBytes = stat(partialPath.c_str(), &partialInfo) == 0 &&
				partialInfo.st_size > 0 ? (uint64_t)partialInfo.st_size : 0;
			if (partialBytes >= request.maxBytes)
			{
				remove(partialPath.c_str());
				partialBytes = 0;
			}
			opt.resumeFrom = partialBytes;
			bytes_.store((size_t)partialBytes, std::memory_order_relaxed);
			downloadFile = fopen(partialPath.c_str(), partialBytes ? "ab" : "wb");
			if (downloadFile)
			{
				opt.outputFile = downloadFile;
				opt.convertTextEncoding = false;
				FILE* keyOut = fopen(keyPath.c_str(), "wb");
				if (keyOut)
				{
					fwrite(currentKey, 1, 16, keyOut);
					fclose(keyOut);
				}
			}
			ok = downloadFile && httpGet(request.url, opt, resp,
				&cancelFlag_, &progress_, &bytes_);
			// Certains serveurs ignorent Range ou ont deja supprime le fichier.
			// Dans ce cas, recommencer proprement une seule fois depuis zero.
			if (downloadFile && partialBytes > 0 &&
				((resp.status == 200) || resp.status == 416))
			{
				fclose(downloadFile);
				downloadFile = nullptr;
				downloadFile = fopen(partialPath.c_str(), "wb");
				opt.resumeFrom = 0;
				if (downloadFile)
				{
					opt.outputFile = downloadFile;
					bytes_.store(0, std::memory_order_relaxed);
					ok = httpGet(request.url, opt, resp,
						&cancelFlag_, &progress_, &bytes_);
				}
				else ok = false;
			}
			if (!downloadFile) resp.error = "Impossible de creer le fichier de telechargement.";
			if (downloadFile)
			{
				const bool written = ferror(downloadFile) == 0;
				const bool flushed = fflush(downloadFile) == 0;
				const bool closed = fclose(downloadFile) == 0;
				downloadFile = nullptr;
				if (!written || !flushed || !closed)
				{
					ok = false;
					resp.error = "Erreur pendant l'ecriture du fichier.";
				}
			}
			if (ok && resp.ok && !resp.tooBig && resp.status < 400 &&
				rename(partialPath.c_str(), request.downloadPath.c_str()) == 0)
			{
				resp.downloadPath = request.downloadPath;
				remove((partialPath + ".urlkey").c_str());
			}
			else
			{
				if (resp.tooBig || resp.status >= 400)
				{
					remove(partialPath.c_str());
					remove((partialPath + ".urlkey").c_str());
				}
				if (resp.tooBig) resp.error = "Fichier trop volumineux (limite 32 Mio).";
				else if (resp.cancelled) resp.error = "Telechargement interrompu; selectionnez de nouveau le lien pour reprendre.";
				else if (resp.status < 400 && !resp.error.empty())
					resp.error += " Le fichier partiel reste sur la SD; reessayez le lien pour reprendre.";
				resp.ok = false;
			}
		}
		else
		{
			ok = httpGet(request.url, opt, resp,
				&cancelFlag_, &progress_, &bytes_);
		}
		// httpGet initialise NetResponse; reinstaurer le type ensuite pour que
		// Browser distingue les pages, feuilles CSS et images.
		resp.requestType = request.requestType;

		LightLock_Lock(&lock_);
		if (cancelFlag_)
		{
			workerRunning_ = false;
			state_ = NetState::Idle;
			LightLock_Unlock(&lock_);
			return;
		}
		if (!ok && resp.cancelled)
		{
			workerRunning_ = false;
			state_ = NetState::Idle;
			LightLock_Unlock(&lock_);
			return;
		}
		result_ = resp;
		if (ok && resp.requestType != NetRequestType::Download)
			bytes_.store(resp.body.size(), std::memory_order_relaxed);
		resultReady_ = true;
		workerRunning_ = false;
		state_ = NetState::Done;
		LightLock_Unlock(&lock_);
		return;
	}
}
