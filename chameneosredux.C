#include <string>
#include <vector>
#include <iostream>
#include <future>
#include "source/actor.h"

static std::string spell(size_t n) {
	static const std::string numbers[] = { " zero", " one", " two", " three", " four", " five", " six", " seven", " eight", " nine" };
	auto next = numbers[n % 10];
	return n / 10 ? spell(n / 10) + next : next;
}

enum color { blue = 0, red, yellow };

static std::ostream& operator<<(std::ostream &s, const color &c ) {
	static const std::string names[] = { "blue", "red", "yellow" };
	return s << names[c];
}

static color table [3][3] = {
	{ blue, yellow, red },
	{ yellow, red, blue },
	{ red, blue, yellow },
};

static void show_complements() {
	for (auto i : { blue, red, yellow })
		for (auto j : { blue, red, yellow })
			std::cout << i << " + " << j << " -> " << (table[i][j]) << std::endl;
}

static std::mutex output_mutex;

static void print_header(std::initializer_list<color> colors) {
	std::cout << std::endl;
	for (auto i : colors)
		std::cout << " " << i;
	std::cout << std::endl;
}

using namespace actor;

struct message {
	const handle* chameneos;
	const color colour;
};

static void broker(size_t meetings_count, size_t color_count) {
	for (auto i = 0u; i < meetings_count; ++i) {
		message left = receive<message>();
		message right = receive<message>();
		left.chameneos->send(right);
		right.chameneos->send(left);
	}
}

static void cleanup(size_t color_count) {
	size_t summary = 0;
	for (auto i = 0u; i < color_count; ++i) {
		message last = receive<message>();
		last.chameneos->kill();
	}
	for (auto i = 0u; i < color_count; ++i) {
		summary += receive<size_t>();
	}
	std::lock_guard<std::mutex> lock(output_mutex);
	std::cout << spell(summary) << std::endl;
}

static void chameneos(color start_color, const handle& broker) {
	size_t meetings = 0, met_self = 0;
	color current = start_color;
	auto self = actor::self();
	try {
		while (1) {
			broker.send(message{&self, current});
			message tmp = receive<message>();
			meetings++;
			current = table[current][tmp.colour];
			if (tmp.chameneos == &self)
				met_self++;
		}
	}
	catch (const death&) {
		std::lock_guard<std::mutex> lock(output_mutex);
		std::cout << meetings << " " << spell(met_self) << std::endl;
		broker.send(meetings);
	}
}

static void run(std::initializer_list<color> colors, size_t count) {
	print_header(colors);
	std::vector<handle> chameneoses;
	for (auto color : colors)
		chameneoses.push_back(spawn(chameneos, color, self()));
	broker(count, colors.size());
	cleanup(colors.size());
	return;
}

int main (int argc, char ** argv) {
	std::vector<std::string> args(argv + 1, argv + argc);
	size_t count = args.size() ? std::stoul(args.at(0)) : 10;
	show_complements();
	run({ blue, red, yellow }, count);
	run({ blue, red, yellow, red, yellow, blue, red, yellow, red, blue }, count);
	std::cout << std::endl;
	return 0;
}
