#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

namespace actor {
	class death : public std::exception {
		const char* what() const noexcept {
			return "Actor terminated";
		}
	};
	template<typename T> class queue {
		std::mutex mutex;
		std::condition_variable cond;
		std::queue<T> messages;
		queue(const queue&) = delete;
		queue<T>& operator=(const queue<T>&) = delete;
		bool killed = false;
		public:
		queue() : mutex(), cond(), messages() { };
		void push(const T& value) {
			std::lock_guard<std::mutex> lock(mutex);
			messages.push(value);
			cond.notify_one();
		}
		void push(T&& value) {
			std::lock_guard<std::mutex> lock(mutex);
			messages.push(std::move(value));
			cond.notify_one();
		}
		T pop() {
			std::unique_lock<std::mutex> lock(mutex);
			while (messages.empty()) {
				if (killed)
					throw death();
				cond.wait(lock);
			}
			T ret = messages.front();
			messages.pop();
			return ret;
		}
		void kill() {
			std::lock_guard<std::mutex> lock(mutex);
			killed = true;
			cond.notify_one();
		}
	};

	template<typename T> class receiver;

	template<typename T> class thread {
		queue<T> mailbox;
		template<typename U, typename... V> void start(std::shared_ptr<thread<T>>& ptr, U&& u, V&&... params) {
			std::thread(std::forward<U>(u), receiver<T>(ptr), std::forward<V>(params)...).detach();
		}
		public:
		thread() : mailbox() { }
		template<typename U, typename... V> static std::weak_ptr<thread<T>> spawn(U&& u, V&&... params) {
			auto ret = std::make_shared<thread<T>>();
			ret->start(ret, std::forward<U>(u), std::forward<V>(params)...);
			return ret;
		}
		const queue<T>& get_mailbox() const {
			return mailbox;
		}
		queue<T>& get_mailbox() {
			return mailbox;
		}
	};

	template<typename T> class actor;

	template<typename T> class receiver {
		std::shared_ptr<thread<T>> athread;
		receiver(const std::shared_ptr<thread<T>>& _athread) : athread(_athread) { }
		friend class thread<T>;
		public:
		T receive() const {
			return athread->get_mailbox().pop();
		}
		actor<T> self() const {
			return actor<T>(athread);
		}
	};

	template<typename T> class actor {
		std::weak_ptr<thread<T>> athread;
		actor(const std::shared_ptr<thread<T>>& other) : athread(other) {}
		friend class receiver<T>;
		public:
		actor(const actor<T>& other) : athread(other.athread) { }
		actor(actor<T>& other) : athread(other.athread) { }
		actor(actor<T>&& other) : athread(std::move(other.athread)) {}
		actor<T>& operator=(const actor& other) {
			athread = other.athread;
		}
		actor<T>& operator=(actor&& other) {
			athread = std::move(other.athread);
		}
		template<typename U, typename... V> explicit actor(U&& _u, V&&... _params) : athread(thread<T>::spawn(std::forward<U>(_u), std::forward<V>(_params)...)) {
		}
		void send(const T& value) const {
			auto lthread = athread.lock();
			if (lthread)
				lthread->get_mailbox().push(value);
		}
		void send(T&& value) const {
			auto lthread = athread.lock();
			if (lthread)
				lthread->get_mailbox().push(std::move(value));
		}
		void kill() const {
			auto lthread = athread.lock();
			if (lthread)
				lthread->get_mailbox().kill();
		}
		bool alive() const noexcept {
			return !athread.expired();
		}
	};
}
