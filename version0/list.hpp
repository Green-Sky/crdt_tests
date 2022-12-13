#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <map>
#include <string>

#include <cassert>

namespace GreenCRDT {

template<typename ValueType, typename AgentType>
struct List {
	struct ListID {
		AgentType id;
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
	};

	// TODO: replace with SoA
	struct Entry {
		ListID id;

		// Yjs
		std::optional<ListID> parent_left;
		std::optional<ListID> parent_right;

		// might be deleted (yes, *sigh*, crtds need tombstones)
		std::optional<ValueType> value;
	};

	// TODO: use something better
	std::vector<Entry> list;

	// number of not deleted entries
	size_t doc_size {0};

	std::map<AgentType, uint64_t> last_seen_seq;

	std::optional<size_t> findIdx(const ListID& list_id) {
		for (size_t i = 0; i < list.size(); i++) {
			if (list[i].id == list_id) {
				return i;
			}
		}

		return std::nullopt;
	}

	// returns false if missing OPs
	// based on YjsMod https://github.com/josephg/reference-crdts/blob/9f4f9c3a97b497e2df8ae4473d1e521d3c3bf2d2/crdts.ts#L293-L348
	// which is a modified Yjs(YATA) algo
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

			// insert parentless into empty doc
			list.emplace(list.begin(), Entry{
				list_id,
				parent_left,
				parent_right,
				value
			});
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

} // GreenCRDT

