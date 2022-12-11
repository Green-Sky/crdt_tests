#include <nlohmann/json.hpp>

#include <array>
#include <list>
#include <optional>

#include <fstream>
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

	struct ListID {
		ID id;
		uint64_t seq{0};

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
			auto seq_sv = str.substr(0, at_pos);
			auto id_sv = str.substr(at_pos+1);

			assert(seq_sv.size() != 0);
			assert(id_sv.size() == 32*2);

			uint64_t tmp_seq {0};
			for (size_t i = 0; i < seq_sv.size(); i++) {
				assert(seq_sv[i] >= '0' && seq_sv[i] <= '9');
				tmp_seq *= 10;
				tmp_seq += seq_sv[i] - '0';
			}

			return {GreenCRDT::ID::fromHex(id_sv), tmp_seq};
		}
	};

	template<typename ValueType>
	struct List {
		struct Entry {
			ListID id;
			std::optional<ListID> parent;

			// might be deleted (yes, *sigh*, crtds need tombstones)
			std::optional<ValueType> value;
		};

		// linked list for now
		std::list<Entry> list;

		// largest observed sequence number minus one
		uint64_t largest_seq{0};

		// the "first" insert(s)
		void addParentless(const ValueType& value, const ListID& list_id) {
			if (list.empty()) { // special
				list.emplace(list.begin(), Entry{
					list_id,
					std::nullopt, // no parent
					value
				});
			} else {
				// loop until we find the position to insert
				for (auto it = list.begin(); it != list.end(); it++) {
					if (
						it->parent && // has a parent, so we need to insert before that (usually the first one)
						list_id < it->id // ensure ordering
					) {
						list.emplace(it, Entry{
							list_id,
							std::nullopt, // no parent
							value
						});
						break;
					}
				}
			}

			largest_seq = list_id.seq > largest_seq ? list_id.seq : largest_seq;
		}

		// returns false if parent not found. (missing OPs)
		bool add(const ListID& parent, const ValueType& value, const ListID& list_id) {
			// list empty, obv cant find parent
			if (list.empty()) {
				return false;
			}

			// loop until we find the parent
			auto it = list.begin();
			for (; it != list.end(); it++) {
				if (it->id == parent) {
					break;
				}
			}
			// not found, prob data missing
			if (it == list.end()) {
				return false;
			}

			bool inserted = false;
			do {
				it++;
				if (
					it == list.end() || // no more elements
					it->parent != parent || // not a sibling
					list_id < it->id // we where created earlier
				) {
					list.emplace(it, Entry{
						list_id,
						parent,
						value
					});
					inserted = true;
					break;
				}
			} while (it != list.end());
			assert(inserted);

			largest_seq = list_id.seq > largest_seq ? list_id.seq : largest_seq;
			return true;
		}

		// returns false if not found
		bool del(const ListID& id) {
			auto it = list.begin();
			for (; it != list.end(); it++) {
				if (it->id == id) {
					it->value = std::nullopt;

					return true;
				}
			}

			// not found
			return false;
		}
	};

	struct TextDocument {
		struct Cursor {
			ID who;
			ListID pos;
		};

		// TODO: determine if char is the best
		List<char> state;
	};
} // GreenCRDT

int main(void) {
	GreenCRDT::TextDocument doc;

	std::ifstream file {"../res/paper.json"};
	std::cout << "start reading...\n";
	for (std::string line; std::getline(file, line); ) {
		nlohmann::json j_entry = nlohmann::json::parse(line);
		const GreenCRDT::ID actor = GreenCRDT::ID::fromHex(static_cast<const std::string&>(j_entry["actor"]));
		uint64_t op_seq = j_entry["startOp"];
		for (const auto& j_op : j_entry["ops"]) {
			if (j_op["action"] == "set") {
				const GreenCRDT::ListID obj = GreenCRDT::ListID::fromStr(static_cast<const std::string&>(j_op["obj"]));
				if (obj.seq != 1) {
					// skip all non text edits
					continue;
				}

				if (j_op["insert"]) {
					const auto& j_parent = j_op["key"];
					if (j_parent == "_head") {
						//std::cout << j_op << "\n";

						auto size_pre = doc.state.list.size();

						doc.state.addParentless(
							static_cast<const std::string&>(j_op["value"]).front(),
							{
								actor,
								op_seq
							}
						);

						assert(size_pre+1 == doc.state.list.size());
					} else { // we have a parrent
						assert(j_parent != nullptr);
						auto size_pre = doc.state.list.size();

						// split parent into seq and id
						GreenCRDT::ListID parent = GreenCRDT::ListID::fromStr(static_cast<const std::string&>(j_parent));

						assert(static_cast<const std::string&>(j_op["value"]).size() == 1);

						doc.state.add(
							parent,
							static_cast<const std::string&>(j_op["value"]).front(),
							{ actor, op_seq }
						);

						assert(size_pre+1 == doc.state.list.size());
					}
				} else {
					// i think this is curser movement
				}
			} else if (j_op["action"] == "del") {
				doc.state.del(GreenCRDT::ListID::fromStr(static_cast<const std::string&>(j_op["key"])));
			} else if (j_op["action"] == "makeText") {
				// doc.clear();
			} else if (j_op["action"] == "makeMap") {
				// no idea
			} else {
				std::cout << "op: " << j_op << "\n";
			}

			op_seq++;
		}
	}

	std::cout << "\ndoc size (with tombstones): " << doc.state.list.size() << "\n";
	std::cout << "\ndoc largest seq: " << doc.state.largest_seq << "\n";
	std::cout << "doc text:\n";

	// simple print
	for (const auto& it : doc.state.list) {
		if (it.value) {
			std::cout << it.value.value();
		}
	}

	std::cout << "\n";

	return 0;
}

