#include "./text_document.hpp"

#include <numeric>
#include <optional>
#include <random>
#include <iostream>
#include <cassert>

// single letter agent, for testing only
using Agent = char;
using Doc = GreenCRDT::TextDocument<Agent>;
using Op = GreenCRDT::TextDocument<Agent>::Op;
using ListType = Doc::ListType;

// maybe switch it up?
using Rng = std::minstd_rand;

// 10*7 -> 70 permutations , ggwp
//               | 1add | 1del | 1rep | 2add | 2del | 2rep | random add | random del | random rep | random
// empty doc     |      | 0    | 0    |      | 0    | 0    | x          | 0          | 0          |
// before 1 char |      |      |      |      |      |      |            |            |            |
// after 1 char  |      |      |      |      |      |      |            |            |            |
// before 2 char |      |      |      |      |      |      |            |            |            |
// in 2 char     |      |      |      |      |      |      |            |            |            |
// after 2 char  |      |      |      |      |      |      |            |            |            |
// random        |      |      |      |      |      |      |            |            |            |

static const std::vector<char> random_chars {
	'a', 'b', 'c', 'd', 'e',
	'f', 'g', 'h', 'i', 'j',
	'k', 'l', 'm', 'n', 'o',
	'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y',
	'z',

	'A', 'B', 'C', 'D', 'E',
	'F', 'G', 'H', 'I', 'J',
	'K', 'L', 'M', 'N', 'O',
	'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y',
	'Z',
};

std::ostream& operator<<(std::ostream& out, const std::optional<ListType::ListID>& id) {
	if (id.has_value()) {
		out << id.value().id << "-" << id.value().seq;
	} else {
		out << "null";
	}
	return out;
}

std::ostream& operator<<(std::ostream& out, const ListType::OpAdd& op) {
	out
		<< "{ id:" << op.id.id
		<< "-" << op.id.seq
		<< ", v:" << op.value
		<< ", l:" << op.parent_left
		<< ", r:" << op.parent_right
		<< " }"
	;
	return out;
}

// genX() changes doc, uses local agent

Op genAdd(Rng& rng, Doc& doc) {
	ListType::OpAdd op {
		{doc.local_agent, 0u},
		std::nullopt,
		std::nullopt,
		random_chars[rng()%random_chars.size()]
	};

	// first id is 0
	if (doc.state.last_seen_seq.count(doc.local_agent)) {
		op.id.seq = doc.state.last_seen_seq[doc.local_agent] + 1;
	}

	if (!doc.state.list.empty()) {
		// gen parents
		size_t li = rng()%(1+doc.state.list.size());
		if (li != doc.state.list.size()) { // nullopt
			op.parent_left = doc.state.list[li].id;
		}

		//size_t r_range = 1+doc.state.list.size();
		//if (li != doc.state.list.size()) {
			//r_range -= li+1;
		//}
		//size_t ri = rng()%r_range;
		//if (li != doc.state.list.size()) {
			//ri += li+1;
		//}
		//if (ri != doc.state.list.size()) { // nullopt
			//op.parent_right = doc.state.list[li].id;
		//}

		if (op.parent_left.has_value()) {
			if (doc.state.list.size() != li + 1) { // left is not last
				op.parent_right = doc.state.list[li+1].id;
			}
		} else {
			// left is before first, so right is first
			op.parent_right = doc.state.list.front().id;
		}
	} // else first char, both nullopt

	//std::cout << "op: " << op << "\n";

	{
		bool r = doc.state.add(op.id, op.value, op.parent_left, op.parent_right);
		if (!r) {
			std::cout << "op: " << op << "\n";
		}
		assert(r);
	}

	return op;
}

//genDel()
//genRep()
//genAddContRange()
//genDelContRange()
//genRepContRange()

//genRand()
//genRandRanges()
std::vector<Op> genRandAll(Rng& rng, Doc& doc) {
	switch (rng() % 1) {
		case 0:
			return {genAdd(rng, doc)};
	}

	return {};
}

void testEmptyDocAdds(size_t seed) {
	Rng rng(seed);

	Doc doc; // empty
	doc.local_agent = 'A';

	std::string changed_text;
	{
		// for modifying
		Doc doctmp = doc;

		const size_t loop_count = (rng() % 55)+1;
		for (size_t i = 0; i < loop_count; i++) {
			genAdd(rng, doctmp);
		}

		changed_text = doctmp.getText();
	}

	assert(doc.getText() != changed_text);

	std::cout << "changed_text: " << changed_text << "\n";

	doc.merge(changed_text);

	assert(doc.getText() == changed_text);
}

void test1CharDocAdds(size_t seed) {
	Rng rng(seed);

	Doc doc;
	doc.local_agent = 'A';

	doc.addText(std::nullopt, std::nullopt, "0");

	assert(doc.getText() == "0");

	std::string changed_text;
	{
		// for modifying
		Doc doctmp = doc;

		const size_t loop_count = (rng() % 13)+1;
		for (size_t i = 0; i < loop_count; i++) {
			genAdd(rng, doctmp);
		}

		changed_text = doctmp.getText();
	}

	assert(doc.getText() != changed_text);

	std::cout << "changed_text: " << changed_text << "\n";

	doc.merge(changed_text);

	assert(doc.getText() == changed_text);
}

int main(void) {
	{
		std::cout << "testEmptyDocAdds:\n";
		for (size_t i = 0; i < 1'000; i++) {
			std::cout << "i " << i << "\n";
			testEmptyDocAdds(1337+i);
			std::cout << std::string(40, '-') << "\n";
		}
		std::cout << std::string(40, '=') << "\n";
	}

	{
		std::cout << "test1CharDocAdds:\n";
		for (size_t i = 0; i < 1'000; i++) {
			std::cout << "i " << i << "\n";
			test1CharDocAdds(1337+i);
			std::cout << std::string(40, '-') << "\n";
		}
		std::cout << std::string(40, '=') << "\n";
	}

	return 0;
}

