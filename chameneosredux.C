#include <string>
#include <vector>
#include <iostream>
#include <future>
#include "source/actor.h"

std::string spell(int n) {
	static std::string numbers[] = {
	   " zero", " one", " two",
	   " three", " four", " five",
	   " six", " seven", " eight",
	   " nine"
	};

	std::string str;
	do {
	   str.insert( 0, numbers[n % 10] );
	   n /= 10;
	} while (n);

	return str;
}

enum color { blue = 0, red, yellow };

// stream operator to write a color

std::ostream& operator<<(std::ostream &s, const color &c ) {
   static const char *names[] = { "blue", "red", "yellow" };
   s << names[c];
   return s;
}

color operator+(const color &c1, const color &c2) {
   switch ( c1 ) {
      case blue: switch ( c2 ) {
         case blue:   return blue;
         case red:    return yellow;
         case yellow: return red;
         default:;
      }
      case red: switch ( c2 ) {
         case blue:   return yellow;
         case red:    return red;
         case yellow: return blue;
         default:;
      }
      case yellow: switch ( c2 ) {
         case blue:   return red;
         case red:    return blue;
         case yellow: return yellow;
         default:;
      }
      default:;
   }
   throw "Invalid";
}

static void show_complements() {
	for (auto i : { blue, red, yellow }) {
		for (auto j : { blue, red, yellow }) {
			std::cout << i << " + " << j << " -> " << (i + j) << std::endl;
		}
	}
}

static std::mutex output_mutex;

void print_header(std::initializer_list<color> colors) {
	std::lock_guard<std::mutex> lock(output_mutex);
	std::cout << std::endl;
	for (auto i : colors)
		std::cout << " " << i;
	std::cout << std::endl;
}

struct message {
	const actor::actor<message>* chameneos;
	const color colour;
	message(const actor::actor<message>* _chameneos, color  _colour) : chameneos(_chameneos), colour(_colour) {}
};

static void broker_func(actor::receiver<message> recv, size_t meetings_count, size_t color_count) {
	for (auto i = 0u; i < meetings_count; ++i) {
		message left = recv.receive();
		message right = recv.receive();
		left.chameneos->send(right);
		right.chameneos->send(left);
	}
	for (auto i = 0u; i < color_count; ++i) {
		message last = recv.receive();
		last.chameneos->kill();
	}
}

static void cleanup_func(const actor::receiver<size_t> recv, size_t color_count, std::promise<void>& promise) {
	size_t summary = 0;
	for (auto i = 0u; i < color_count; ++i) {
		summary += recv.receive();
	}
	std::lock_guard<std::mutex> lock(output_mutex);
	std::cout << spell(summary) << std::endl;
	promise.set_value();
}

static void chameneos_func(const actor::receiver<message>& recv, color start_color, const actor::actor<message>& broker, const actor::actor<size_t> cleanup) {
	size_t meetings = 0, met_self = 0;
	color current = start_color;
	auto self = recv.self();
	try {
		while (1) {
			broker.send(message(&self, current));
			message tmp = recv.receive();
			meetings++;
			current = current + tmp.colour;
			if (tmp.chameneos == &self)
				met_self++;
		}
	}
	catch (const actor::death&) {
		std::lock_guard<std::mutex> lock(output_mutex);
		std::cout << meetings << " " << spell(met_self) << std::endl;
		cleanup.send(meetings);
	}
}

static void run(std::initializer_list<color> colors, size_t count) {
	print_header(colors);
	auto broker = actor::actor<message>::spawn(&broker_func, count, colors.size());
	std::promise<void> promise;
	auto future = promise.get_future();
	auto cleanup = actor::actor<size_t>::spawn(cleanup_func, colors.size(), std::ref(promise));
	std::vector<actor::actor<message>> chameneoses;
	for (auto color : colors) {
		chameneoses.push_back(actor::actor<message>::spawn(chameneos_func, color, broker, cleanup));
	}
	future.wait();
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
