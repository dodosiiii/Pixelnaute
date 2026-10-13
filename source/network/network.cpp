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
		{
			socInit_ = true;
			curl_global_init(CURL_GLOBAL_DEFAULT);
		}
	}

	return httpInit_ || socInit_;
}

void NetworkManager::end()
{
	cancel();
	if (httpInit_) { httpServiceExit(); httpInit_ = false; }
	if (socInit_)
	{
		curl_global_cleanup();
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
	return state_ == NetState::Connecting || state_ == NetState::Receiving;
}

NetState NetworkManager::state() const
{
	return state_;
}

float NetworkManager::progress() const
{
	return progress_.load(std::memory_order_relaxed);
}

size_t NetworkManager::bytesReceived() const
{
	return bytes_.load(std::memory_order_relaxed);
}

const NetResponse* NetworkManager::pendingResult() const
{
	return resultReady_ ? &result_ : nullptr;
}

//--------------------------------------------------------------------------
void NetworkManager::start(const std::string& url, size_t maxBytes, bool allowTlsBypass)
{
	if (busy()) return;
	url_ = url;
	maxBytes_ = maxBytes;
	allowBypass_ = allowTlsBypass;
	cancelFlag_ = 0;
	resultReady_ = false;
	result_ = NetResponse();
	progress_.store(0.0f, std::memory_order_relaxed);
	bytes_.store(0, std::memory_order_relaxed);
	state_ = NetState::Connecting;

	Thread t = threadCreate(threadFunc, this, 256 * 1024, 0x30, 0, true);
	if (t)
		threadDetach(t);
	else
		state_ = NetState::Idle;
}

void NetworkManager::cancel()
{
	cancelFlag_ = 1;
}

//--------------------------------------------------------------------------
bool NetworkManager::takeResult(NetResponse& out)
{
	if (!resultReady_) return false;
	out = result_;
	resultReady_ = false;
	result_ = NetResponse();
	state_ = NetState::Idle;
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
	HttpOptions opt;
	opt.maxBytes = maxBytes_;
	opt.verifyTls = !allowBypass_;
	opt.allowTlsBypass = allowBypass_;
	state_ = NetState::Receiving;


	NetResponse resp;
	bool ok = httpGet(url_, opt, resp, &cancelFlag_, &progress_, &bytes_);
	if (!ok)
	{
		if (resp.cancelled)
		{
			state_ = NetState::Idle;
			return;
		}
		result_ = resp;
		resultReady_ = true;
		state_ = NetState::Done;
		return;
	}
	result_ = resp;
	bytes_.store(resp.body.size(), std::memory_order_relaxed);
	resultReady_ = true;
	state_ = NetState::Done;
}