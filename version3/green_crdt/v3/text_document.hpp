#pragma once

#include "./list.hpp"

#include <variant>

//#include <iostream> // debug

namespace GreenCRDT::V3 {

template<typename ActorType>
struct TextDocument {
	// TODO: determine if char is the best
	using ListType = List<char, ActorType>;

	struct OpAdd {
		typename ListType::ListID id;

		std::optional<typename ListType::ListID> parent_left;
		std::optional<typename ListType::ListID> parent_right;

		char value;
	};

	struct OpDel {
		typename ListType::ListID id;
	};

	using Op = std::variant<OpAdd, OpDel>;

	//// TODO: implement
	//struct Cursor {
		//AgentType who;
		//typename ListType::ListID pos;
	//};

	ActorType local_actor;

	ListType state;

	[[nodiscard]] std::string getText(void) const {
		std::string text;

		for (const auto& it : state._list_data) {
			if (it.value.has_value()) {
				text += it.value.value();
			}
		}

		return text;
	}

	bool apply(const Op& op) {
		if(std::holds_alternative<OpAdd>(op)) {
			const auto& add_op = std::get<OpAdd>(op);
			//std::cout << "a:" << add_op.id.id << " s:" << add_op.id.seq << " v:" << add_op.value << "\n";
			return state.add(add_op.id, add_op.value, add_op.parent_left, add_op.parent_right);
		} else if (std::holds_alternative<OpDel>(op)) {
			const auto& del_op = std::get<OpDel>(op);
			return state.del(del_op.id);
		} else {
			assert(false);
		}
	}

	bool apply(const std::vector<Op>& ops) {
		for (const auto& op : ops) {
			if (!apply(op)) {
				// this is not ideal, since we might have applyed some, and dont report which/howmany
				return false;
			}
		}

		return true;
	}

	static std::vector<Op> text2adds(
		const ActorType& actor, uint64_t seq, // seq is the first seq
		std::optional<typename ListType::ListID> parent_left,
		std::optional<typename ListType::ListID> parent_right,
		std::string_view text
	) {
		std::vector<Op> ops;
		for (size_t i = 0; i < text.size(); i++) {
			typename ListType::ListID new_id {actor, seq++};

			ops.emplace_back(OpAdd{
				new_id,
				parent_left,
				parent_right,
				text[i]
			});

			parent_left = new_id;
		}

		return ops;
	}

	// adds in tast with specified parents
	// returns generated ops
	std::vector<Op> addText(
		std::optional<typename ListType::ListID> parent_left,
		std::optional<typename ListType::ListID> parent_right,
		std::string_view text
	) {
		// TODO: move actor setting to list
		if (!state.findActor(local_actor).has_value()) {
			state._actors.push_back(local_actor);
		}

		// TODO: look up typesystem and fix (move? decltype?)
		std::vector<Op> ops = text2adds(
			// TODO: abstract actors
			local_actor, state._last_seen_seq.count(state.findActor(local_actor).value()) ? state._last_seen_seq[state.findActor(local_actor).value()]+1u : 0u,
			parent_left,
			parent_right,
			text
		);

		// TODO: make this better
		// and apply
		for (const auto& op : ops) {
			if(std::holds_alternative<OpAdd>(op)) {
				const auto& add_op = std::get<OpAdd>(op);
				//std::cout << "a:" << add_op.id.id << " s:" << add_op.id.seq << " v:" << add_op.value << "\n";
				bool r = state.add(add_op.id, add_op.value, add_op.parent_left, add_op.parent_right);
				assert(r);
			} else if (std::holds_alternative<OpDel>(op)) {
				const auto& del_op = std::get<OpDel>(op);
				state.del(del_op.id);
			} else {
				assert(false);
			}
		}

		return ops; // TODO: move?
	}

	// deletes everything in range [first, last)
	// returns generated ops
	std::vector<Op> delRange(
		std::optional<typename ListType::ListID> left,
		std::optional<typename ListType::ListID> right
	) {
		size_t first_idx = 0;
		if (left.has_value()) {
			auto res = state.findIdx(left.value());
			if (!res.has_value()) {
				assert(false && "cant find left");
				return {};
			}
			first_idx = res.value();
		}

		size_t last_idx = state.size();
		if (right.has_value()) {
			auto res = state.findIdx(right.value());
			if (!res.has_value()) {
				assert(false && "cant find right");
				return {};
			}
			last_idx = res.value();
		}

		std::vector<Op> ops;

		for (size_t i = first_idx; i < last_idx; i++) {
			if (!state.getValue(i).has_value()) {
				// allready deleted
				continue;
			}

			ops.emplace_back(OpDel{
				//state.list.at(i).id
				state.getID(i)
			});

			// TODO: do delets get a seq?????

			state.del(state.getID(i));
		}

		return ops;
	}

	// generates ops from the difference
	// note: rn it only creates 1 diff patch
	std::vector<Op> merge(std::string_view text) {
		if (text.empty()) {
			if (state.empty() || state.getDocSize() == 0) {
				// no op
				return {};
			} else {
				// delete all
				return delRange(std::nullopt, std::nullopt);
			}
		}
		// text not empty

		if (state.empty()) {
			return addText(
				std::nullopt,
				std::nullopt,
				text
			);
		}
		// neither empty

		// find start and end of changes
		// start
		size_t list_start = 0;
		size_t list_start_counted = 0;
		size_t text_start = 0;
		bool differ = false;
		for (; list_start < state.size() && text_start < text.size();) {
			// jump over tombstones
			if (!state.getValue(list_start).has_value()) {
				list_start++;
				continue;
			}

			if (state.getValue(list_start).value() != text[text_start]) {
				differ = true;
				break;
			}

			list_start++;
			text_start++;
			list_start_counted++;
		}

		// doc and text dont differ
		if (!differ && list_start == state.size() && text_start == text.size()) {
			return {};
		}
		//std::cout << "list.size: " << state.list.size() << "(" << getText().size() << ")" << " text.size: " << text.size() << "\n";
		//std::cout << "list_start: " << list_start << " text_start: " << text_start << "\n";

		// +1 so i can have unsigned
		size_t list_end = state.size();
		size_t text_end = text.size();
		//for (; list_end > 0 && text_end > 0 && list_end >= list_start && text_end >= text_start;) {
		//while (list_end >= list_start && text_end >= text_start) {
		size_t list_end_counted = 0;
		differ = false; // var reuse
		//while (list_start_counted - list_end_counted > state.doc_size && text_end >= text_start) {
		while (state.getDocSize() - list_start_counted > list_end_counted && text_end >= text_start) {
			// jump over tombstones
			if (!state.getValue(list_end-1).has_value()) {
				list_end--;
				continue;
			}

			if (state.getValue(list_end-1).value() != text[text_end-1]) {
				differ = true;
				break;
			}

			list_end--;
			text_end--;
			list_end_counted++;
		}

		if (!differ && text_start == text_end+1) {
			// we ran into eachother without seeing the different char
			// TODO: do we need to increment list_end? text_end?
			list_end++;
		}

		//std::cout << "list_end: " << list_end << " text_end: " << text_end << "\n";
		//std::cout << "substring before: " << text.substr(text_start, text.size() - state.doc_size) << "\n";

		std::vector<Op> ops;

		// 1. clear range (del all list_start - list_end)
		if (list_start <= list_end && list_start < state.size()) {
			//list_end += list_start == list_end;
			ops = delRange(
				state.getID(list_start),
				list_end < state.size() ? std::make_optional(state.getID(list_end)) : std::nullopt
			);
			//std::cout << "deleted: " << ops.size() << "\n";
		}

		//std::cout << "text between: " << getText() << "\n";
		//std::cout << "substring between: " << text.substr(text_start, text.size() - state.doc_size) << "\n";

		// 2. add range (add all text_start - text_end)
		if (state.getDocSize() < text.size()) {
			auto tmp_add_ops = addText(
				list_start == 0 ? std::nullopt : std::make_optional(state.getID(list_start-1)),
				list_start == state.size() ? std::nullopt :std::make_optional(state.getID(list_start)),
				text.substr(text_start, text.size() - state.getDocSize())
			);
			//std::cout << "added: " << tmp_add_ops.size() << "\n";
			ops.insert(ops.end(), tmp_add_ops.begin(), tmp_add_ops.end());
		}

		return ops;
	}
};

} // GreenCRDT::V3

