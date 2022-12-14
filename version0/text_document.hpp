#pragma once

#include "./list.hpp"

#include <optional>
#include <variant>
#include <vector>

namespace GreenCRDT {

template<typename AgentType>
struct TextDocument {
	// TODO: determine if char is the best
	using ListType = List<char, AgentType>;

	using Op = std::variant<typename ListType::OpAdd, typename ListType::OpDel>;

	struct Cursor {
		AgentType who;
		typename ListType::ListID pos;
	};

	AgentType local_agent;

	ListType state;

	[[nodiscard]] std::string getText(void) const {
		std::string text;

		for (const auto& it : state.list) {
			if (it.value) {
				text += it.value.value();
			}
		}

		return text;
	}

	static std::vector<Op> text2adds(
		const AgentType& agent, uint64_t& last_seq,
		std::optional<typename ListType::ListID> parent_left,
		std::optional<typename ListType::ListID> parent_right,
		std::string_view text
	) {
		std::vector<Op> ops;
		for (size_t i = 0; i < text.size(); i++) {
			typename ListType::ListID new_id {agent, ++last_seq};

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
			local_agent, state.last_seen_seq[local_agent],
			parent_left,
			parent_right,
			text
		);

		// TODO: make this better
		// and apply
		for (const auto& op : ops) {
			if constexpr (std::holds_alternative<typename ListType::OpAdd>(op)) {
				const auto& add_op = std::get<typename ListType::OpAdd>(op);
				state.add(add_op.id, add_op.value, add_op.parent_left, add_op.parent_right);
			} else if constexpr (std::holds_alternative<typename ListType::OpDel>(op)) {
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
			first_idx = res;
		}

		size_t last_idx = state.list.size();
		if (right.has_value()) {
			auto res = state.findIdx(right.value());
			if (!res.has_value()) {
				assert(false && "cant find right");
				return {};
			}
			last_idx = res;
		}

		std::vector<Op> ops;

		for (size_t i = first_idx; i < last_idx; i++) {
			ops.emplace_back(typename ListType::OpDel{
				state.list[i].id
			});

			// TODO: do delets get a seq?????

			state.list[i].value = std::nullopt;
		}

		return ops;
	}

	// generates ops from the difference
	// note: rn it only creates 1 diff patch
	std::vector<Op> merge(std::string_view other_text) {
		if (other_text.empty()) {
			return {};
		}

		if (state.list.empty()) {
			return addText(
				std::nullopt,
				std::nullopt,
				other_text
			);
		}

		// find start and end of changes
		// start
		size_t list_idx_start = 0;
		size_t other_idx_start = 0;
		//for (; idx_start < state.list.size(); idx_start++) {}

	}
};

} // GreenCRDT

