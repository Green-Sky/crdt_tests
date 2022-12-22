#define EXTRA_ASSERTS 0

#include <green_crdt/v3/list.hpp>
#include <nlohmann/json.hpp>

#include <unordered_map>
#include <string_view>
#include <fstream>
#include <iostream>
#include <cassert>

using ActorID = std::array<uint8_t, 32>;
using List = GreenCRDT::V3::List<char, ActorID>;

template<>
struct std::hash<ActorID> {
	std::size_t operator()(ActorID const& s) const noexcept {
		static_assert(sizeof(size_t) == 8);
		// TODO: maybe shuffle the indices a bit
		return
			(static_cast<size_t>(s[0]) << 8*0) |
			(static_cast<size_t>(s[1]) << 8*1) |
			(static_cast<size_t>(s[2]) << 8*2) |
			(static_cast<size_t>(s[3]) << 8*3) |
			(static_cast<size_t>(s[4]) << 8*4) |
			(static_cast<size_t>(s[5]) << 8*5) |
			(static_cast<size_t>(s[6]) << 8*6) |
			(static_cast<size_t>(s[7]) << 8*7)
		;
	}
};

// for dev, benching in debug is usefull, but only if the ammount of asserts is reasonable
#if !defined(extra_assert)
	#if defined(EXTRA_ASSERTS) && EXTRA_ASSERTS == 1
		#define extra_assert(...) assert(__VA_ARGS__)
	#else
		#define extra_assert(...) void(0)
	#endif
#endif

namespace detail {
	uint8_t nib_from_hex(char c) {
		extra_assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));

		if (c >= '0' && c <= '9') {
			return static_cast<uint8_t>(c) - '0';
		} else if (c >= 'a' && c <= 'f') {
			return (static_cast<uint8_t>(c) - 'a') + 10u;
		} else {
			return 0u;
		}
	}
} // detail

static ActorID ActorIDFromStr(std::string_view str) {
	extra_assert(str.size() == 32*2);
	ActorID tmp;

	for (size_t i = 0; i < tmp.size(); i++) {
		tmp[i] = detail::nib_from_hex(str[i*2]) << 4 | detail::nib_from_hex(str[i*2+1]);
	}

	return tmp;
}

// seq@ID type format used in the json
struct JObj {
	ActorID id;
	uint64_t seq {0};
};

static JObj JObjFromStr(std::string_view str) {
	extra_assert(str.size() > 32*2 + 1);

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

	return {ActorIDFromStr(id_sv), tmp_seq};
}

int main(void) {
	List list;

	std::ifstream file {"../res/paper.json"};
	std::cout << "start reading...\n";

	uint64_t g_total_inserts {0};
	uint64_t g_total_deletes {0};
	//uint64_t g_seq_inserts {0}; // the opsec are not sequentially growing for inserts, so we sidestep
	std::unordered_map<ActorID, uint64_t> g_seq_inserts {0}; // the opsec are not sequentially growing for inserts, so we sidestep
	std::unordered_map<ActorID, std::unordered_map<uint64_t, uint64_t>> map_seq; // maps json op_seq -> lits id seq

	for (std::string line; std::getline(file, line); ) {
		nlohmann::json j_entry = nlohmann::json::parse(line);
		const ActorID actor = ActorIDFromStr(static_cast<const std::string&>(j_entry["actor"]));
		const size_t actor_idx = list.findActor(actor).value_or(0u);
		uint64_t op_seq = j_entry["startOp"];
		for (const auto& j_op : j_entry["ops"]) {
			if (j_op["action"] == "set") {
				const auto obj = JObjFromStr(static_cast<const std::string&>(j_op["obj"]));
				if (obj.seq != 1) {
					// skip all non text edits (create text doc, curser etc)
					continue;
				}

				if (j_op["insert"]) {
					const auto& j_parent = j_op["key"];
					extra_assert(!j_parent.is_null());
					if (j_parent == "_head") {
						uint64_t tmp_seq {g_seq_inserts[actor]++};
						bool r = list.add(
							{actor, tmp_seq},
							static_cast<const std::string&>(j_op["value"]).front(),
							std::nullopt,
							std::nullopt
						);
						assert(r);
						map_seq[actor][op_seq] = tmp_seq;
						g_total_inserts++;
					} else { // we have a parrent
						extra_assert(static_cast<const std::string&>(j_op["value"]).size() == 1);

						size_t hint_last_insert {0};
						if (list._last_inserted_idx.count(actor_idx)) {
							hint_last_insert = list._last_inserted_idx[actor_idx];
						}

						// split parent into seq and actor
						const auto parent_left = JObjFromStr(static_cast<const std::string&>(j_parent));
						auto idx_opt = list.findIdx({parent_left.id, map_seq[parent_left.id][parent_left.seq]}, hint_last_insert);
						assert(idx_opt.has_value());

						std::optional<List::ListID> parent_left_id;
						{
							const auto& tmp_parent_left_id = list._list_ids.at(idx_opt.value());
							parent_left_id = {list._actors[tmp_parent_left_id.actor_idx], tmp_parent_left_id.seq};
						}

						std::optional<List::ListID> parent_right_id;
						if (idx_opt.value()+1 < list._list_ids.size()) {
							const auto& tmp_parent_right_id = list._list_ids.at(idx_opt.value()+1);
							parent_right_id = {list._actors[tmp_parent_right_id.actor_idx], tmp_parent_right_id.seq};
						}

						uint64_t tmp_seq {g_seq_inserts[actor]++};
						bool r = list.add(
							{actor, tmp_seq},
							static_cast<const std::string&>(j_op["value"]).front(),
							parent_left_id,
							parent_right_id
						);
						assert(r);
						map_seq[actor][op_seq] = tmp_seq;
						g_total_inserts++;
					}
				} else {
					// i think this is curser movement
				}
			} else if (j_op["action"] == "del") {
				const auto list_id = JObjFromStr(static_cast<const std::string&>(j_op["key"]));
				bool r = list.del({list_id.id, map_seq[list_id.id][list_id.seq]});
				assert(r);
				g_total_deletes++;
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

	std::cout << "\ndoc size (with tombstones): " << list._list_ids.size() << "\n";
	std::cout << "doc size: " << list.getDocSize() << "\n";
	std::cout << "total inserts: " << g_total_inserts << "\n";
	std::cout << "total deletes: " << g_total_deletes << "\n";
	std::cout << "total ops: " << g_total_inserts + g_total_deletes << "\n";

	//std::cout << "find_hint: " << list._stat_find_with_hint << "\n";
	//std::cout << "find_hint_hit: " << list._stat_find_with_hint_hit << "\n";

	// checked, looks correct
#if 0
	std::cout << "doc text:\n";
	// simple print
	for (const auto& it : list.list) {
		if (it.value) {
			std::cout << it.value.value();
		}
	}
	std::cout << "\n";
#endif

	return 0;
}

