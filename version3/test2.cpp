#include <green_crdt/v3/text_document.hpp>

#include <numeric>
#include <optional>
#include <random>
#include <iostream>
#include <cassert>
#include <variant>

// single letter agent, for testing only
using Agent = std::string;
using Doc = GreenCRDT::V3::TextDocument<Agent>;
using Op = Doc::Op;
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

std::ostream& operator<<(std::ostream& out, const Doc::OpAdd& op) {
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
	Doc::OpAdd op {
		{doc.local_actor, 0u},
		std::nullopt,
		std::nullopt,
		random_chars[rng()%random_chars.size()]
	};

	// TODO: move to list
	// make sure actor index exists
	if (!doc.state.findActor(doc.local_actor).has_value()) {
		doc.state._actors.push_back(doc.local_actor);
	}

	// first id is 0
	if (doc.state._last_seen_seq.count(doc.state.findActor(doc.local_actor).value())) {
		op.id.seq = doc.state._last_seen_seq[doc.state.findActor(doc.local_actor).value()] + 1;
	}

	if (!doc.state.empty()) {
		// gen parents
		size_t li = rng()%(1+doc.state.size());
		if (li != doc.state.size()) { // nullopt
			op.parent_left = doc.state.getID(li);
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
			if (doc.state.size() != li + 1) { // left is not last
				op.parent_right = doc.state.getID(li+1);
			}
		} else {
			// left is before first, so right is first
			op.parent_right = doc.state.getID(0);
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
	if (doc.state.getDocSize() == 0) {
		assert(false && "empty doc");
		return {}; // empty
	}

	doc.state.verify();

	Doc::OpDel op{};

	// search for undelted entry
	size_t idx = rng()%doc.state.size();
	bool found = false;
	for (size_t attempts = 0; attempts <= doc.state.size(); attempts++) {
		//if (doc.state.list[idx].value.has_value()) {
		if (doc.state.getValue(idx).has_value()) {
			op.id = doc.state.getID(idx);
			found = true;
			break;
		}
		idx = (idx+1) % doc.state.size();
	}

	assert(found);

	{
		auto size_pre = doc.state.getDocSize();
		bool r = doc.state.del(op.id);
		assert(r);
		assert(size_pre-1 == doc.state.getDocSize());
		assert(doc.state.verify());
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
	doc.local_actor = 'A';

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
	assert(doc.getText().size() == doc.state.getDocSize());
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.getDocSize());

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
}

void test1CharDocAdds(size_t seed) {
	Rng rng(seed);

	Doc doc;
	doc.local_actor = 'A';

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
	assert(doc.getText().size() == doc.state.getDocSize());
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.getDocSize());

	std::cout << "text after merge: " << doc.getText() << "\n";

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
}

void test1CharDocDels(size_t seed) {
	Rng rng(seed);

	Doc doc;
	doc.local_actor = 'A';

	assert(doc.getText().size() == doc.state.getDocSize());
	doc.addText(std::nullopt, std::nullopt, "0123");
	assert(doc.getText().size() == doc.state.getDocSize());

	assert(doc.getText() == "0123");

	std::string changed_text;
	{
		// for modifying
		Doc doctmp = doc;

		const size_t loop_count = (rng() % 4)+1;
		std::cout << "going to delete: "  << loop_count << "\n";
		for (size_t i = 0; i < loop_count; i++) {
			genDel(rng, doctmp);
		}

		changed_text = doctmp.getText();
		assert(doctmp.getText().size() == doctmp.state.getDocSize());

		if (loop_count == doc.state.getDocSize()) {
			assert(doctmp.state.getDocSize() == 0);
			assert(changed_text.size() == 0);
		}
	}

	assert(doc.getText() != changed_text);

	std::cout << "text: " << doc.getText() << "\n";
	std::cout << "changed_text: " << changed_text << "\n";

	Doc otherdoc = doc;
	assert(doc.getText().size() == doc.state.getDocSize());
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.getDocSize());

	std::cout << "text after merge: " << doc.getText() << "\n";

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
}

void test2CharDocAdds(size_t seed) {
	Rng rng(seed);

	Doc doc;
	doc.local_actor = 'A';

	assert(doc.getText().size() == doc.state.getDocSize());
	doc.addText(std::nullopt, std::nullopt, "012345");
	assert(doc.getText().size() == doc.state.getDocSize());

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
	assert(doc.getText().size() == doc.state.getDocSize());
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.getDocSize());

	std::cout << "text after merge: " << doc.getText() << "\n";

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
}

void testChange1(size_t seed) {
	Rng rng(seed);

	Doc doc;
	doc.local_actor = 'A';

	assert(doc.getText().size() == doc.state.getDocSize());
	doc.addText(std::nullopt, std::nullopt, "012345");
	assert(doc.getText().size() == doc.state.getDocSize());

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
	assert(doc.getText().size() == doc.state.getDocSize());
	const auto merge_ops = doc.merge(changed_text);
	assert(doc.getText().size() == doc.state.getDocSize());

	std::cout << "text after merge: " << doc.getText() << "\n";

	assert(doc.getText() == changed_text);

	assert(otherdoc.apply(merge_ops));
	assert(doc.getText() == otherdoc.getText());
}

void testBugSame(void) {
	Doc doc;
	doc.local_actor = 'A';

	std::string_view new_text1{"a"};
	doc.merge(new_text1);
	assert(doc.getText() == new_text1);

	std::string_view new_text2{"aa"};
	doc.merge(new_text2);
	assert(doc.getText() == new_text2);
}

void testBugDoubleDel(void) {
	Doc doc;
	doc.local_actor = 'A';

	{
		std::string_view new_text{"a"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
	}

	{
		std::string_view new_text{""};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
		assert(std::holds_alternative<Doc::OpDel>(ops.front()));
		assert(std::get<Doc::OpDel>(ops.front()).id.seq == 0);
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
	doc.local_actor = 'A';

	{
		std::string_view new_text{"a"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
	}

	{
		std::string_view new_text{"aa"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
	}

	{
		std::string_view new_text{"a"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
	}
}

void testBugSameDel2(void) {
	Doc doc;
	doc.local_actor = 'A';

	{
		std::string_view new_text{"a"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
	}

	{
		std::string_view new_text{"aa"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
	}

	{
		std::string_view new_text{"aaa"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
	}

	{
		std::string_view new_text{"aa"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
	}

	{
		std::string_view new_text{"a"};
		const auto ops = doc.merge(new_text);
		assert(doc.getText() == new_text);
		assert(ops.size() == 1);
	}
}

void testMulti1(void) {
	Doc docA;
	docA.local_actor = 'A';

	Doc docB;
	docB.local_actor = 'B';

	// state A
	{
		std::string_view new_text{"iiiiiii"};
		const auto ops = docA.merge(new_text);
		assert(docA.getText() == new_text);

		assert(docB.apply(ops));

		assert(docB.getText() == new_text);
		assert(docB.state.getDocSize() == docA.state.getDocSize());
		assert(docB.state.size() == docA.state.size());
	}

	// now B inserts b
	{
		std::string_view new_text{"iiibiiii"};
		const auto ops = docB.merge(new_text);
		assert(docB.getText() == new_text);
		assert(ops.size() == 1); // 1 new inserted char, nothing to delete

		assert(docA.apply(ops));

		assert(docA.getText() == new_text);
	}
}

void testPaste1(void) {
	Doc docA;
	docA.local_actor = 'A';

	{
		std::string_view new_text{"iiiiiii"};
		const auto ops = docA.merge(new_text);
		assert(ops.size() == 7);
		assert(docA.getText() == new_text);
	}

	{
		std::string_view new_text{"iiiiiii\n"};
		const auto ops = docA.merge(new_text);
		assert(ops.size() == 1);
		assert(docA.getText() == new_text);
	}

	{
		std::string_view new_text{"iiiiiii\niiiiiii"};
		const auto ops = docA.merge(new_text);
		assert(ops.size() == 7);
		assert(docA.getText() == new_text);
	}
}

void testPaste2(void) {
	Doc docA;
	docA.local_actor = 'A';

	{
		std::string_view new_text{"aiiiiib"};
		const auto ops = docA.merge(new_text);
		assert(ops.size() == 7);
		assert(docA.getText() == new_text);
	}

	{
		std::string_view new_text{"aiiiiib\n"};
		const auto ops = docA.merge(new_text);
		assert(ops.size() == 1);
		assert(docA.getText() == new_text);
	}

	{
		std::string_view new_text{"aiiiiib\naiiiiib"};
		const auto ops = docA.merge(new_text);
		assert(ops.size() == 7);
		assert(docA.getText() == new_text);
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

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "testMulti1:\n";
		testMulti1();
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "testPaste1:\n";
		testPaste1();
	}

	std::cout << std::string(40, '=') << "\n";

	{
		std::cout << "testPaste2:\n";
		testPaste2();
	}

	return 0;
}

