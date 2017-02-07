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
		queue() : mutex(), cond(), messages(), kill_flag(false) { }
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
			cond.wait(lock, [&] { return kill_flag || !messages.empty(); });
			if (kill_flag)
				throw death();
			T ret = std::move(messages.front());
			messages.pop();
			return ret;
		}
		void kill() {
			std::lock_guard<std::mutex> lock(mutex);
			kill_flag = true;
			cond.notify_all();
		}
		bool killed() const noexcept {
			return kill_flag;
		}
	};

	template<typename T> class handle;

	template<typename T> class receiver {
		std::shared_ptr<queue<T>> my_queue = std::make_shared<queue<T>>();
		friend class handle<T>;
		public:
		T receive() const {
			return my_queue->pop();
		}
		handle<T> self() const noexcept {
			return handle<T>(my_queue);
		}
	};

	template<typename T, typename U, typename... V> static handle<T> spawn(U&& u, V&&... params) {
		receiver<T> ret;
		std::thread(std::forward<U>(u), ret, std::forward<V>(params)...).detach();
		return ret.self();
	}

	template<typename T> class handle {
		std::weak_ptr<queue<T>> weak_queue;
		explicit handle(const std::shared_ptr<queue<T>>& other) noexcept : weak_queue(other) {}
		friend class receiver<T>;
		public:
		handle(const handle<T>& other) noexcept = default;
		handle(handle<T>& other) noexcept = default;
		handle(handle<T>&& other) noexcept = default;
		template<typename... U> explicit handle(U&&... _u) : handle(spawn<T>(std::forward<U>(_u)...)) { }
		void send(const T& value) const {
			auto strong_queue = weak_queue.lock();
			if (strong_queue)
				strong_queue->push(value);
		}
		void send(T&& value) const {
			auto strong_queue = weak_queue.lock();
			if (strong_queue)
				strong_queue->push(std::move(value));
		}
		void kill() const {
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
		friend void swap(handle<T>& left, handle<T>& right) noexcept {
			swap(left.weak_queue, right.weak_queue);
		}
	};
}
