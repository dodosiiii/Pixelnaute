#pragma once
//=============================================================================
// network.hpp - NetworkManager : requetes asynchrones sur un thread dedie
//=============================================================================
#include "../common.hpp"
#include "http.hpp"
#include <3ds.h>
#include <atomic>
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

	// Lance une requete GET (http ou https) en arriere-plan.
	// maxBytes : 0 = illimite (downloads), sinon limite de la memoire page.
	void start(const std::string& url, size_t maxBytes, bool allowTlsBypass);

	// Annule le transfert en cours.
	void cancel();

	bool       busy() const;
	NetState   state() const;
	float      progress() const;           // 0..1
	size_t     bytesReceived() const;

	// Consomme le resultat (disponible quand state() == Resolving).
	// Appeler une seule fois apres l'avoir observe (marque Idle).
	bool takeResult(NetResponse& out);

	const NetResponse* pendingResult() const;

private:
	mutable LightLock lock_ = {};
	volatile int cancelFlag_ = 0;
	volatile bool running_ = false;
	volatile NetState state_ = NetState::Idle;
	std::atomic<float> progress_{ 0.0f };
	std::atomic<size_t> bytes_{ 0 };
	NetState lastState_ = NetState::Idle;
	NetResponse result_{};
	bool resultReady_ = false;
	std::string url_;
	size_t maxBytes_ = 0;
	bool allowBypass_ = false;
	bool threadDone_ = false;

	static void threadFunc(void* arg);
	void worker();

	// Services init par begin()/end()
	bool httpInit_ = false;
	bool socInit_ = false;
	void* socMem_ = nullptr;
};