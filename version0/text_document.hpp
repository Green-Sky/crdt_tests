#include "./list.hpp"

namespace GreenCRDT {

template<typename AgentType>
struct TextDocument {
	// TODO: determine if char is the best
	using ListType = List<char, AgentType>;

	struct Cursor {
		AgentType who;
		typename ListType::ListID pos;
	};

	ListType state;

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

