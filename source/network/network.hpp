#pragma once
//=============================================================================
// network.hpp - NetworkManager : requetes asynchrones sur un thread dedie
//=============================================================================
#include "../common.hpp"
#include "http.hpp"
#include <3ds.h>
#include <atomic>
#include <deque>
#include <string>

enum class NetState
{
	Idle,          // aucun transfert
	Connecting,    // resolution + connexion
	Receiving,     // telechargement en cours
	Done,          // resultat pret (a consommer)
};

class NetworkManager
{
public:
	bool begin();          // initservices (httpc)
	void end();

	// Lance une requete GET ou POST (http ou https) en arriere-plan.
	// maxBytes : 0 = illimite (downloads), sinon limite de la memoire page.
	bool start(const std::string& url, size_t maxBytes, bool verifyTls,
	           const std::string& method = "GET", const std::string& postData = "",
	           NetRequestType requestType = NetRequestType::Page, int timeoutMs = 30000,
	           const std::string& referer = "", const std::string& csrfToken = "",
	           const std::string& downloadPath = "",
	           const std::string& ifNoneMatch = "",
	           const std::string& ifModifiedSince = "");

	// Annule le transfert en cours.
	void cancel();

	bool       busy() const;
	NetState   state() const;
	float      progress() const;           // 0..1
	size_t     bytesReceived() const;

	// Consomme le resultat (disponible quand state() == Resolving).
	// Appeler une seule fois apres l'avoir observe (marque Idle).
	bool takeResult(NetResponse& out);

private:
	mutable LightLock lock_ = {};
	std::atomic<int> cancelFlag_{ 0 };
	NetState state_ = NetState::Idle;
	std::atomic<float> progress_{ 0.0f };
	std::atomic<size_t> bytes_{ 0 };
	NetState lastState_ = NetState::Idle;
	NetResponse result_{};
	bool resultReady_ = false;
	struct Request
	{
		std::string url;
		size_t maxBytes = 0;
		bool verifyTls = true;
		std::string method = "GET";
		std::string postData;
		NetRequestType requestType = NetRequestType::Page;
		int timeoutMs = 30000;
		std::string referer;
		std::string csrfToken;
		std::string downloadPath;
		std::string ifNoneMatch;
		std::string ifModifiedSince;
	};
	std::deque<Request> requestQueue_;
	bool workerRunning_ = false;

	static void threadFunc(void* arg);
	void worker();

	// Services init par begin()/end()
	bool httpInit_ = false;
	bool socInit_ = false;
	bool curlInit_ = false;
	void* socMem_ = nullptr;
};
