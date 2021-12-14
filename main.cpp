
#define _CRT_SECURE_NO_WARNINGS

#ifdef _DEBUG 
#else
#include "mimalloc-new-delete.h"
#endif

#include <iostream>
#include <string>
#include <ctime>

#include "claujson.h"

void test() {
	simdjson::dom::parser _x, _y;

	auto x = _x.load("citylots.json");
	if (x.error() != simdjson::error_code::SUCCESS) {
		std::cout << x.error() << "\n";
	}
	auto y = _y.load("output.json");
	if (y.error() != simdjson::error_code::SUCCESS) {
		std::cout << y.error() << "\n";
	}

	auto& tape_a = _x.raw_tape();
	auto& str_a = _x.raw_string_buf();
	auto& tape_b = _y.raw_tape();
	auto& str_b = _y.raw_string_buf();
	int64_t len_a, len_b;


	size_t tape_idx = 0;
	uint64_t tape_val = tape_a[tape_idx];
	uint8_t type = uint8_t(tape_val >> 56);

	size_t how_many = 0;
	if (type == 'r') {
		how_many = size_t(tape_val & simdjson::internal::JSON_VALUE_MASK);
		len_a = how_many;
	}
	tape_val = tape_b[tape_idx];
	type = uint8_t(tape_val >> 56);
	if (type == 'r') {
		how_many = size_t(tape_val & simdjson::internal::JSON_VALUE_MASK);
		len_b = how_many;
	}

	std::cout << len_a << " " << len_b << "\n";
	if (len_a != len_b) {
		return;
	}
	for (size_t i = 0; i < len_a; ++i) {
		if (claujson::Convert(&tape_a[i], str_a) == claujson::Convert(&tape_b[i], str_b)) {
			//
		}
		else {
			std::cout << "error"; return;
		}
	}
	std::cout << "end\n";
}

int main(int argc, char* argv[])
{
	//test();

	claujson::UserType ut;
	
	int a = clock();
	std::vector<claujson::Block> blocks;
	auto x = claujson::Parse(argv[1], 0, &ut, blocks);
	claujson::PoolManager poolManager(x, std::move(blocks)); // using pool manager, add Item or remove
	int b = clock();
	std::cout << "total " << b - a << "ms\n";

	//claujson::LoadData::_save(std::cout, &ut);
	//claujson::LoadData::save("output.json", ut);

	//test2(ut);

	bool ok = nullptr != x;

	//ut.remove_all(poolManager);
	poolManager.Clear();
	
	return !ok;
}
