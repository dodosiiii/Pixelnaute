#pragma once

#include "../common.hpp"
#include <3ds.h>
#include <atomic>
#include <string>

enum class UpdateState
{
	Idle,
	Checking,
	Downloading,
	Installing,
	Downloaded,
	Installed,
	AwaitingConfirmation,
	UpToDate,
	Failed,
};

class ReleaseUpdater
{
public:
	void configure(const char* selfPath, bool ciaTitle = false);
	bool startCheck();
	bool approveInstall();
	void cancelPendingInstall();
	void shutdown();

	UpdateState state() const;
	float progress() const;
	std::string message() const;
	std::string releaseNotes() const;
	std::string pendingVersion() const;
	bool active() const;
	bool restartRequired() const;
	bool ciaMode() const { return ciaMode_; }
	std::string ciaDownloadPath() const;

private:
	mutable LightLock lock_ = {};
	std::string selfPath_;
	std::string ciaDownloadPath_;
	bool ciaMode_ = false;
	std::string message_;
	std::string releaseNotes_;
	std::string pendingTag_;
	std::string approvedTag_;
	UpdateState state_ = UpdateState::Idle;
	float progress_ = 0.0f;
	std::atomic<float> transferProgress_{ 0.0f };
	std::atomic<int> cancelFlag_{ 0 };
	bool workerRunning_ = false;

	static void threadFunc(void* arg);
	void worker();
	void setStatus(UpdateState state, float progress, const std::string& message);
};
