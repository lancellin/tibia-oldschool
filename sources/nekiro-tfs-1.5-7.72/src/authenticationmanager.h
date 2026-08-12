#ifndef FS_AUTHENTICATIONMANAGER_H_6B6C70CE97A14AB89723ECEB80641E4E
#define FS_AUTHENTICATIONMANAGER_H_6B6C70CE97A14AB89723ECEB80641E4E

#include "authenticationservice.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class AuthenticationManager
{
	public:
		using Completion = std::function<void(AuthenticationResult)>;

		bool start();
		void shutdown();
		bool enqueue(AuthenticationRequest request, Completion completion);

		bool isRunning() const;
		size_t pendingCount() const;

	private:
		struct Task {
			AuthenticationRequest request;
			Completion completion;
		};

		void workerMain();

		mutable std::mutex mutex;
		std::condition_variable signal;
		std::deque<Task> tasks;
		std::vector<std::thread> workers;
		Argon2Policy policy;
		size_t queueCapacity = 0;
		bool running = false;
		bool stopping = false;
};

extern AuthenticationManager g_authenticationManager;

#endif
