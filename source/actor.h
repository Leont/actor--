#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

namespace actor {
	template<typename T> class queue {
		std::mutex mutex;
		std::condition_variable cond;
		std::queue<T> messages;
		queue(const queue&) = delete;
		queue<T>& operator=(const queue<T>&) = delete;
		public:
		queue() : mutex(), cond(), messages() { };
		void push(T&& value) {
			std::lock_guard<std::mutex> lock(mutex);
			messages.push(value);
			cond.notify_one();
		}
		T pop() {
			std::unique_lock<std::mutex> lock(mutex);
			while (messages.empty());
				cond.wait(lock);
			return messages.pop();
		}
	};

	template<typename T> class receiver {
		const std::shared_ptr<queue<T>> mailbox;
		template<typename> friend class actor;
		receiver(const std::shared_ptr<queue<T>>& _mailbox) : mailbox(_mailbox) { }
		public:
		T receive() const {
			return mailbox->pop();
		}
	};

	template<typename T> class actor {
		std::weak_ptr<queue<T>> mailbox_ref;
		public:
		template<typename U, typename... V> actor(U&& u, V&&... _params) : mailbox_ref([&](V&&... params){
			auto mailbox = std::make_shared<queue<T>>();
			std::thread(u, receiver<T>(mailbox), std::forward<V>(params)...).detach();
			return mailbox;
		}(std::forward<V>(_params)...)) {}
		void send(T&& value) const {
			auto mailbox = mailbox_ref.lock();
			if (mailbox)
				mailbox->push(value);
		}
		bool alive() const noexcept {
			return !mailbox_ref.expired();
		}
	};
}
