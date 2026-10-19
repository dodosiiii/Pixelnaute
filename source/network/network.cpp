#include "network.hpp"
#include <malloc.h>
#include <curl/curl.h>


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
	const std::string& csrfToken)
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
		opt.allowTlsBypass = request.verifyTls;
		opt.method = request.method;
		opt.postData = request.postData;
		opt.timeoutMs = request.timeoutMs;
		opt.referer = request.referer;
		opt.csrfToken = request.csrfToken;
		opt.convertTextEncoding = request.requestType != NetRequestType::Image;

		NetResponse resp;
		const bool ok = httpGet(request.url, opt, resp, &cancelFlag_, &progress_, &bytes_);
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
		if (ok) bytes_.store(resp.body.size(), std::memory_order_relaxed);
		resultReady_ = true;
		workerRunning_ = false;
		state_ = NetState::Done;
		LightLock_Unlock(&lock_);
		return;
	}
}
