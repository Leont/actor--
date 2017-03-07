// Copyright Leon Timmermans 2012-2017

#ifndef __ACTOR_H__
#define __ACTOR_H__

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <list>
#include <tuple>

#include <experimental/tuple>
#include <boost/any.hpp>
namespace std {
	using any = boost::any;
	using boost::any_cast;
	using experimental::apply;
}

namespace actor {
	namespace {
		template<typename T> struct function_traits : public function_traits<decltype(&T::operator())> {
		};
		template <typename ClassType, typename ReturnType, typename... Args> struct function_traits<ReturnType(ClassType::*)(Args...) const> {
			using args = std::tuple<typename std::decay<Args>::type...>;
		};

		template<size_t pos = 0, typename... T> static typename std::enable_if<pos >= sizeof...(T), bool>::type match_if(const std::any&, const std::tuple<T...>&) {
			return false;
		}
		template<size_t pos = 0, typename... T> static typename std::enable_if<pos < sizeof...(T), bool>::type match_if(const std::any& any, const std::tuple<T...>& tuple) {
			using current = typename std::tuple_element<pos, std::tuple<T...>>::type;
			using arg_type = typename function_traits<current>::args;
			if (const arg_type* value = std::any_cast<arg_type>(&any)) {
				std::apply(std::get<pos>(tuple), *value);
				return true;
			}
			else
				return match_if<pos+1>(any, tuple);
		}

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
			std::tuple<A...> matchers(std::forward<A>(args)...);

			for (auto current = pending.begin(); current != pending.end(); ++current) {
				if (match_if(*current, matchers)) {
					pending.erase(current);
					return;
				}
			}
			std::unique_lock<std::mutex> lock(mutex);
			while (1) {
				cond.wait(lock, [&] { return !incoming.empty(); });
				if (match_if(incoming.front(), matchers)) {
					incoming.pop();
					return;
				}
				else {
					pending.push_back(std::move(incoming.front()));
					incoming.pop();
				}
			}
		}
		template<typename Clock, typename Rep, typename Period, typename... A> bool match_until(const std::chrono::time_point<Clock, std::chrono::duration<Rep, Period>>& until, A&&... args) {
			std::tuple<A...> matchers(std::forward<A>(args)...);

			for (auto current = pending.begin(); current != pending.end(); ++current) {
				if (match_if(*current, matchers)) {
					pending.erase(current);
					return true;
				}
			}
			std::unique_lock<std::mutex> lock(mutex);
			while (1) {
				if (!cond.wait_until(lock, until, [&] { return !incoming.empty(); }))
					return false;
				if (match_if(incoming.front(), matchers)) {
					incoming.pop();
					return true;
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
		template<typename... Args> void send(Args&&... args) const {
			auto strong_queue = weak_queue.lock();
			if (strong_queue)
				strong_queue->push(std::make_tuple(std::forward<Args>(args)...));
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

	template<typename... Args> std::tuple<Args...> receive() {
		return mailbox->pop<std::tuple<Args...>>();
	}

	template<typename... Matchers> void receive(Matchers&&... matchers) {
		mailbox->match(std::forward<Matchers>(matchers)...);
	}

	template<typename Condition, typename... Matchers> void receive_while(const Condition& condition, Matchers&&... matchers) {
		while (condition)
			receive(std::forward<Matchers>(matchers)...);
	}

	template<typename Clock, typename Rep, typename Period, typename... Matchers> bool receive_until(const std::chrono::time_point<Clock, std::chrono::duration<Rep, Period>>& until, Matchers&&... matchers) {
		return mailbox->match_until(until, std::forward<Matchers>(matchers)...);
	}

	template<typename Rep, typename Period, typename... Matchers> bool receive_for(const std::chrono::duration<Rep, Period>& until, Matchers&&... matchers) {
		return receive_until(std::chrono::steady_clock::now() + until, std::forward<Matchers>(matchers)...);
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

#endif
