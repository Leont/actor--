#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <list>

#include <boost/any.hpp>
namespace std {
	using any = boost::any;
	using boost::any_cast;
}

namespace actor {
	class death : public std::exception {
		const char* what() const noexcept {
			return "Actor terminated";
		}
	};

	class queue {
		std::mutex mutex;
		std::condition_variable cond;
		std::queue<std::any> incoming;
		std::list<std::any> pending;
		std::atomic<bool> kill_flag;
		queue(const queue&) = delete;
		queue& operator=(const queue&) = delete;
		public:
		queue()
		: mutex()
		, cond()
		, incoming()
		, pending()
		, kill_flag(false)
		{ }
		template<typename T> void push(const T& value) {
			std::lock_guard<std::mutex> lock(mutex);
			incoming.push(value);
			cond.notify_one();
		}
		template<typename T> void push(T&& value) {
			std::lock_guard<std::mutex> lock(mutex);
			incoming.push(std::move(value));
			cond.notify_one();
		}
		template<typename T> T pop() {
			for (auto current = pending.begin(); current != pending.end(); ++current) {
				if (T* tmp = std::any_cast<T>(&*current)) {
					T ret = std::move(*tmp);
					pending.erase(current);
					return ret;
				}
			}
			std::unique_lock<std::mutex> lock(mutex);
			while (1) {
				cond.wait(lock, [&] { return kill_flag || !incoming.empty(); });
				if (kill_flag)
					throw death();
				if (T* tmp = std::any_cast<T>(&incoming.front())) {
					T ret = std::move(*tmp);
					incoming.pop();
					return ret;
				}
				else {
					pending.push_back(std::move(incoming.front()));
					incoming.pop();
				}
			}
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

	class handle {
		std::weak_ptr<queue> weak_queue;
		public:
		explicit handle(const std::shared_ptr<queue>& other) noexcept : weak_queue(other) {}
		handle(const handle& other) noexcept = default;
		handle(handle& other) noexcept = default;
		handle(handle&& other) noexcept = default;
		template<typename T> void send(const T& value) const {
			auto strong_queue = weak_queue.lock();
			if (strong_queue)
				strong_queue->push(value);
		}
		template<typename T> void send(T&& value) const {
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
		friend void swap(handle& left, handle& right) noexcept {
			swap(left.weak_queue, right.weak_queue);
		}
	};

	namespace {
		thread_local std::shared_ptr<queue> mailbox = std::make_shared<queue>();
	}
	handle self() {
		return handle(mailbox);
	}

	template<typename T> T receive() {
		return mailbox->pop<T>();
	}

	template<typename U, typename... V> static handle spawn(U&& func, V&&... params) {
		auto mail = std::make_shared<queue>();
		auto callback = [mail, func=std::forward<U>(func), params...]() {
			mailbox = mail;
			func(params...);
		};
		std::thread(callback).detach();
		return handle(mail);
	}
}
