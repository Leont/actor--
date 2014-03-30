#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

#include <iostream>

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
			messages.push(value);
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

	template<typename T> class receiver {
		const std::shared_ptr<queue<T>> mailbox;
		public:
		receiver(const std::shared_ptr<queue<T>>& _mailbox) : mailbox(_mailbox) { }
		T receive() const {
			return mailbox->pop();
		}
	};

	template<typename T> class actor {
		std::shared_ptr<queue<T>> mailbox_ref;
		public:
		actor(const actor<T>& other) : mailbox_ref(other.mailbox_ref) { }
		actor(actor<T>&& other) : mailbox_ref(std::move(other.mailbox_ref)) {}
		actor<T>& operator=(const actor& other) {
			mailbox_ref = other.mailbox_ref;
		}
		actor<T>& operator=(actor&& other) {
			mailbox_ref = std::move(other.mailbox_ref);
		}
		//
		template<typename U, typename... V , typename W = typename std::enable_if<std::is_function<typename std::remove_reference<U>::type>::value>::type> explicit actor(U&& _u, V&&... _params) : mailbox_ref([this](U&& u, V&&... params) {
			static_assert(!std::is_same<U, receiver<T>>::value, "ERROR");
			auto mailbox = std::make_shared<queue<T>>();
			receiver<T> receive(mailbox);
			std::thread(std::forward<U>(u), *this, receive, std::forward<V>(params)...).detach();
			return mailbox;
		}(std::forward<U>(_u), std::forward<V>(_params)...)) {}
		void send(const T& value) const {
			auto mailbox = mailbox_ref;
			if (mailbox)
				mailbox->push(value);
		}
		void send(T&& value) const {
			auto mailbox = mailbox_ref;
			if (mailbox)
				mailbox->push(value);
		}
		void kill() const {
			auto mailbox = mailbox_ref;
			if (mailbox)
				mailbox->kill();
		}
		bool alive() const noexcept {
			return !mailbox_ref.expired();
		}
	};
	template<typename T, typename U, typename... V> actor<T> spawn(U&& u, V&&... params) {
		return actor<T>(true, std::forward<U>(u), std::forward<V>(params)...);
	}
}
