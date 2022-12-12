#include <array>
#include <list>
#include <optional>
#include <vector>
#include <map>

#include <iostream>
#include <cassert>

namespace GreenCRDT {
	namespace detail {
		uint8_t nib_from_hex(char c) {
			assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));

			if (c >= '0' && c <= '9') {
				return static_cast<uint8_t>(c) - '0';
			} else if (c >= 'a' && c <= 'f') {
				return (static_cast<uint8_t>(c) - 'a') + 10u;
			} else {
				return 0u;
			}
		}
	} // detail

	struct ID {
		// something like a toxid
		// for testing its martins paper trace
		std::array<uint8_t, 32> data;

		bool operator<(const ID& rhs) const {
			return data < rhs.data;
		}

		bool operator==(const ID& rhs) const {
			return data == rhs.data;
		}

		bool operator!=(const ID& rhs) const {
			return data != rhs.data;
		}

		static ID fromHex(const std::string_view str) {
			assert(str.size() == 32*2);
			ID tmp;

			for (size_t i = 0; i < tmp.data.size(); i++) {
				tmp.data[i] = detail::nib_from_hex(str[i*2]) << 4 | detail::nib_from_hex(str[i*2+1]);
			}

			return tmp;
		}
	};

	using Agent = ID;

	struct ListID {
		Agent id;
		uint64_t seq{0}; // strictly increasing for that agent

		bool operator<(const ListID& rhs) const {
			if (seq < rhs.seq) {
				return true;
			} else if (seq > rhs.seq) {
				return false;
			} else { // ==
				return id < rhs.id;
			}
		}

		bool operator==(const ListID& rhs) const {
			return seq == rhs.seq && id == rhs.id;
		}

		bool operator!=(const ListID& rhs) const {
			return seq != rhs.seq || id != rhs.id;
		}

		static ListID fromStr(const std::string_view str) {
			assert(str.size() >= 32*2);

			size_t at_pos = str.find_first_of('@');
			const auto seq_sv = str.substr(0, at_pos);
			const auto agent_sv = str.substr(at_pos+1);

			assert(seq_sv.size() != 0);
			assert(agent_sv.size() == 32*2);

			uint64_t tmp_seq {0};
			for (size_t i = 0; i < seq_sv.size(); i++) {
				assert(seq_sv[i] >= '0' && seq_sv[i] <= '9');
				tmp_seq *= 10;
				tmp_seq += seq_sv[i] - '0';
			}

			return {GreenCRDT::Agent::fromHex(agent_sv), tmp_seq};
		}
	};

	template<typename ValueType>
	struct List {
		// TODO: replace with SoA
		struct Entry {
			ListID id;

			// Yjs
			std::optional<ListID> parent_left;
			std::optional<ListID> parent_right;

			// might be deleted (yes, *sigh*, crtds need tombstones)
			std::optional<ValueType> value;
		};

		// linked list for now
		std::list<Entry> list;

		// number of not deleted entries
		size_t doc_size {0};

		std::map<Agent, uint64_t> last_seen_seq;

		auto find(const ListID& list_id) {
			auto it = list.begin();
			for (; it != list.end(); it++) {
				if (it->id == list_id) {
					break;
				}
			}

			return it;
		}

		// returns false if parent not found. (missing OPs)
		// returns false if missing operations
		// base on YjsMod https://github.com/josephg/reference-crdts/blob/9f4f9c3a97b497e2df8ae4473d1e521d3c3bf2d2/crdts.ts#L293-L348
		// which is a modified Yjs algo
		bool add(const ListID& list_id, const ValueType& value, const std::optional<ListID>& parent_left, const std::optional<ListID>& parent_right) {
			// check agent op order
			if (!last_seen_seq.count(list_id.id)) {
				// we dont know this agent yet, first seq needs to be 0
				if (list_id.seq != 0) {
					return false;
				}
			} else {
				// making sure we dont skip operations by that agent
				if (list_id.seq != last_seen_seq.at(list_id.id) + 1) {
					return false;
				}
			}

			if (list.empty()) {
				if (parent_left.has_value() || parent_right.has_value()) {
					// empty, missing parents
					return false;
				}
					list.emplace(list.begin(), Entry{
						list_id,
						parent_left,
						parent_right,
						value
					});

				// insert parentless into empty doc
			} else {
				// find left
				auto left_it = list.begin();
				if (parent_left.has_value()) {
					left_it = find(parent_left.value());
					if (left_it == list.end()) {
						// missing parent left
						return false;
					}

					left_it++; // we insert before the it, so we need to go past the left parent
				}
				// left_it is not at the first potential insert position

				// find right
				auto right_it = list.end();
				if (parent_right.has_value()) {
					right_it = find(parent_right.value());
					if (right_it == list.end()) {
						// missing parent right
						return false;
					}
				}

				do {
					// if rightmost / end of doc, insert
					if (left_it == right_it) {
						break;
					}
				} while (true);


				list.emplace(left_it, Entry{
					list_id,
					parent_left,
					parent_right,
					value
				});
			}

			doc_size++;
			last_seen_seq[list_id.id] = list_id.seq;
			return true;
		}

		// returns false if not found
		bool del(const ListID& id) {
			auto it = list.begin();
			for (; it != list.end(); it++) {
				if (it->id == id) {
					it->value = std::nullopt;

					assert(doc_size > 0);
					doc_size--;
					return true;
				}
			}

			// not found
			return false;
		}
	};

	struct TextDocument {
		struct Cursor {
			Agent who;
			ListID pos;
		};

		// TODO: determine if char is the best
		List<char> state;

		friend std::ostream& operator<<(std::ostream& out, const TextDocument& doc) {
			// simple print
			for (const auto& it : doc.state.list) {
				if (it.value) {
					out << it.value.value();
				}
			}

			return out;
		}

		std::string getText(void) {
			std::string text;

			for (const auto& it : state.list) {
				if (it.value) {
					text += it.value.value();
				}
			}

			return text;
		}
	};
} // GreenCRDT

void testSingle1(void) {
	GreenCRDT::TextDocument doc;
	const auto agent_a = GreenCRDT::Agent::fromHex("0a00000000000000000000000000000000000000000000000000000000000000");

	doc.state.add({agent_a, 0}, 'a', std::nullopt, std::nullopt);
	doc.state.add({agent_a, 1}, 'b', GreenCRDT::ListID{agent_a, 0u}, std::nullopt);

	assert(doc.getText() == "ab");
}

void testConcurrent1(void) {
	const auto agent_a = GreenCRDT::Agent::fromHex("0a00000000000000000000000000000000000000000000000000000000000000");
	const auto agent_b = GreenCRDT::Agent::fromHex("0b00000000000000000000000000000000000000000000000000000000000000");
	// agent_a < agent_b

	// concurrent insert of first element
	{ // variant 1, a then b
		GreenCRDT::TextDocument doc;
		doc.state.add({agent_a, 0}, 'a', std::nullopt, std::nullopt);
		doc.state.add({agent_b, 0}, 'b', std::nullopt, std::nullopt);

		assert(doc.getText() == "ab");
	}
	{ // variant 2, b then a
		GreenCRDT::TextDocument doc;
		doc.state.add({agent_b, 0}, 'b', std::nullopt, std::nullopt);
		doc.state.add({agent_a, 0}, 'a', std::nullopt, std::nullopt);

		assert(doc.getText() == "ab");
	}
}

void testMain1(void) {
	GreenCRDT::TextDocument doc;

	const GreenCRDT::Agent agent0 = GreenCRDT::Agent::fromHex("9654bd415cfac9ee6aa9f55bf1024f840afdbb1f37237673c928491a91ff7783");
	uint64_t agent0_seq {0};
	const GreenCRDT::Agent agent1 = GreenCRDT::Agent::fromHex("0100000000000000000000000000000000000000000000000000000000000000");
	uint64_t agent1_seq {0};

	struct AddOp {
		GreenCRDT::ListID id;
		char value;
		std::optional<GreenCRDT::ListID> parent_left;
		std::optional<GreenCRDT::ListID> parent_right;
	};

	const std::vector<AddOp> a0_ops {
		{{agent0, agent0_seq++}, 'a', std::nullopt, std::nullopt},
		{{agent0, agent0_seq++}, 'b', GreenCRDT::ListID{agent0, 0u}, std::nullopt},
		{{agent0, agent0_seq++}, 'c', GreenCRDT::ListID{agent0, 1u}, std::nullopt},
		{{agent0, agent0_seq++}, 'd', GreenCRDT::ListID{agent0, 1u}, GreenCRDT::ListID{agent0, 2u}},
	};

	const std::vector<AddOp> a1_ops {
		// knows of a0 up to {a0, 1}
		{{agent1, agent1_seq++}, 'z', GreenCRDT::ListID{agent0, 0u}, GreenCRDT::ListID{agent0, 1u}},
		{{agent1, agent1_seq++}, 'y', GreenCRDT::ListID{agent0, 1u}, std::nullopt},
	};

	{ // the ez, in order stuff
		// a0 insert first char, 'a', since its the first, we dont have any parents
		doc.state.add(a0_ops[0].id, a0_ops[0].value, a0_ops[0].parent_left, a0_ops[0].parent_right);
		assert(doc.getText() == "a");

		// a0 insert secound char, 'b' after 'a', no parents to right
		doc.state.add(a0_ops[1].id, a0_ops[1].value, a0_ops[1].parent_left, a0_ops[1].parent_right);
		assert(doc.getText() == "ab");

		// a0 insert 'c' after 'b', no parents to right
		doc.state.add(a0_ops[2].id, a0_ops[2].value, a0_ops[2].parent_left, a0_ops[2].parent_right);
		assert(doc.getText() == "abc");

		// a0 insert 'd' after 'b', 'c' parent right
		doc.state.add(a0_ops[3].id, a0_ops[3].value, a0_ops[3].parent_left, a0_ops[3].parent_right);
		assert(doc.getText() == "abdc");

		// a1 insert 'z' after 'a', 'b' parent right
		doc.state.add(a1_ops[0].id, a1_ops[0].value, a1_ops[0].parent_left, a1_ops[0].parent_right);
		assert(doc.getText() == "azbdc");
	}

	std::cout << "done with ez\n";

	{ // a1 was not uptodate only had 0,1 of a0
		// a1 insert 'y' after 'b', no parent right
		doc.state.add(a1_ops[1].id, a1_ops[1].value, a1_ops[1].parent_left, a1_ops[1].parent_right);
		assert(doc.getText() == "azbydc");
	}

	std::cout << "\ndoc size (with tombstones): " << doc.state.list.size() << "\n";
	std::cout << "\ndoc size: " << doc.state.doc_size << "\n";
	std::cout << "doc text:\n";

	// simple print
	std::cout << doc << "\n";
}

int main(void) {
	std::cout << "testSingle1:\n";
	testSingle1();
	std::cout << std::string(40, '-') << "\n";

	std::cout << "testConcurrent1:\n";
	testConcurrent1();
	std::cout << std::string(40, '-') << "\n";

	std::cout << "testMain1:\n";
	testMain1();
	std::cout << std::string(40, '-') << "\n";

	return 0;
}

