#pragma once

#include "./list.hpp"

#include <variant>

//#include <iostream> // debug

namespace GreenCRDT {

template<typename AgentType>
struct TextDocument {
	// TODO: determine if char is the best
	using ListType = List<char, AgentType>;

	using Op = std::variant<typename ListType::OpAdd, typename ListType::OpDel>;

	// TODO: implement
	struct Cursor {
		AgentType who;
		typename ListType::ListID pos;
	};

	AgentType local_agent;

	ListType state;

	[[nodiscard]] std::string getText(void) const {
		std::string text;

		for (const auto& it : state.list) {
			if (it.value.has_value()) {
				text += it.value.value();
			}
		}

		return text;
	}

	bool apply(const Op& op) {
		if(std::holds_alternative<typename ListType::OpAdd>(op)) {
			const auto& add_op = std::get<typename ListType::OpAdd>(op);
			//std::cout << "a:" << add_op.id.id << " s:" << add_op.id.seq << " v:" << add_op.value << "\n";
			return state.add(add_op.id, add_op.value, add_op.parent_left, add_op.parent_right);
		} else if (std::holds_alternative<typename ListType::OpDel>(op)) {
			const auto& del_op = std::get<typename ListType::OpDel>(op);
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
		const AgentType& agent, uint64_t seq, // seq is the first seq
		std::optional<typename ListType::ListID> parent_left,
		std::optional<typename ListType::ListID> parent_right,
		std::string_view text
	) {
		std::vector<Op> ops;
		for (size_t i = 0; i < text.size(); i++) {
			typename ListType::ListID new_id {agent, seq++};

			ops.emplace_back(typename ListType::OpAdd{
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
		// TODO: look up typesystem and fix (move? decltype?)
		std::vector<Op> ops = text2adds(
			local_agent, state.last_seen_seq.count(local_agent) ? state.last_seen_seq[local_agent]+1u : 0u,
			parent_left,
			parent_right,
			text
		);

		// TODO: make this better
		// and apply
		for (const auto& op : ops) {
			if(std::holds_alternative<typename ListType::OpAdd>(op)) {
				const auto& add_op = std::get<typename ListType::OpAdd>(op);
				//std::cout << "a:" << add_op.id.id << " s:" << add_op.id.seq << " v:" << add_op.value << "\n";
				bool r = state.add(add_op.id, add_op.value, add_op.parent_left, add_op.parent_right);
				assert(r);
			} else if (std::holds_alternative<typename ListType::OpDel>(op)) {
				const auto& del_op = std::get<typename ListType::OpDel>(op);
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

		size_t last_idx = state.list.size();
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
			ops.emplace_back(typename ListType::OpDel{
				state.list[i].id
			});

			// TODO: do delets get a seq?????

			state.del(state.list[i].id);
		}

		return ops;
	}

	// generates ops from the difference
	// note: rn it only creates 1 diff patch
	std::vector<Op> merge(std::string_view text) {
		if (text.empty()) {
			if (state.list.empty()) {
				// no op
				return {};
			} else {
				// delete all
				return delRange(std::nullopt, std::nullopt);
			}
		}
		// text not empty

		if (state.list.empty()) {
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
		size_t text_start = 0;
		bool differ = false;
		for (; list_start < state.list.size() && text_start < text.size();) {
			// jump over tombstones
			if (!state.list[list_start].value.has_value()) {
				list_start++;
				continue;
			}

			if (state.list[list_start].value.value() != text[text_start]) {
				differ = true;
				break;
			}

			list_start++;
			text_start++;
		}

		// doc and text dont differ
		if (!differ && list_start == state.list.size() && text_start == text.size()) {
			return {};
		}

		//std::cout << "list.size: " << state.list.size() << "(" << getText().size() << ")" << " text.size: " << text.size() << "\n";
		//std::cout << "list_start: " << list_start << " text_start: " << text_start << "\n";

		// +1 so i can have unsigned
		size_t list_end = state.list.size();
		size_t text_end = text.size();
		for (; list_end > 0 && text_end > 0 && list_end >= list_start && text_end >= text_start;) {
			// jump over tombstones
			if (!state.list[list_end-1].value.has_value()) {
				list_end--;
				continue;
			}

			if (state.list[list_end-1].value.value() != text[text_end-1]) {
				break;
			}

			list_end--;
			text_end--;
		}

		//std::cout << "list_end: " << list_end << " text_end: " << text_end << "\n";

		std::vector<Op> ops;

		// 1. clear range (del all list_start - list_end)
		if (list_start <= list_end && list_start < state.list.size()) {
			ops = delRange(
				state.list[list_start].id,
				list_end < state.list.size() ? std::make_optional(state.list[list_end].id) : std::nullopt
			);
			//std::cout << "deleted: " << ops.size() << "\n";
		}

		//std::cout << "text between: " << getText() << "\n";

		// 2. add range (add all text_start - text_end)
		if (state.doc_size < text.size()) {
			auto tmp_add_ops = addText(
				list_start == 0 ? std::nullopt : std::make_optional(state.list[list_start-1].id),
				list_start == state.list.size() ? std::nullopt :std::make_optional(state.list.at(list_start).id),
				text.substr(text_start, text.size() - state.doc_size)
			);
			//std::cout << "added: " << tmp_add_ops.size() << "\n";
			ops.insert(ops.end(), tmp_add_ops.begin(), tmp_add_ops.end());
		}

		//assert(false && "implement me");
		return ops;
	}
};

} // GreenCRDT

