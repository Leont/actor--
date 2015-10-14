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
		std::atomic<bool> kill_flag;
		queue(const queue&) = delete;
		queue<T>& operator=(const queue<T>&) = delete;
		public:
		queue() noexcept : mutex(), cond(), messages(), kill_flag(false) { };
		void push(const T& value) noexcept(noexcept(T(value))) {
			std::lock_guard<std::mutex> lock(mutex);
			messages.push(value);
			cond.notify_one();
		}
		void push(T&& value) noexcept(noexcept(T(std::move(value)))) {
			std::lock_guard<std::mutex> lock(mutex);
			messages.push(std::move(value));
			cond.notify_one();
		}
		T pop() {
			std::unique_lock<std::mutex> lock(mutex);
			cond.wait(lock, [&] { return kill_flag || !messages.empty(); });
			if (kill_flag)
				throw death();
			T ret = std::move(messages.front());
			messages.pop();
			return std::move(ret);
		}
		void kill() noexcept {
			std::lock_guard<std::mutex> lock(mutex);
			kill_flag = true;
			cond.notify_all();
		}
		bool killed() const noexcept {
			return kill_flag;
		}
	};

	template<typename T> class actor;

	template<typename T> class receiver {
		std::shared_ptr<queue<T>> my_queue;
		explicit receiver(const std::shared_ptr<queue<T>>& _my_queue) noexcept : my_queue(_my_queue) { }
		friend class actor<T>;
		public:
		T receive() const {
			return my_queue->pop();
		}
		actor<T> self() const noexcept {
			return actor<T>(my_queue);
		}
	};

	template<typename T> class actor {
		std::weak_ptr<queue<T>> weak_queue;
		explicit actor(const std::shared_ptr<queue<T>>& other) noexcept : weak_queue(other) {}
		template<typename U, typename... V> static std::weak_ptr<queue<T>> spawn(U&& u, V&&... params) {
			auto ret = std::make_shared<queue<T>>();
			std::thread(std::forward<U>(u), receiver<T>(ret), std::forward<V>(params)...).detach();
			return ret;
		}
		friend class receiver<T>;
		public:
		actor(const actor<T>& other) noexcept : weak_queue(other.weak_queue) { }
		actor(actor<T>& other) noexcept : weak_queue(other.weak_queue) { }
		actor(actor<T>&& other) noexcept : weak_queue(std::move(other.weak_queue)) { }
		actor<T>& operator=(const actor& other) noexcept {
			weak_queue = other.weak_queue;
		}
		actor<T>& operator=(actor&& other) noexcept {
			weak_queue = std::move(other.weak_queue);
		}
		template<typename U, typename... V> explicit actor(U&& _u, V&&... _params) : weak_queue(spawn(std::forward<U>(_u), std::forward<V>(_params)...)) {
		}
		void send(const T& value) const noexcept(noexcept(T(value))) {
			auto strong_queue = weak_queue.lock();
			if (strong_queue)
				strong_queue->push(value);
		}
		void send(T&& value) const noexcept(noexcept(T(std::move(value)))) {
			auto strong_queue = weak_queue.lock();
			if (strong_queue)
				strong_queue->push(std::move(value));
		}
		void kill() const noexcept {
			auto strong_queue = weak_queue.lock();
			if (strong_queue)
				strong_queue->kill();
		}
		bool killed() const noexcept {
			auto strong_queue = weak_queue.lock();
			return strong_queue ? strong_queue->killed() : true;
		}
		bool zombie() const noexcept {
			return weak_queue.expired();
		}
	};
}
