#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <map>
#include <string>

#include <cassert>

#if !defined(extra_assert)
	#if defined(EXTRA_ASSERTS) && EXTRA_ASSERTS == 1
		#define extra_assert(...) assert(__VA_ARGS__)
	#else
		#define extra_assert(...) void(0)
	#endif
#endif

namespace GreenCRDT::V1 {

template<typename ValueType, typename ActorType>
struct List {
	// for public interface
	struct ListID {
		ActorType id;
		uint64_t seq{0}; // strictly increasing for that actor

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

	struct ListIDInternal {
		size_t actor_idx{0};
		uint64_t seq{0}; // strictly increasing for that actor

		bool operator==(const ListIDInternal& rhs) const {
			return seq == rhs.seq && actor_idx == rhs.actor_idx;
		}
	};

	// internally the index into this array is used to refer to an actor
	std::vector<ActorType> _actors;

	// TODO: replace with SoA
	struct Entry {
		ListIDInternal id;

		// Yjs
		std::optional<ListIDInternal> parent_left;
		std::optional<ListIDInternal> parent_right;

		// might be deleted (yes, *sigh*, crtds need tombstones)
		std::optional<ValueType> value;
	};

	// TODO: use something better, edit: this seems fine
	std::vector<Entry> list;

	// number of not deleted entries
	size_t doc_size {0};

	std::map<size_t, uint64_t> last_seen_seq;

	std::optional<size_t> findActor(const ActorType& actor) const {
		for (size_t i = 0; i < _actors.size(); i++) {
			if (_actors[i] == actor) {
				return i;
			}
		}
		return std::nullopt;
	}

	std::optional<size_t> findIdx(const ListIDInternal& list_id) const {
		extra_assert(verify());

		for (size_t i = 0; i < list.size(); i++) {
			if (list[i].id == list_id) {
				return i;
			}
		}

		return std::nullopt;
	}

	std::optional<size_t> findIdx(const ListID& list_id) const {
		extra_assert(verify());

		const auto actor_idx_opt = findActor(list_id.id);
		if (!actor_idx_opt.has_value()) {
			return std::nullopt;
		}

		const ListIDInternal tmp_id {actor_idx_opt.value(), list_id.seq};

		for (size_t i = 0; i < list.size(); i++) {
			if (list[i].id == tmp_id) {
				return i;
			}
		}

		return std::nullopt;
	}

	// returns false if missing OPs
	// based on YjsMod https://github.com/josephg/reference-crdts/blob/9f4f9c3a97b497e2df8ae4473d1e521d3c3bf2d2/crdts.ts#L293-L348
	// which is a modified Yjs(YATA) algo
	bool add(const ListID& list_id, const ValueType& value, const std::optional<ListID>& parent_left, const std::optional<ListID>& parent_right) {
		extra_assert(verify());

		size_t actor_idx {0};
		{ // new actor?
			// add, even if op fails
			const auto actor_opt = findActor(list_id.id);
			if (!actor_opt.has_value()) {
				actor_idx = _actors.size();
				_actors.push_back(list_id.id);
			} else {
				actor_idx = actor_opt.value();
			}
		}

		// check actor op order
		if (!last_seen_seq.count(actor_idx)) {
			// we dont know this actor yet, first seq needs to be 0
			if (list_id.seq != 0) {
				return false;
			}
		} else {
			// making sure we dont skip operations by that actor
			if (list_id.seq != last_seen_seq.at(actor_idx) + 1) {
				return false;
			}
		}

		size_t insert_idx = 0;
		if (list.empty()) {
			if (parent_left.has_value() || parent_right.has_value()) {
				// empty, missing parents
				return false;
			}
		} else {
			// find left
			std::optional<size_t> left_idx = std::nullopt;
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
						// actor id tie breaker
						if (_actors[actor_idx] < _actors[at_i.id.actor_idx]) {
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
		}

		{ // actual insert
			Entry new_entry;

			new_entry.id.actor_idx = actor_idx;
			new_entry.id.seq = list_id.seq;

			if (parent_left.has_value()) {
				new_entry.parent_left = ListIDInternal{findActor(parent_left.value().id).value(), parent_left.value().seq};
			}

			if (parent_right.has_value()) {
				new_entry.parent_right = ListIDInternal{findActor(parent_right.value().id).value(), parent_right.value().seq};
			}

			new_entry.value = value;

			list.emplace(list.begin() + insert_idx, new_entry);
		}

		doc_size++;
		last_seen_seq[actor_idx] = list_id.seq;

		extra_assert(verify());
		return true;
	}

	// returns false if not found
	bool del(const ListID& id) {
		extra_assert(verify());

		auto actor_idx_opt = findActor(id.id);
		if (!actor_idx_opt.has_value()) {
			// we dont have anything with that actor
			return false;
		}

		const ListIDInternal tmp_id {actor_idx_opt.value(), id.seq};

		for (auto& it : list) {
			if (it.id == tmp_id) {
				if (it.value.has_value()) {
					it.value.reset();

					doc_size--;
					extra_assert(verify());
					return true;
				} else {
					extra_assert(verify());
					return false; // TODO: allow double deletes?,,,, need ids
				}
			}
		}

		extra_assert(verify());
		return false;
	}

	std::vector<ValueType> getArray(void) const {
		std::vector<ValueType> array;
		for (const auto& e : list) {
			if (e.value.has_value()) {
				array.push_back(e.value.value());
			}
		}

		return array;
	}

	// TODO: only in debug?
	bool verify(void) const {
		size_t actual_size = 0;
		for (const auto& it : list) {
			if (it.value.has_value()) {
				actual_size++;
			}
		}
		//assert(doc_size == actual_size);
		return doc_size == actual_size;
	}
};

} // GreenCRDT::V1

