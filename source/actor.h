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
	namespace {
		template<typename T> struct function_traits : public function_traits<decltype(&T::operator())> {
		};
		template <typename ClassType, typename ReturnType, typename Args> struct function_traits<ReturnType(ClassType::*)(Args) const> {
			using arg = typename std::decay<Args>::type;
		};

		template<size_t pos, typename... T> static typename std::enable_if<pos >= sizeof...(T), bool>::type match_if(const std::any& any, const std::tuple<T...>& tuple) {
			return false;
		}
		template<size_t pos, typename... T> static typename std::enable_if<pos < sizeof...(T), bool>::type match_if(const std::any& any, const std::tuple<T...>& tuple) {
			using current = typename std::tuple_element<pos, std::tuple<T...>>::type;
			using arg_type = typename function_traits<current>::arg;
			if (const arg_type* value = std::any_cast<arg_type>(&any)) {
				std::get<pos>(tuple)(*value);
				return true;
			}
			else
				return match_if<pos+1>(any, tuple);
		}

		template<typename... Values> class options {
			std::tuple<Values...> matchers;
			public:
			options(Values&&... values)
			: matchers(std::forward<Values>(values)...)
			{ }
			bool match(const std::any& any) {
				return match_if<0>(any, matchers);
			}
		};
	}

	class queue {
		std::mutex mutex;
		std::condition_variable cond;
		std::queue<std::any> incoming;
		std::list<std::any> pending;
		queue(const queue&) = delete;
		queue& operator=(const queue&) = delete;
		public:
		queue()
		: mutex()
		, cond()
		, incoming()
		, pending()
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
				cond.wait(lock, [&] { return !incoming.empty(); });
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
		template<typename... A> void match(A&&... args) {
			options<A...> opts(std::forward<A>(args)...);

			for (auto current = pending.begin(); current != pending.end(); ++current) {
				if (opts.match(*current)) {
					pending.erase(current);
					return;
				}
			}
			std::unique_lock<std::mutex> lock(mutex);
			while (1) {
				cond.wait(lock, [&] { return !incoming.empty(); });
				if (opts.match(incoming.front())) {
					incoming.pop();
					return;
				}
				else {
					pending.push_back(std::move(incoming.front()));
					incoming.pop();
				}
			}
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

	template<typename... Matchers> void receive(Matchers&&... matchers) {
		mailbox->match(std::forward<Matchers>(matchers)...);
	}

	template<typename U, typename... V> handle spawn(U&& func, V&&... params) {
		auto mail = std::make_shared<queue>();
		auto callback = [mail, func=std::forward<U>(func), params...]() {
			mailbox = mail;
			func(params...);
		};
		std::thread(callback).detach();
		return handle(mail);
	}
}
