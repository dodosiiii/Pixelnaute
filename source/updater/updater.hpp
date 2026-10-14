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
	Installed,
	UpToDate,
	Failed,
};

class ReleaseUpdater
{
public:
	void configure(const char* selfPath);
	bool startCheck();
	void shutdown();

	UpdateState state() const;
	float progress() const;
	std::string message() const;
	bool active() const;

private:
	mutable LightLock lock_ = {};
	std::string selfPath_;
	std::string message_;
	UpdateState state_ = UpdateState::Idle;
	float progress_ = 0.0f;
	std::atomic<float> transferProgress_{ 0.0f };
	volatile int cancelFlag_ = 0;
	bool workerRunning_ = false;

	static void threadFunc(void* arg);
	void worker();
	void setStatus(UpdateState state, float progress, const std::string& message);
};
