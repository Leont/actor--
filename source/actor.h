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
			message_impl(Types... value)
			: tuple(std::forward<Types>(value)...)
			{}
			template<typename Handler> void apply(Handler& handler) {
				return std::apply(handler, std::move(tuple));
			}
		};
		template<typename... Types> using message_t = message_impl<std::decay_t<Types>...>;

		template<typename T> struct message_for : public message_for<decltype(&std::remove_reference_t<T>::operator())> {
		};
		template<typename ClassType, typename ReturnType, typename... Args> struct message_for<ReturnType (ClassType::*)(Args...) const> {
			using type = message_t<Args...>;
		};
		template<typename ClassType, typename ReturnType, typename... Args> struct message_for<ReturnType (ClassType::*)(Args...)> {
			using type = message_t<Args...>;
		};

		template<typename Derived, typename Base> static std::unique_ptr<Derived> dynamic_pointer_cast(std::unique_ptr<Base>& input) {
			if (Derived* result = dynamic_cast<Derived*>(input.get())) {
				input.release();
				return std::unique_ptr<Derived>(result);
			}
			return nullptr;
		}

		template<typename... Types> class matcher {
			static constexpr size_t elements = sizeof...(Types);
			static_assert(elements != 0, "Can't call receive without arguments");
			using tuple_type = std::tuple<Types...>;
			tuple_type tuple;
			template<size_t position> using message_type = typename message_for<std::tuple_element_t<position, tuple_type>>::type;

			public:
			matcher(Types... matchers)
			: tuple(std::move(matchers)...)
			{}

			template<size_t position = 0> bool match(std::unique_ptr<message>& msg) {
				if (auto owner = dynamic_pointer_cast<message_type<position>>(msg)) {
					owner->apply(std::get<position>(tuple));
					return true;
				}
				else if constexpr (position + 1 < elements)
					return match<position + 1>(msg);
				else
					return false;
			}
		};

		std::mutex mutex;
		std::condition_variable cond;
		std::queue<std::unique_ptr<message>> incoming;
		std::list<std::unique_ptr<message>> pending;
		std::vector<std::weak_ptr<queue>> monitors;
		std::atomic<bool> dead;

		queue(const queue&) = delete;
		queue& operator=(const queue&) = delete;

		public:
		queue() = default;

		template<typename... Types> void push(Types&&... values) {
			std::lock_guard<std::mutex> lock(mutex);
			if (dead)
				return;
			incoming.push(std::make_unique<message_t<Types...>>(std::forward<Types>(values)...));
			cond.notify_one();
		}
		template<typename Matcher> void match(Matcher&& matchers) {
			auto waiter = [this](std::unique_lock<std::mutex>& lock) {
				cond.wait(lock, [this] { return !incoming.empty(); });
				return true;
			};
			match_with(std::forward<Matcher>(matchers), waiter);
		}
		template<typename Until, typename Matcher> bool match_until(const Until& until, Matcher&& matchers) {
			auto waiter = [this, &until](std::unique_lock<std::mutex>& lock) {
				return cond.wait_until(lock, until, [this] { return !incoming.empty(); });
			};
			return match_with(std::forward<Matcher>(matchers), waiter);
		}

		bool add_monitor(const std::shared_ptr<queue>& monitor) {
			std::lock_guard<std::mutex> lock(mutex);
			if (!dead)
				monitors.push_back(monitor);
			return !dead;
		}
		bool alive() const noexcept {
			return !dead;
		}
		template<typename... Args> void mark_dead(const Args&... args) {
			std::lock_guard<std::mutex> lock(mutex);
			dead = true;
			pending.clear();
			while (!incoming.empty())
				incoming.pop();
			for (const auto& monitor : monitors)
				if (const auto strong = monitor.lock())
					strong->push(args...);
			monitors.clear();
		}

		private:
		template<typename Waiter> std::unique_ptr<message> pop_incoming(const Waiter& waiter) {
			std::unique_lock<std::mutex> lock(mutex);
			if (!waiter(lock))
				return nullptr;
			std::unique_ptr<message> next = std::move(incoming.front());
			incoming.pop();
			return next;
		}
		template<typename Matcher, typename Waiter> bool match_with(Matcher&& matchers, const Waiter& waiter) {
			for (auto current = pending.begin(); current != pending.end(); ++current) {
				if (!*current)
					pending.erase(current);
				else if (matchers.match(*current))
					return true;
			}
			while (std::unique_ptr<message> next = pop_incoming(waiter)) {
				if (matchers.match(next))
					return true;
				else
					pending.push_back(std::move(next));
			}
			return false;
		}
	};

	namespace hidden {
		extern const thread_local std::shared_ptr<queue> mailbox = std::make_shared<queue>();
	}

	class handle {
		std::shared_ptr<queue> mailbox;

		public:
		explicit handle(const std::shared_ptr<queue>& other) noexcept
		: mailbox(other)
		{}

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

		friend bool operator==(const handle& left, const handle& right) noexcept {
			return left.mailbox == right.mailbox;
		}
		friend bool operator!=(const handle& left, const handle& right) noexcept {
			return left.mailbox != right.mailbox;
		}
		friend bool operator<(const handle& left, const handle& right) noexcept {
			return left.mailbox < right.mailbox;
		}
	};

	namespace hidden {
		extern const thread_local handle self_var(hidden::mailbox);
	}
	static inline const handle& self() noexcept {
		return hidden::self_var;
	}

	template<typename... Matchers> void receive(Matchers&&... matchers) {
		hidden::mailbox->match(queue::matcher<Matchers...>(std::forward<Matchers>(matchers)...));
	}

	template<typename Until, typename... Matchers> bool receive_until(const Until& until, Matchers&&... matchers) {
		return hidden::mailbox->match_until(until, queue::matcher<Matchers...>(std::forward<Matchers>(matchers)...));
	}

	template<typename Duration, typename... Matchers> bool receive_for(const Duration& until, Matchers&&... matchers) {
		return receive_until(std::chrono::steady_clock::now() + until, std::forward<Matchers>(matchers)...);
	}

	struct exit {};
	struct error {};
	struct stop {};

	template<typename... Matchers> void receive_loop(Matchers&&... matchers) {
		try {
			auto matching = queue::matcher<Matchers...>(std::forward<Matchers>(matchers)...);
			while (1)
				hidden::mailbox->match(matching);
		}
		catch (stop) {
		}
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
			catch (exit) {
			}
			catch (...) {
				hidden::mailbox->mark_dead(error(), self(), std::current_exception());
				return;
			}
			hidden::mailbox->mark_dead(exit(), self());
		};
		std::thread(callback, std::forward<Func>(func), std::forward<Args>(params)...).detach();
		return promise.get_future().get();
	}
} // namespace actor

#endif
