#include <array>
#include <list>
#include <optional>
#include <vector>
#include <map>

#include <numeric>
#include <random>
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
		std::vector<Entry> list;

		// number of not deleted entries
		size_t doc_size {0};

		std::map<Agent, uint64_t> last_seen_seq;

		auto findIt(const ListID& list_id) {
			auto it = list.begin();
			for (; it != list.end(); it++) {
				if (it->id == list_id) {
					break;
				}
			}

			return it;
		}

		std::optional<size_t> findIdx(const ListID& list_id) {
#if 0
			size_t i = 0;
			for (auto it = list.cbegin(); it != list.cend(); it++, i++) {
				if (it->id == list_id) {
					return i;
				}
			}
#endif
			for (size_t i = 0; i < list.size(); i++) {
				if (list[i].id == list_id) {
					return i;
				}
			}

			return std::nullopt;
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
				std::optional<size_t> left_idx = std::nullopt;
				size_t insert_idx = 0;
				if (parent_left.has_value()) {
					left_idx = findIdx(parent_left.value());
					if (!left_idx.has_value()) {
						// missing parent left
						return false;
					}

					// we insert before the it, so we need to go past the left parent
					insert_idx = left_idx.value() + 1;
				} // else insert_idx = 0

				// find right
				size_t right_idx = list.size();
				if (parent_right.has_value()) {
					auto tmp_right = findIdx(parent_right.value());
					if (!tmp_right.has_value()) {
						return false;
					}
					right_idx = tmp_right.value();
				}

				bool scanning {false};

				for(size_t i = insert_idx;; i++) {
					if (!scanning) {
						insert_idx = i;
					}
					// if right parent / end of doc, insert
					if (insert_idx == right_idx) {
						break;
					}
					// we ran past right o.o ?
					if (insert_idx == list.size()) {
						break;
					}

					const Entry& at_i = list[i];
					// parents left and right
					std::optional<size_t> i_left_idx {std::nullopt};
					if (at_i.parent_left.has_value()) {
						i_left_idx = findIdx(at_i.parent_left.value());
						if (!i_left_idx.has_value()) {
							assert(false && "item in list with unknown parent left!!");
							return false;
						}
					}

					// possibility map
					//
					//         | ir < r | ir == r       | ir > r
					// -------------------------------------
					// il < l  | insert | insert        | insert
					// il == l | ?      | agentfallback | ?
					// il > l  | skip   | skip          | skip

					if (i_left_idx < left_idx) {
						break;
					} else if (i_left_idx == left_idx) {
						// get i parent_right
						size_t i_right_idx = list.size();
						if (at_i.parent_right.has_value()) {
							auto tmp_right = findIdx(at_i.parent_right.value());
							if (!tmp_right.has_value()) {
								assert(false && "item in list with unknown parent right!!");
								return false;
							}
							i_right_idx = tmp_right.value();
						}

						if (i_right_idx < right_idx) {
							scanning = true;
						} else if (i_right_idx == right_idx) {
							// agent id tie breaker
							if (list_id.id < at_i.id.id) {
								break;
							} else {
								scanning = false;
							}
						} else { // i_right_idx > right_idx
							scanning = false;
						}
					} else { // il > l
						// do nothing
					}
				}

				list.emplace(list.begin() + insert_idx, Entry{
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

struct AddOp {
	GreenCRDT::ListID id;
	char value;
	std::optional<GreenCRDT::ListID> parent_left;
	std::optional<GreenCRDT::ListID> parent_right;
};

void randomAddPermutations(const std::vector<AddOp>& ops, const std::string& expected) {
	// TODO: more then 1k?
	for (size_t i = 0; i < 1000; i++) {
		std::minstd_rand rng(1337 + i);
		std::vector<size_t> ops_todo(ops.size());
		std::iota(ops_todo.begin(), ops_todo.end(), 0u);

		size_t attempts {0};

		GreenCRDT::TextDocument doc;
		do {
			size_t idx = rng() % ops_todo.size();

			if (doc.state.add(ops[ops_todo[idx]].id, ops[ops_todo[idx]].value, ops[ops_todo[idx]].parent_left, ops[ops_todo[idx]].parent_right)) {
				// only remove if it was possible -> returned true;
				ops_todo.erase(ops_todo.begin()+idx);
			}

			attempts++;
			assert(attempts < 10'000); // in case we run into an endless loop
		} while (!ops_todo.empty());

		assert(doc.getText() == expected);
	}
}

void testInterleave1(void) {
	const auto agent_a = GreenCRDT::Agent::fromHex("0a00000000000000000000000000000000000000000000000000000000000000");
	const auto agent_b = GreenCRDT::Agent::fromHex("0b00000000000000000000000000000000000000000000000000000000000000");
	// agent_a < agent_b

	const std::vector<AddOp> ops {
		{{agent_a, 0u}, 'a', std::nullopt, std::nullopt},
		{{agent_a, 1u}, 'a', GreenCRDT::ListID{agent_a, 0u}, std::nullopt},
		{{agent_a, 2u}, 'a', GreenCRDT::ListID{agent_a, 1u}, std::nullopt},
		{{agent_b, 0u}, 'b', std::nullopt, std::nullopt},
		{{agent_b, 1u}, 'b', GreenCRDT::ListID{agent_b, 0u}, std::nullopt},
		{{agent_b, 2u}, 'b', GreenCRDT::ListID{agent_b, 1u}, std::nullopt},
	};

	randomAddPermutations(ops, "aaabbb");
}

void testInterleave2(void) {
	const auto agent_a = GreenCRDT::Agent::fromHex("0a00000000000000000000000000000000000000000000000000000000000000");
	const auto agent_b = GreenCRDT::Agent::fromHex("0b00000000000000000000000000000000000000000000000000000000000000");
	// agent_a < agent_b

	const std::vector<AddOp> ops {
		{{agent_a, 0u}, 'a', std::nullopt, std::nullopt},
		{{agent_a, 1u}, 'a', std::nullopt, GreenCRDT::ListID{agent_a, 0u}},
		{{agent_a, 2u}, 'a', std::nullopt, GreenCRDT::ListID{agent_a, 1u}},
		{{agent_b, 0u}, 'b', std::nullopt, std::nullopt},
		{{agent_b, 1u}, 'b', std::nullopt, GreenCRDT::ListID{agent_b, 0u}},
		{{agent_b, 2u}, 'b', std::nullopt, GreenCRDT::ListID{agent_b, 1u}},
	};

	randomAddPermutations(ops, "aaabbb");
}

void testConcurrent2(void) {
	const auto agent_a = GreenCRDT::Agent::fromHex("0a00000000000000000000000000000000000000000000000000000000000000");
	const auto agent_b = GreenCRDT::Agent::fromHex("0b00000000000000000000000000000000000000000000000000000000000000");
	const auto agent_c = GreenCRDT::Agent::fromHex("0c00000000000000000000000000000000000000000000000000000000000000");
	const auto agent_d = GreenCRDT::Agent::fromHex("0d00000000000000000000000000000000000000000000000000000000000000");

	const std::vector<AddOp> ops {
		{{agent_a, 0u}, 'a', std::nullopt, std::nullopt},
		{{agent_c, 0u}, 'c', std::nullopt, std::nullopt},

		{{agent_b, 0u}, 'b', std::nullopt, std::nullopt},
		{{agent_d, 0u}, 'd', GreenCRDT::ListID{agent_a, 0u}, GreenCRDT::ListID{agent_c, 0u}},
	};

	randomAddPermutations(ops, "adbc");
}

void testMain1(void) {
	GreenCRDT::TextDocument doc;

	const GreenCRDT::Agent agent0 = GreenCRDT::Agent::fromHex("9654bd415cfac9ee6aa9f55bf1024f840afdbb1f37237673c928491a91ff7783");
	uint64_t agent0_seq {0};
	const GreenCRDT::Agent agent1 = GreenCRDT::Agent::fromHex("0100000000000000000000000000000000000000000000000000000000000000");
	uint64_t agent1_seq {0};

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

