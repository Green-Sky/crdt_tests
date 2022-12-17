#include <crdt/text_document.hpp>

#include <numeric>
#include <optional>
#include <random>
#include <iostream>
#include <cassert>
#include <variant>

// single letter agent, for testing only
using Agent = char;
using Doc = GreenCRDT::TextDocument<Agent>;
using Op = GreenCRDT::TextDocument<Agent>::Op;
using ListType = Doc::ListType;

// maybe switch it up?
//using Rng = std::minstd_rand;
//using Rng = std::mt19937;
using Rng = std::ranlux24_base;

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

Op genDel(Rng& rng, Doc& doc) {
	if (doc.state.doc_size == 0) {
		assert(false && "empty doc");
		return {}; // empty
	}

	doc.state.verify();

	ListType::OpDel op{};

	// search for undelted entry
	size_t idx = rng()%doc.state.list.size();
	bool found = false;
	for (size_t attempts = 0; attempts <= doc.state.list.size(); attempts++) {
		if (doc.state.list[idx].value.has_value()) {
			op.id = doc.state.list[idx].id;
			found = true;
			break;
		}
		idx = (idx+1) % doc.state.list.size();
	}

	assert(found);

	{
		auto size_pre = doc.state.doc_size;
		bool r = doc.state.del(op.id);
		assert(r);
		assert(size_pre-1 == doc.state.doc_size);
		size_t actual_size = 0;
		for (const auto& it : doc.state.list) {
			if (it.value.has_value()) {
				actual_size++;
			}
		}
		assert(doc.state.doc_size == actual_size);
	}

	return op;
}

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

	Doc otherdoc = doc;
	assert(doc.getText().size() == doc.state.doc_size);
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.doc_size);

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
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

		const size_t loop_count = (rng() % 4)+1;
		for (size_t i = 0; i < loop_count; i++) {
			genAdd(rng, doctmp);
		}

		changed_text = doctmp.getText();
	}

	assert(doc.getText() != changed_text);

	std::cout << "text: " << doc.getText() << "\n";
	std::cout << "changed_text: " << changed_text << "\n";

	Doc otherdoc = doc;
	assert(doc.getText().size() == doc.state.doc_size);
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.doc_size);

	std::cout << "text after merge: " << doc.getText() << "\n";

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
}

void test1CharDocDels(size_t seed) {
	Rng rng(seed);

	Doc doc;
	doc.local_agent = 'A';

	assert(doc.getText().size() == doc.state.doc_size);
	doc.addText(std::nullopt, std::nullopt, "0123");
	assert(doc.getText().size() == doc.state.doc_size);

	assert(doc.getText() == "0123");

	std::string changed_text;
	{
		// for modifying
		Doc doctmp = doc;

		const size_t loop_count = (rng() % 4)+1;
		std::cout << "going to  delete: "  << loop_count << "\n";
		for (size_t i = 0; i < loop_count; i++) {
			genDel(rng, doctmp);
		}

		changed_text = doctmp.getText();
		assert(doctmp.getText().size() == doctmp.state.doc_size);

		if (loop_count == doc.state.doc_size) {
			assert(doctmp.state.doc_size == 0);
			assert(changed_text.size() == 0);
		}
	}

	assert(doc.getText() != changed_text);

	std::cout << "text: " << doc.getText() << "\n";
	std::cout << "changed_text: " << changed_text << "\n";

	Doc otherdoc = doc;
	assert(doc.getText().size() == doc.state.doc_size);
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.doc_size);

	std::cout << "text after merge: " << doc.getText() << "\n";

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
}

void test2CharDocAdds(size_t seed) {
	Rng rng(seed);

	Doc doc;
	doc.local_agent = 'A';

	assert(doc.getText().size() == doc.state.doc_size);
	doc.addText(std::nullopt, std::nullopt, "012345");
	assert(doc.getText().size() == doc.state.doc_size);

	assert(doc.getText() == "012345");

	std::string changed_text;
	{
		// for modifying
		Doc doctmp = doc;

		const size_t loop_count = (rng() % 6)+1;
		for (size_t i = 0; i < loop_count; i++) {
			genAdd(rng, doctmp);
		}

		changed_text = doctmp.getText();
	}

	assert(doc.getText() != changed_text);

	std::cout << "text: " << doc.getText() << "\n";
	std::cout << "changed_text: " << changed_text << "\n";

	Doc otherdoc = doc;
	assert(doc.getText().size() == doc.state.doc_size);
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.doc_size);

	std::cout << "text after merge: " << doc.getText() << "\n";

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
}

void testChange1(size_t seed) {
	Rng rng(seed);

	Doc doc;
	doc.local_agent = 'A';

	assert(doc.getText().size() == doc.state.doc_size);
	doc.addText(std::nullopt, std::nullopt, "012345");
	assert(doc.getText().size() == doc.state.doc_size);

	assert(doc.getText() == "012345");

	std::string changed_text;
	{
		// for modifying
		Doc doctmp = doc;

		{ // dels
			const size_t loop_count = (rng() % 6)+1;
			for (size_t i = 0; i < loop_count; i++) {
				genDel(rng, doctmp);
			}
		}

		{ // adds
			const size_t loop_count = (rng() % 6)+1;
			for (size_t i = 0; i < loop_count; i++) {
				genAdd(rng, doctmp);
			}
		}

		changed_text = doctmp.getText();
	}

	assert(doc.getText() != changed_text);

	std::cout << "text: " << doc.getText() << "\n";
	std::cout << "changed_text: " << changed_text << "\n";

	Doc otherdoc = doc;
	assert(doc.getText().size() == doc.state.doc_size);
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.doc_size);

	std::cout << "text after merge: " << doc.getText() << "\n";

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
}

void testBugSame(void) {
	Doc doc;
	doc.local_agent = 'A';

	std::string_view new_text1{"a"};
	doc.merge(new_text1);
	assert(doc.getText() == new_text1);

	std::string_view new_text2{"aa"};
	doc.merge(new_text2);
	assert(doc.getText() == new_text2);
}

void testBugDoubleDel(void) {
	Doc doc;
	doc.local_agent = 'A';

	{
		std::string_view new_text{"a"};
		doc.merge(new_text);
		assert(doc.getText() == new_text);
	}

	{
		std::string_view new_text{""};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
		assert(std::holds_alternative<ListType::OpDel>(ops.front()));
		assert(std::get<ListType::OpDel>(ops.front()).id.seq == 0);
	}

	{
		std::string_view new_text{""};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 0);
	}
}

void testBugSameDel(void) {
	Doc doc;
	doc.local_agent = 'A';

	{
		std::string_view new_text{"a"};
		doc.merge(new_text);
		assert(doc.getText() == new_text);
	}

	{
		std::string_view new_text{"aa"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
	}

	{
		std::string_view new_text{"a"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
	}
}

void testBugSameDel2(void) {
	Doc doc;
	doc.local_agent = 'A';

	{
		std::string_view new_text{"a"};
		doc.merge(new_text);
		assert(doc.getText() == new_text);
	}

	{
		std::string_view new_text{"aa"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
	}

	{
		std::string_view new_text{"aaa"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
	}

	{
		std::string_view new_text{"aa"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
	}

	{
		std::string_view new_text{"a"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
	}
}

int main(void) {
	const size_t loops = 1'000;
	{
		std::cout << "testEmptyDocAdds:\n";
		for (size_t i = 0; i < loops; i++) {
			std::cout << "i " << i << "\n";
			testEmptyDocAdds(1337+i);
			std::cout << std::string(40, '-') << "\n";
		}
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "test1CharDocAdds:\n";
		for (size_t i = 0; i < loops; i++) {
			std::cout << "i " << i << "\n";
			test1CharDocAdds(1337+i);
			std::cout << std::string(40, '-') << "\n";
		}
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "test1CharDocDels:\n";
		for (size_t i = 0; i < loops; i++) {
			std::cout << "i " << i << "\n";
			test1CharDocDels(1337+i);
			std::cout << std::string(40, '-') << "\n";
		}
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "test2CharDocAdds:\n";
		for (size_t i = 0; i < loops; i++) {
			std::cout << "i " << i << "\n";
			test2CharDocAdds(1337+i);
			std::cout << std::string(40, '-') << "\n";
		}
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "testChange1:\n";
		for (size_t i = 0; i < loops; i++) {
			std::cout << "i " << i << "\n";
			testChange1(1337+i);
			std::cout << std::string(40, '-') << "\n";
		}
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "testBugSame:\n";
		testBugSame();
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "testBugDoubleDel:\n";
		testBugDoubleDel();
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "testBugSameDel:\n";
		testBugSameDel();
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "testBugSameDel2:\n";
		testBugSameDel2();
	}

	return 0;
}

