#include <string>
#include <vector>
#include <iostream>
#include <future>
#include <actor.h>

static std::string spell(const size_t n) {
	static const std::string numbers[] = { " zero", " one", " two", " three", " four", " five", " six", " seven", " eight", " nine" };
	const auto next = numbers[n % 10];
	return n / 10 ? spell(n / 10) + next : next;
}

enum color { blue = 0, red, yellow };

static inline std::ostream& operator<<(std::ostream& s, const color c) {
	static const std::string names[] = { "blue", "red", "yellow" };
	return s << names[c];
}

static color table [3][3] = {
	{ blue, yellow, red },
	{ yellow, red, blue },
	{ red, blue, yellow },
};

static void show_complements() {
	for (const auto i : { blue, red, yellow })
		for (const auto j : { blue, red, yellow })
			std::cout << i << " + " << j << " -> " << (table[i][j]) << std::endl;
}

static void print_header(const std::initializer_list<color>& colors) {
	std::cout << std::endl;
	for (const auto i : colors)
		std::cout << " " << i;
	std::cout << std::endl;
}

using namespace actor;

static void broker(const size_t meetings_count) {
	for (auto i = 0ul; i < meetings_count; ++i) {
		receive([](const handle& handle_left, color color_left) {
			receive([&](const handle& handle_right, color color_right) {
				handle_left.send(handle_right, color_right);
				handle_right.send(handle_left, color_left);
			});
		});
	}
}

static std::mutex output_mutex;

static void cleanup(size_t color_count) {
	auto summary = 0ul;
	receive_loop(
		[] (const handle& other, color) {
			other.send(stop());
		},
		[&] (size_t mismatch) {
			summary += mismatch;
			if (--color_count == 0)
				throw stop();
		}
	);
	std::lock_guard<std::mutex> lock(output_mutex);
	std::cout << spell(summary) << std::endl;
}

static void chameneos(color current, const handle& broker) {
	auto meetings = 0ul, met_self = 0ul;
	const auto self = actor::self();
	broker.send(self, current);
	receive_loop(
		[&] (const handle& other, const color colour) {
			meetings++;
			current = table[current][colour];
			if (other == self)
				met_self++;
			broker.send(self, current);
		},
		[&] (stop) {
			std::lock_guard<std::mutex> lock(output_mutex);
			std::cout << meetings << " " << spell(met_self) << std::endl;
			broker.send(meetings);
			throw stop();
		}
	);
}

static void run(const std::initializer_list<color>& colors, const size_t count) {
	print_header(colors);
	for (const auto color : colors)
		spawn(chameneos, color, self());
	broker(count);
	cleanup(colors.size());
	return;
}

int main (int argc, char ** argv) {
	const auto count = argc > 1 ? std::stoul(argv[1]) : 10000ul;
	show_complements();
	run({ blue, red, yellow }, count);
	run({ blue, red, yellow, red, yellow, blue, red, yellow, red, blue }, count);
	std::cout << std::endl;
	return 0;
}
