// Copyright Leon Timmermans 2012-2017

#ifndef __ACTOR_H__
#define __ACTOR_H__

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <tuple>

namespace actor {
	class queue {
		class message {
			public:
			virtual ~message() = default;
		};
		template<typename... Types> class message_impl : public message {
			std::tuple<Types...> tuple;
			public:
			template<typename... Args> message_impl(Args&&... value)
			: tuple(std::make_tuple(std::forward<Args>(value)...))
			{}
			template<typename Handler> void apply(Handler& handler) {
				return std::apply(handler, std::move(tuple));
			}
		};
		template<typename... Types> using message_t = message_impl<std::decay_t<Types>...>;

		template<typename T> struct message_for : public message_for<decltype(&T::operator())> {
		};
		template <typename ClassType, typename ReturnType, typename... Args> struct message_for<ReturnType(ClassType::*)(Args...) const> {
			using type = message_t<Args...>;
		};
		template <typename ClassType, typename ReturnType, typename... Args> struct message_for<ReturnType(ClassType::*)(Args...)> {
			using type = message_t<Args...>;
		};

		template<typename... Types> class matcher {
			using tuple_type = std::tuple<Types&&...>;
			static_assert(std::tuple_size<tuple_type>::value != 0, "Can't call receive without arguments");
			tuple_type tuple;
			public:
			matcher(Types&&... matchers)
			: tuple(std::make_tuple(std::forward<Types>(matchers)...))
			{}
			template<size_t position = 0, typename Callback> bool match(std::unique_ptr<message>& any, const Callback& callback) {
				using message_type = typename message_for<std::decay_t<std::tuple_element_t<position, tuple_type>>>::type;

				if (message_type* real = dynamic_cast<message_type*>(any.get())) {
					auto owner = std::move(any);
					callback();
					real->apply(std::get<position>(tuple));
					return true;
				}
				else if constexpr (position + 1 < std::tuple_size<tuple_type>::value)
					return match<position+1>(any, callback);
				else
					return false;
			}
		};

		std::mutex mutex;
		std::condition_variable cond;
		std::queue<std::unique_ptr<message>> incoming;
		std::list<std::unique_ptr<message>> pending;
		std::vector<std::weak_ptr<queue>> monitors;
		std::atomic<bool> living;
		queue(const queue&) = delete;
		queue& operator=(const queue&) = delete;

		public:
		queue()
		: mutex()
		, cond()
		, incoming()
		, pending()
		, monitors()
		, living(true)
		{ }
		template<typename... Types> void push(Types&&... values) {
			std::lock_guard<std::mutex> lock(mutex);
			if (!living)
				return;
			incoming.push(std::make_unique<message_t<Types...>>(std::forward<Types>(values)...));
			cond.notify_one();
		}
		template<typename Matcher> void match(Matcher&& matchers) {
			match_with([this](std::unique_lock<std::mutex>& lock) -> bool { cond.wait(lock, [&] { return !incoming.empty(); }); return true; }, std::forward<Matcher>(matchers));
		}
		template<typename Clock, typename Rep, typename Period, typename Matcher> bool match_until(const std::chrono::time_point<Clock, std::chrono::duration<Rep, Period>>& until, Matcher&& matchers) {
			return match_with([this, &until](std::unique_lock<std::mutex>& lock) { return cond.wait_until(lock, until, [&] { return !incoming.empty(); }); }, std::forward<Matcher>(matchers));
		}
		bool add_monitor(const std::shared_ptr<queue>& monitor) {
			std::lock_guard<std::mutex> lock(mutex);
			if (living)
				monitors.push_back(monitor);
			return living;
		}
		bool alive() const {
			return living;
		}
		template<typename... Args> void mark_dead(const Args&... args) {
			std::lock_guard<std::mutex> lock(mutex);
			living = false;
			pending.clear();
			while (!incoming.empty())
				incoming.pop();
			for (const auto& monitor : monitors)
				if (const auto strong = monitor.lock())
					strong->push(args...);
			monitors.clear();
		}
		private:
		template<typename Waiter, typename Matcher> bool match_with(const Waiter& waiter, Matcher&& matchers) {
			for (auto current = pending.begin(); current != pending.end(); ++current)
				if (matchers.match(*current, [&] { pending.erase(current); }))
					return true;
			std::unique_lock<std::mutex> lock(mutex);
			while (1) {
				if (!waiter(lock))
					return false;
				else if (matchers.match(incoming.front(), [&] { incoming.pop(); lock.unlock(); }))
					return true;
				else {
					pending.push_back(std::move(incoming.front()));
					incoming.pop();
				}
			}
		}
	};

	namespace hidden {
		extern const thread_local std::shared_ptr<queue> mailbox = std::make_shared<queue>();
	}

	class handle {
		std::shared_ptr<queue> mailbox;
		public:
		explicit handle(const std::shared_ptr<queue>& other) noexcept : mailbox(other) {}
		template<typename... Args> void send(Args&&... args) const {
			mailbox->push(std::forward<Args>(args)...);
		}
		bool monitor() const {
			return mailbox->add_monitor(hidden::mailbox);
		}
		bool alive() const noexcept {
			return mailbox->alive();
		}
		friend void swap(handle& left, handle& right) noexcept {
			swap(left.mailbox, right.mailbox);
		}
		friend bool operator==(const handle& left, const handle& right) {
			return left.mailbox == right.mailbox;
		}
		friend bool operator!=(const handle& left, const handle& right) {
			return left.mailbox != right.mailbox;
		}
		friend bool operator<(const handle& left, const handle& right) {
			return left.mailbox < right.mailbox;
		}
	};

	namespace hidden {
		extern const thread_local handle self_var(hidden::mailbox);
	}
	static inline const handle& self() {
		return hidden::self_var;
	}

	template<typename... Matchers> void receive(Matchers&&... matchers) {
		hidden::mailbox->match(queue::matcher(std::forward<Matchers>(matchers)...));
	}

	template<typename Clock, typename Rep, typename Period, typename... Matchers> bool receive_until(const std::chrono::time_point<Clock, std::chrono::duration<Rep, Period>>& until, Matchers&&... matchers) {
		return hidden::mailbox->match_until(until, queue::matcher(std::forward<Matchers>(matchers)...));
	}

	template<typename Rep, typename Period, typename... Matchers> bool receive_for(const std::chrono::duration<Rep, Period>& until, Matchers&&... matchers) {
		return receive_until(std::chrono::steady_clock::now() + until, std::forward<Matchers>(matchers)...);
	}

	struct exit {};
	struct error {};
	struct stop {};

	template<typename... Matchers> void receive_loop(Matchers&&... matchers) {
		try {
			auto matching = queue::matcher(std::forward<Matchers>(matchers)...);
			while (1)
				hidden::mailbox->match(matching);
		}
		catch (stop) { }
	}

	static inline void leave_loop() {
		throw stop();
	}

	template<typename Func, typename... Args> handle spawn(Func&& func, Args&&... params) {
		std::promise<handle> promise;
		auto callback = [&promise](auto function, auto... args) {
			promise.set_value(self());
			try {
				function(std::forward<Args>(args)...);
			}
			catch (exit) { }
			catch (...) {
				hidden::mailbox->mark_dead(error(), self(), std::current_exception());
				return;
			}
			hidden::mailbox->mark_dead(exit(), self());
		};
		std::thread(callback, std::forward<Func>(func), std::forward<Args>(params)...).detach();
		return promise.get_future().get();
	}
}

#endif
