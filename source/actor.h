#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <utility>

namespace actor {
	template<typename T> class queue {
		std::mutex mutex;
		std::condition_variable cond;
		std::queue<T> messages;
		queue(const queue&) = delete;
		queue<T>& operator=(const queue<T>&) = delete;
		public:
		queue() : mutex(), cond(), messages() { };
		void push(T&& value) noexcept {
			std::lock_guard<std::mutex> lock(mutex);
			messages.push(value);
			cond.notify_one();
		}
		T pop() noexcept {
			std::unique_lock<std::mutex> lock(mutex);
			while (messages.empty());
				cond.wait(lock);
			return messages.pop();
		}
	};

	template<typename T> class receiver {
		const std::shared_ptr<queue<T>> mailbox;
		public:
		receiver(const std::shared_ptr<queue<T>>& _mailbox) : mailbox(_mailbox) { }
		T receive() noexcept {
			return mailbox->pop();
		}
	};

	template<typename T> class actor {
		std::shared_ptr<queue<T>> mailbox;
		public:
		template<typename U, typename... V> actor(U&& u, V&&... params) noexcept : mailbox(new queue<T>()) {
			std::thread thr(u, receiver<T>(mailbox), params...);
			thr.detach();
		}
		void send(T&& value) noexcept {
			mailbox->push(value);
		}
	};
}
