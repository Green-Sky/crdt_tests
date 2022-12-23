#define EXTRA_ASSERTS 1
#include <green_crdt/v4/list.hpp>

#include <numeric>
#include <random>
#include <iostream>
#include <cassert>
#include <string_view>
#include <vector>

// single letter actor, for testing only
using Actor = char;
using ListType = GreenCRDT::V4::List<char, Actor>;

namespace std {
bool operator==(const std::vector<char>& lhs, const std::string_view& rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}

	for (size_t i = 0; i < rhs.size(); i++) {
		if (lhs[i] != rhs[i]) {
			return false;
		}
	}

	return true;
}
} // namespace std

void testSingle1(void) {
	ListType list;

	assert(list.add({'A', 0}, 'a', std::nullopt, std::nullopt));
	assert(list.add({'A', 1}, 'b', ListType::ListID{'A', 0u}, std::nullopt));

	assert(list.getArray() == "ab");
}


void testConcurrent1(void) {
	// agent_a < agent_b

	// concurrent insert of first element
	{ // variant 1, a then b
		ListType list;
		assert(list.add({'A', 0}, 'a', std::nullopt, std::nullopt));
		assert(list.add({'B', 0}, 'b', std::nullopt, std::nullopt));

		assert(list.getArray() == "ab");
	}
	{ // variant 2, b then a
		ListType list;
		assert(list.add({'B', 0}, 'b', std::nullopt, std::nullopt));
		assert(list.add({'A', 0}, 'a', std::nullopt, std::nullopt));

		assert(list.getArray() == "ab");
	}
}

struct AddOp {
	ListType::ListID id;
	char value;
	std::optional<ListType::ListID> parent_left;
	std::optional<ListType::ListID> parent_right;
};

void randomAddPermutations(const std::vector<AddOp>& ops, const std::string& expected) {
	// TODO: more then 1k?
	for (size_t i = 0; i < 1000; i++) {
		std::minstd_rand rng(1337 + i);
		std::vector<size_t> ops_todo(ops.size());
		std::iota(ops_todo.begin(), ops_todo.end(), 0u);

		size_t attempts {0};

		ListType list;
		do {
			size_t idx = rng() % ops_todo.size();

			if (list.add(ops[ops_todo[idx]].id, ops[ops_todo[idx]].value, ops[ops_todo[idx]].parent_left, ops[ops_todo[idx]].parent_right)) {
				// only remove if it was possible -> returned true;
				ops_todo.erase(ops_todo.begin()+idx);
			}

			attempts++;
			assert(attempts < 10'000); // in case we run into an endless loop
		} while (!ops_todo.empty());

		assert(list.getArray() == expected);
	}
}

void testInterleave1(void) {
	const std::vector<AddOp> ops {
		{{'A', 0u}, 'a', std::nullopt, std::nullopt},
		{{'A', 1u}, 'a', ListType::ListID{'A', 0u}, std::nullopt},
		{{'A', 2u}, 'a', ListType::ListID{'A', 1u}, std::nullopt},
		{{'B', 0u}, 'b', std::nullopt, std::nullopt},
		{{'B', 1u}, 'b', ListType::ListID{'B', 0u}, std::nullopt},
		{{'B', 2u}, 'b', ListType::ListID{'B', 1u}, std::nullopt},
	};

	randomAddPermutations(ops, "aaabbb");
}

void testInterleave2(void) {
	const std::vector<AddOp> ops {
		{{'A', 0u}, 'a', std::nullopt, std::nullopt},
		{{'A', 1u}, 'a', std::nullopt, ListType::ListID{'A', 0u}},
		{{'A', 2u}, 'a', std::nullopt, ListType::ListID{'A', 1u}},
		{{'B', 0u}, 'b', std::nullopt, std::nullopt},
		{{'B', 1u}, 'b', std::nullopt, ListType::ListID{'B', 0u}},
		{{'B', 2u}, 'b', std::nullopt, ListType::ListID{'B', 1u}},
	};

	randomAddPermutations(ops, "aaabbb");
}

void testConcurrent2(void) {
	const std::vector<AddOp> ops {
		{{'A', 0u}, 'a', std::nullopt, std::nullopt},
		{{'C', 0u}, 'c', std::nullopt, std::nullopt},
		{{'B', 0u}, 'b', std::nullopt, std::nullopt},
		{{'D', 0u}, 'd', ListType::ListID{'A', 0u}, ListType::ListID{'C', 0u}},
	};

	randomAddPermutations(ops, "adbc");
}

void testMain1(void) {
	ListType list;

	static_assert('0' < '1');

	const std::vector<AddOp> a0_ops {
		{{'0', 0u}, 'a', std::nullopt, std::nullopt},
		{{'0', 1u}, 'b', ListType::ListID{'0', 0u}, std::nullopt},
		{{'0', 2u}, 'c', ListType::ListID{'0', 1u}, std::nullopt},
		{{'0', 3u}, 'd', ListType::ListID{'0', 1u}, ListType::ListID{'0', 2u}},
	};

	const std::vector<AddOp> a1_ops {
		// knows of a0 up to {a0, 1}
		{{'1', 0u}, 'z', ListType::ListID{'0', 0u}, ListType::ListID{'0', 1u}},
		{{'1', 1u}, 'y', ListType::ListID{'0', 1u}, std::nullopt},
	};

	{ // the ez, in order stuff
		// a0 insert first char, 'a', since its the first, we dont have any parents
		assert(list.add(a0_ops[0].id, a0_ops[0].value, a0_ops[0].parent_left, a0_ops[0].parent_right));
		assert(list.getArray() == "a");

		// a0 insert secound char, 'b' after 'a', no parents to right
		assert(list.add(a0_ops[1].id, a0_ops[1].value, a0_ops[1].parent_left, a0_ops[1].parent_right));
		assert(list.getArray() == "ab");

		// a0 insert 'c' after 'b', no parents to right
		assert(list.add(a0_ops[2].id, a0_ops[2].value, a0_ops[2].parent_left, a0_ops[2].parent_right));
		assert(list.getArray() == "abc");

		// a0 insert 'd' after 'b', 'c' parent right
		assert(list.add(a0_ops[3].id, a0_ops[3].value, a0_ops[3].parent_left, a0_ops[3].parent_right));
		assert(list.getArray() == "abdc");

		// a1 insert 'z' after 'a', 'b' parent right
		assert(list.add(a1_ops[0].id, a1_ops[0].value, a1_ops[0].parent_left, a1_ops[0].parent_right));
		assert(list.getArray() == "azbdc");
	}

	std::cout << "done with ez\n";

	{ // a1 was not uptodate only had 0,1 of a0
		// a1 insert 'y' after 'b', no parent right
		assert(list.add(a1_ops[1].id, a1_ops[1].value, a1_ops[1].parent_left, a1_ops[1].parent_right));
		assert(list.getArray() == "azbdcy");
	}

	std::cout << "\ndoc size (with tombstones): " << list._list.size() << "\n";
	std::cout << "\ndoc size: " << list.getDocSize() << "\n";
	std::cout << "doc text:\n";

	const auto tmp_array = list.getArray();
	std::cout << std::string_view(tmp_array.data(), tmp_array.size()) << "\n";
}

int main(void) {
	std::cout << "testSingle1:\n";
	testSingle1();
	std::cout << std::string(40, '-') << "\n";

	std::cout << "testConcurrent1:\n";
	testConcurrent1();
	std::cout << std::string(40, '-') << "\n";

	std::cout << "testInterleave1:\n";
	testInterleave1();
	std::cout << std::string(40, '-') << "\n";

	std::cout << "testInterleave2:\n";
	testInterleave2();
	std::cout << std::string(40, '-') << "\n";

	std::cout << "testConcurrent2:\n";
	testConcurrent2();
	std::cout << std::string(40, '-') << "\n";

	std::cout << "testMain1:\n";
	testMain1();
	std::cout << std::string(40, '-') << "\n";

	return 0;
}

