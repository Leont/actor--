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
			std::tuple<Types...> _value;
			public:
			template<typename... Args> message_impl(Args&&... value)
			: _value(std::make_tuple(std::forward<Args>(value)...))
			{}
			std::tuple<Types...>&& get() {
				return std::move(_value);
			}
		};

		template<typename... Types> static std::unique_ptr<message> make_message(Types&&... values) {
			return std::make_unique<message_impl<std::decay_t<Types>...>>(std::forward<Types>(values)...);
		}

		template<typename T> struct message_for : public message_for<decltype(&T::operator())> {
		};
		template <typename ClassType, typename ReturnType, typename... Args> struct message_for<ReturnType(ClassType::*)(Args...) const> {
			using type = message_impl<std::decay_t<Args>...>;
		};

		template<typename Callback, typename Head, typename... Tail> static bool match_if(std::unique_ptr<message>& any, const Callback& callback, const Head& head, const Tail&... tail) {
			using message_type = typename message_for<Head>::type;

			if (message_type* real = dynamic_cast<message_type*>(any.get())) {
				auto owner = std::move(any);
				callback();
				std::apply(head, real->get());
				return true;
			}
			else if constexpr (sizeof...(Tail))
				return match_if(any, callback, tail...);
			else
				return false;
		}

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
			incoming.push(make_message(std::forward<Types>(values)...));
			cond.notify_one();
		}
		template<typename... Args> void match(const Args&... matchers) {
			match_with([this](auto& lock, const auto& check) -> bool { cond.wait(lock, check); return true; }, matchers...);
		}
		template<typename Clock, typename Rep, typename Period, typename... Args> bool match_until(const std::chrono::time_point<Clock, std::chrono::duration<Rep, Period>>& until, const Args&... args) {
			return match_with([this, &until](auto& lock, const auto& check) { return cond.wait_until(lock, until, check); }, args...);
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
		template<typename Waiter, typename... Args> bool match_with(const Waiter& waiter, const Args&... matchers) {
			static_assert(sizeof...(Args) != 0, "Can't call receive without arguments");

			for (auto current = pending.begin(); current != pending.end(); ++current)
				if (match_if(*current, [&] { pending.erase(current); }, matchers...))
					return true;
			std::unique_lock<std::mutex> lock(mutex);
			while (1) {
				if (!waiter(lock, [&] { return !incoming.empty(); }))
					return false;
				else if (match_if(incoming.front(), [&] { incoming.pop(); lock.unlock(); }, matchers...))
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

	template<typename... Matchers> void receive(const Matchers&... matchers) {
		hidden::mailbox->match(matchers...);
	}

	template<typename Clock, typename Rep, typename Period, typename... Matchers> bool receive_until(const std::chrono::time_point<Clock, std::chrono::duration<Rep, Period>>& until, Matchers&&... matchers) {
		return hidden::mailbox->match_until(until, matchers...);
	}

	template<typename Rep, typename Period, typename... Matchers> bool receive_for(const std::chrono::duration<Rep, Period>& until, Matchers&&... matchers) {
		return receive_until(std::chrono::steady_clock::now() + until, matchers...);
	}

	struct exit {};
	struct error {};

	template<typename Func, typename... Args> handle spawn(Func&& func, Args&&... params) {
		std::promise<handle> promise;
		auto callback = [&promise](auto function, auto... args) {
			promise.set_value(self());
			try {
				function(std::forward<Args>(args)...);
			}
			catch(...) {
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
