#include <crdt/text_document.hpp>
#include <nlohmann/json.hpp>

extern "C" {
#include <zed_net.h>
#include <tox/tox.h>
#include <sodium.h>
}

#include <optional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <variant>
#include <thread>
#include <future>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <utility>

#include <iostream>
#include <cassert>

//struct ToxPubKey {
	//std::array<uint8_t, 32> data;

	//bool operator==(const ToxPubKey& other) const {
		//return data == other.data;
	//}

	//bool operator<(const ToxPubKey& other) const {
		//return data < other.data;
	//}
//};
using ToxPubKey = std::array<uint8_t, 32>;

template<>
struct std::hash<ToxPubKey> {
	std::size_t operator()(ToxPubKey const& s) const noexcept {
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

// single letter agent, for testing only
//using Agent = char;
//using Agent = uint16_t; // tmp local port
using Agent = ToxPubKey;
using Doc = GreenCRDT::TextDocument<Agent>;
using ListType = Doc::ListType;

struct Command {
	Agent agent;
	uint64_t seq {0}; // independed of the ops inside, theoretically
	//...
	std::vector<Doc::Op> ops;
};

namespace std {
	template<typename T>
	static void to_json(nlohmann::json& nlohmann_json_j, const std::optional<T>& nlohmann_json_t) {
		if (nlohmann_json_t.has_value()) {
			nlohmann_json_j = nlohmann_json_t.value();
		} else {
			nlohmann_json_j = nullptr;
		}
	}

	template<typename T>
	static void from_json(const nlohmann::json& nlohmann_json_j, std::optional<T>& nlohmann_json_t) {
		if (!nlohmann_json_j.is_null()) {
			nlohmann_json_t = static_cast<T>(nlohmann_json_j);
		} else {
			nlohmann_json_t = std::nullopt;
		}
	}
} // namespace std

namespace GreenCRDT {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ListType::ListID,
	id,
	seq
)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ListType::OpAdd,
	id,
	parent_left,
	parent_right,
	value
)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ListType::OpDel,
	id
)

} // namespace GreenCRDT

// bc variant <.<
namespace std {
	static void to_json(nlohmann::json& nlohmann_json_j, const Doc::Op& nlohmann_json_t) {
		if (std::holds_alternative<Doc::ListType::OpAdd>(nlohmann_json_t)) {
			nlohmann_json_j["t"] = "add";
			nlohmann_json_j["d"] = std::get<Doc::ListType::OpAdd>(nlohmann_json_t);
		} else if (std::holds_alternative<Doc::ListType::OpDel>(nlohmann_json_t)) {
			nlohmann_json_j["t"] = "del";
			nlohmann_json_j["d"] = std::get<Doc::ListType::OpDel>(nlohmann_json_t);
		} else {
			assert(false && "missing op type");
		}
	}

	static void from_json(const nlohmann::json& nlohmann_json_j, Doc::Op& nlohmann_json_t) {
		if (nlohmann_json_j.is_null()) {
			std::cerr << "got null j\n";
			return;
		}

		if (nlohmann_json_j.at("t") == "add") {
			nlohmann_json_t = static_cast<Doc::ListType::OpAdd>(nlohmann_json_j.at("d"));
		} else if (nlohmann_json_j.at("t") == "del") {
			nlohmann_json_t = static_cast<Doc::ListType::OpDel>(nlohmann_json_j.at("d"));
		} else {
			assert(false && "missing op type");
		}
	}
} // namespace std

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Command,
	agent,
	seq,
	ops
)

namespace vim {

static bool sendResponse(zed_net_socket_t* remote_socket, const int64_t id, const nlohmann::json& j) {
	auto j_msg = nlohmann::json::array();

	j_msg.push_back(id);
	j_msg.push_back(j);

	std::string str = j_msg.dump();
	str += '\n';

	auto ret = zed_net_tcp_socket_send(remote_socket, str.data(), str.size());
	return ret == 0;
}

static bool sendCommand(zed_net_socket_t* remote_socket, const std::string_view mode, const std::string_view command) {
	auto j = nlohmann::json::array();

	j.push_back(mode);
	j.push_back(command);

	std::string str = j.dump();
	str += '\n';

	auto ret = zed_net_tcp_socket_send(remote_socket, str.data(), str.size());
	return ret == 0;
}

static bool sendSetup(zed_net_socket_t* remote_socket) {
	return sendCommand(remote_socket, "ex",

// vars
R"(
let b:green_crdt_timer_can_send = v:true
let b:green_crdt_timer_can_fetch = v:true
let b:green_crdt_dirty = v:true
)"

// send
R"(
function! GreenCRDTSendTimerCallback(timer) abort
	let b:green_crdt_timer_can_send = v:true
	call GreenCRDTCheckTimeAndSend()
endfunction
)"

// TODO: make send sync? (ch_evalexpr())
R"(
function! GreenCRDTCheckTimeAndSend() abort
	if b:green_crdt_timer_can_send && b:green_crdt_dirty
		let b:green_crdt_timer_can_send = v:false
		call ch_sendexpr(b:channel, [{'cmd': 'full_buffer', 'lines': getbufline(bufnr(), 1, '$')}])
		let b:green_crdt_dirty = v:false
		call timer_start(100, 'GreenCRDTSendTimerCallback')
	endif
endfunction
)"

// fetch
R"(
function! GreenCRDTFetchTimerCallback(timer) abort
	let b:green_crdt_timer_can_fetch = v:true
	call GreenCRDTCheckTimeAndFetch()
endfunction
)"

R"(
function! GreenCRDTCheckTimeAndFetch()
	if reg_executing() isnot# '' | return | endif

	if b:green_crdt_timer_can_fetch
		let b:green_crdt_timer_can_fetch = v:false

		" dont update when inserting or visual (or atleast not in visual)
		if mode() is# 'n'
			let l:response = ch_evalexpr(b:channel, [{'cmd': 'fetch_changes'}])
			" TODO: dont use empty as an indicator
			if ! empty(l:response)
				for [line_number, line] in l:response
					call setline(line_number, line)
				endfor

				let l:buffer_line_count = line('$')
				let l:new_line_count = len(l:response)
				if l:buffer_line_count > new_line_count
					call deletebufline(bufnr(), l:new_line_count+1, buffer_line_count)
				endif
			endif
		endif

		let b:green_crdt_fetch_timer = timer_start(203, 'GreenCRDTFetchTimerCallback')
	endif
endfunction
)"

// change event
R"(
function! GreenCRDTChangeEvent()
	let b:green_crdt_dirty = v:true
	call GreenCRDTCheckTimeAndSend()
	call GreenCRDTCheckTimeAndFetch()
endfunction
)"

// TODO: pull changes

// cleanup, to be called by user
// delfunction fails for stop... but well
R"(
function! GreenCRDTStop()
	augroup green_crdt
		au!
	augroup END

	call timer_stop(b:green_crdt_fetch_timer)

	call ch_close(b:channel)

	delfunction GreenCRDTCheckTimeAndSend
	delfunction GreenCRDTCheckTimeAndFetch
	delfunction GreenCRDTSendTimerCallback
	delfunction GreenCRDTFetchTimerCallback
	delfunction GreenCRDTChangeEvent
	"delfunction GreenCRDTStop
	let b:green_crdt_timer_can_send = v:true
endfunction
)"

// this is a hack, bc for some EX mode IPC buggyness reason, it only works as single commands OR inside a function
R"(
function! GreenCRDTSetupEvents() abort
	augroup green_crdt
		au!
		au TextChanged <buffer> call GreenCRDTChangeEvent()
		au TextChangedI <buffer> call GreenCRDTChangeEvent()
	augroup END
endfunction
call GreenCRDTSetupEvents()
delfunction GreenCRDTSetupEvents
)"

R"(
let b:green_crdt_fetch_timer = timer_start(300, 'GreenCRDTFetchTimerCallback')

echo 'setup done'
)");
}

} // namespace vim

// visibility hack
struct RequestCommand {
	Agent agent;
	uint64_t seq{0};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RequestCommand,
	agent,
	seq
)

// hash for unordered_set
template<>
struct std::hash<std::pair<uint32_t, Agent>> {
	std::size_t operator()(std::pair<uint32_t, Agent> const& s) const noexcept {
		return std::hash<uint32_t>{}(s.first) << 3 ^ std::hash<Agent>{}(s.second);
	}
};

struct SharedContext {
	std::atomic_bool should_quit {false};

	// tox ngc id for agent
	ToxPubKey agent;
	std::promise<void> agent_set;

	std::mutex command_lists_mutex; // for list and frontier!!
	std::unordered_map<ToxPubKey, std::unordered_map<uint64_t, Command>> command_lists;
	std::unordered_map<ToxPubKey, uint64_t> command_frontier; // last applied seq

	// contains remote changes we can apply in the main thread
	std::mutex staging_mutex;
	std::unordered_map<ToxPubKey, uint64_t> staging_frontier; // last seq we have in command_lists, via tox
	// (can be lower then command_frontier for local agent

	// TODO
	std::mutex unknown_agents_mutex;
	std::unordered_set<ToxPubKey> unknown_agents; // list of agents we read about but dont have in command/saging frontier

	// contains remote changes with missing parent seq
	// could merge into comamnd_lists
	std::unordered_map<ToxPubKey, std::unordered_map<uint64_t, Command>> buffer;

	std::atomic_bool should_gossip_local{false}; // local changes (set by main thread, reset by tox thread)
	std::unordered_set<ToxPubKey> should_gossip_remote; // list of ids we have new seq for (only modified by tox thread)
	std::unordered_map<ToxPubKey, uint64_t> heard_gossip; // seq frontiers we have heard about

	// peer ids that requested the last known seq for agent
	std::unordered_set<std::pair<uint32_t, Agent>> requested_frontier;

	// peer ids that requested a command
	std::unordered_map<uint32_t, RequestCommand> requested_commands;

	Tox* tox {nullptr};
	bool tox_dht_online {false};
	bool tox_group_online {false};
	uint32_t tox_group_number {-1u};
	std::unordered_set<uint32_t> tox_seen_peers;
};

namespace tox {

namespace pkg {

	enum PKGID : uint8_t {
		FRONTIER = 32,
		REQUEST_FRONTIER,
		REQUEST_FRONTIERS,

		COMMAND,
		REQUEST_COMMAND,
	};

	// send the currently last known seq you have (excluding buffer)
	struct Frontier {
		Agent agent;
		uint64_t seq{0};
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Frontier,
		agent,
		seq
	)

	// request the last known seq another peer has for agent
	struct RequestFrontier {
		Agent agent;
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RequestFrontier,
		agent
	)

	using Command = ::Command;

	// request every command for agent after_seq - until_seq (inclusive)
	//struct RequestCommands {
		//Agent agent;
		//uint64_t after_seq{0};
		//uint64_t until_seq{0};
	//};
	using RequestCommand = ::RequestCommand;

	//NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RequestCommand,
		//agent,
		//seq
	//)

} // namespace pkg

static std::vector<uint8_t> hex2bin(const std::string& str) {
	std::vector<uint8_t> bin{};
	bin.resize(str.size()/2, 0);

	sodium_hex2bin(bin.data(), bin.size(), str.c_str(), str.length(), nullptr, nullptr, nullptr);

	return bin;
}

static std::string bin2hex(const std::vector<uint8_t>& bin) {
	std::string str{};
	str.resize(bin.size()*2, '?');

	// HECK, std is 1 larger than size returns ('\0')
	sodium_bin2hex(str.data(), str.size()+1, bin.data(), bin.size());

	return str;
}

// callbacks
static void log_cb(Tox*, TOX_LOG_LEVEL level, const char *file, uint32_t line, const char *func, const char *message, void *);
static void self_connection_status_cb(Tox*, TOX_CONNECTION connection_status, void *);
//static void friend_request_cb(Tox *tox, const uint8_t *public_key, const uint8_t *message, size_t length, void *);
static void group_custom_packet_cb(Tox* tox, uint32_t group_number, uint32_t peer_id, const uint8_t* data, size_t length, void* user_data);
static void group_custom_private_packet_cb(Tox* tox, uint32_t group_number, uint32_t peer_id, const uint8_t* data, size_t length, void* user_data);

void toxThread(SharedContext* ctx) {
	using namespace std::chrono_literals;

	std::minstd_rand rng{1337};

	TOX_ERR_OPTIONS_NEW err_opt_new;
	Tox_Options* options = tox_options_new(&err_opt_new);
	assert(err_opt_new == TOX_ERR_OPTIONS_NEW::TOX_ERR_OPTIONS_NEW_OK);
	tox_options_set_log_callback(options, log_cb);
#ifndef USE_TEST_NETWORK
	tox_options_set_local_discovery_enabled(options, true);
#endif
	tox_options_set_udp_enabled(options, true);
	tox_options_set_hole_punching_enabled(options, true);
	tox_options_set_tcp_port(options, 0);

	TOX_ERR_NEW err_new;
	ctx->tox = tox_new(options, &err_new);
	tox_options_free(options);
	if (err_new != TOX_ERR_NEW_OK) {
		std::cerr << "tox_new failed with error code " << err_new << "\n";
		ctx->should_quit.store(true);
		return;
	}

	std::cout << "tox instance created\n";

#define CALLBACK_REG(x) tox_callback_##x(ctx->tox, x##_cb)
	CALLBACK_REG(self_connection_status);

	CALLBACK_REG(group_custom_packet);
	CALLBACK_REG(group_custom_private_packet);
#undef CALLBACK_REG

	{ // dht bootstrap
		struct DHT_node {
			const char* ip;
			uint16_t port;
			const char key_hex[TOX_PUBLIC_KEY_SIZE*2 + 1]; // 1 for null terminator
			unsigned char key_bin[TOX_PUBLIC_KEY_SIZE];
		};

		DHT_node nodes[] {
			{"tox.plastiras.org",	33445,	"8E8B63299B3D520FB377FE5100E65E3322F7AE5B20A0ACED2981769FC5B43725", {}}, // 14
			{"tox2.plastiras.org",	33445,	"B6626D386BE7E3ACA107B46F48A5C4D522D29281750D44A0CBA6A2721E79C951", {}}, // 14
		};

		for (size_t i = 0; i < sizeof(nodes)/sizeof(DHT_node); i ++) {
			sodium_hex2bin(
				nodes[i].key_bin, sizeof(nodes[i].key_bin),
				nodes[i].key_hex, sizeof(nodes[i].key_hex)-1,
				NULL, NULL, NULL
			);
			tox_bootstrap(ctx->tox, nodes[i].ip, nodes[i].port, nodes[i].key_bin, NULL);
			// TODO: use extra tcp option to avoid error msgs
			tox_add_tcp_relay(ctx->tox, nodes[i].ip, nodes[i].port, nodes[i].key_bin, NULL);
		}
	}

	// "thread local"
	Agent agent_local;

	while (!ctx->should_quit) {
		// tox iterate
		tox_iterate(ctx->tox, ctx);

		if (!ctx->tox_dht_online) { // first wait for dht
			if (tox_self_get_connection_status(ctx->tox) != TOX_CONNECTION::TOX_CONNECTION_NONE) {
				ctx->tox_dht_online = true;

				std::cout << "tox connected to dht\n";

				// public
				// 87F3EBA4C0D27926F9ED77E2FF9D8F26F9869D71311D0DB4CA857C1E25A40B18 - green_crdt_test1
				const auto chat_id = hex2bin("87F3EBA4C0D27926F9ED77E2FF9D8F26F9869D71311D0DB4CA857C1E25A40B18");
				const std::string_view name {"green_crdt_vim2"};
				ctx->tox_group_number = tox_group_join(ctx->tox, chat_id.data(), reinterpret_cast<const uint8_t*>(name.data()), name.size(), nullptr, 0, nullptr);

				if (!tox_group_self_get_public_key(ctx->tox, ctx->tox_group_number, ctx->agent.data(), nullptr)) {
					std::cerr << "failed to get own pub key\n";
					ctx->should_quit = true;
					ctx->agent_set.set_value();
					return; // fuck everything
				}
				agent_local = ctx->agent;
				ctx->agent_set.set_value();
			}
		} else if (!ctx->tox_group_online) { // then wait for group to connect
			if (tox_group_get_number_groups(ctx->tox) != 0) {
				ctx->tox_group_online = true;
				std::cout << "tox connected to group\n";
			}
		} else { // do the thing
			std::vector<std::pair<Agent, uint64_t>> missing_in_buffer;
			{ // pump from buffer to staging
				const size_t max_commands = 2;
				size_t number_of_commands_done = 0;
				std::vector<Agent> empty_buffers;
				{
					std::lock_guard lg_staging{ctx->staging_mutex};
					for (auto& [agent, buffer] : ctx->buffer) {
						if (buffer.empty()) {
							empty_buffers.push_back(agent);
							continue;
						}
						if (agent == agent_local) {
							// skip ? self
							continue;
						}
						if (number_of_commands_done >= max_commands) {
							break;
						}

						// determain the seq we are looking for in buffer
						uint64_t seq {0};
						if (ctx->staging_frontier.count(agent)) {
							seq = ctx->staging_frontier.at(agent) + 1;
						}

						if (!buffer.count(seq)) { // not in buffer, skip
							// check if old in buffer
							for (const auto& it : buffer) {
								if (it.first < seq) {
									assert(false && "buffer not clean !!");
								}
							}

							//std::cout << "!!! buffer not empty but not next seq\n";
							missing_in_buffer.push_back(std::make_pair(agent, seq));
							continue;
						}

						std::vector<uint64_t> seq_to_remove;
						{ // this can lead to dead locks, if other code does this wrong
							std::lock_guard lg{ctx->command_lists_mutex};
							for (; buffer.count(seq); seq++) {
								ctx->command_lists[agent][seq] = buffer.at(seq);
								ctx->staging_frontier[agent] = seq;
								seq_to_remove.push_back(seq);

								number_of_commands_done++;
								if (number_of_commands_done >= max_commands) {
									break;
								}
							}

							//ctx->staging_frontier[agent] = seq;
						}
						ctx->should_gossip_remote.emplace(agent);

						for (const auto key : seq_to_remove) {
							buffer.erase(key);
						}
						if (buffer.empty()) {
							empty_buffers.push_back(agent);
						}
					}
				} // scope for staging lock
				for (const auto& agent : empty_buffers) {
					ctx->buffer.erase(agent);
				}
			}

			{ // request missing in buffer
				// (every tick lol)
				for (const auto& [agent, seq] : missing_in_buffer) {
					// ask random peer_id we have seen before
					const uint32_t peer_id = *ctx->tox_seen_peers.cbegin() + (rng() % ctx->tox_seen_peers.size());
					const auto status = tox_group_peer_get_connection_status(ctx->tox, ctx->tox_group_number, peer_id, nullptr);
					if (status == TOX_CONNECTION_NONE) {
						// bad luck, skip
						// TODO: do seen peers cleanup
						continue;
					}

					// send request for command
					pkg::RequestCommand rc_pkg{
						agent, seq
					};
					std::vector<uint8_t> data = nlohmann::json::to_msgpack(rc_pkg);
					// prepend pkgid
					data.emplace(data.begin(), static_cast<uint8_t>(pkg::PKGID::REQUEST_COMMAND));

					Tox_Err_Group_Send_Custom_Private_Packet send_err{TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK};
					if (!tox_group_send_custom_private_packet(ctx->tox, ctx->tox_group_number, peer_id, true, data.data(), data.size(), &send_err)) {
						std::cerr << "failed to send command request packet for " << std::dec << seq << " from " << std::hex << (int)agent[0] << " " << std::dec << (int)send_err << "\n";
						assert(send_err != TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_TOO_LONG);
					} else {
						std::cout << "sent command request packet for " << std::dec << seq << " from " << std::hex << (int)agent[0] << " to " << std::dec << peer_id << "\n";
					}
				}
			}

			// request frontier (implicit list of agents)
			// only every couple of second, can get large
			// OR get back random agent and do it often

			{ // handle requests
				// TODO: this lock is trash
				if (!ctx->requested_commands.empty()) {
					std::lock_guard lg{ctx->command_lists_mutex};
					for (const auto& [peer_id, request] : ctx->requested_commands) {
						if (ctx->command_lists.count(request.agent) && ctx->command_lists.at(request.agent).count(request.seq)) {
							const auto& command = ctx->command_lists.at(request.agent).at(request.seq);

							// send command

							std::vector<uint8_t> data = nlohmann::json::to_msgpack(command);
							// prepend pkgid
							data.emplace(data.begin(), static_cast<uint8_t>(pkg::PKGID::COMMAND));

							Tox_Err_Group_Send_Custom_Private_Packet send_err{TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK};
							if (!tox_group_send_custom_private_packet(ctx->tox, ctx->tox_group_number, peer_id, true, data.data(), data.size(), &send_err)) {
								std::cerr << "failed to send command packet " << send_err << "\n";
								assert(send_err != TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_TOO_LONG);
							} else {
								std::cout << "sent requested command to " << peer_id << "\n";
							}
						}
						// else, we dont care. maybe check staging too
					}

					// HACK: clear each tick
					ctx->requested_commands.clear();
					std::cout << "cleared requested commands\n";
				}
			}

			{ // gossip frontier
				// for mutex locking simplicity this is an either-or
				if (ctx->should_gossip_local.exchange(false)) {
					pkg::Frontier f_pkg{
						agent_local,
						0u
					};

					pkg::Command c_pkg{
						agent_local,
						0u,
						{}
					};

					{ // lock
						std::lock_guard lg{ctx->command_lists_mutex};
						assert(ctx->command_frontier.count(agent_local));

						f_pkg.seq = ctx->command_frontier.at(agent_local);

						c_pkg = ctx->command_lists[agent_local][f_pkg.seq];
						assert(!c_pkg.ops.empty());
					}

					{ // gossip
						std::vector<uint8_t> data = nlohmann::json::to_msgpack(f_pkg);
						// prepend pkgid
						data.emplace(data.begin(), static_cast<uint8_t>(pkg::PKGID::FRONTIER));

						Tox_Err_Group_Send_Custom_Packet send_err{TOX_ERR_GROUP_SEND_CUSTOM_PACKET_OK};
						if (!tox_group_send_custom_packet(ctx->tox, 0, true, data.data(), data.size(), &send_err)) {
							std::cerr << "failed to send gossip packet of local agent" << send_err << "\n";
							assert(send_err != TOX_ERR_GROUP_SEND_CUSTOM_PACKET_TOO_LONG);
							// TODO: set should_gossip_local back to true?
						} else {
							std::cout << "sent gossip of local agent\n";
						}
					}
					{ // command
						std::vector<uint8_t> data = nlohmann::json::to_msgpack(c_pkg);
						// prepend pkgid
						data.emplace(data.begin(), static_cast<uint8_t>(pkg::PKGID::COMMAND));

						Tox_Err_Group_Send_Custom_Packet send_err{TOX_ERR_GROUP_SEND_CUSTOM_PACKET_OK};
						if (!tox_group_send_custom_packet(ctx->tox, 0, true, data.data(), data.size(), &send_err)) {
							std::cerr << "failed to send command packet of local agent\n";
							assert(send_err != TOX_ERR_GROUP_SEND_CUSTOM_PACKET_TOO_LONG);
						} else {
							std::cout << "sent command of local agent\n";
						}
					}
				} else if (!ctx->should_gossip_remote.empty()) {
					// we got new remote staged, lets amp the traffic

					// only do first
					auto it = ctx->should_gossip_remote.cbegin();

					pkg::Frontier f_pkg{
						*it,
						0u
					};

					{ // lock
						std::lock_guard lg{ctx->staging_mutex};
						assert(ctx->staging_frontier.count(*it));

						f_pkg.seq = ctx->staging_frontier.at(*it);
					}

					std::vector<uint8_t> data = nlohmann::json::to_msgpack(f_pkg);
					// prepend pkgid
					data.emplace(data.begin(), static_cast<uint8_t>(pkg::PKGID::FRONTIER));

					Tox_Err_Group_Send_Custom_Packet send_err{TOX_ERR_GROUP_SEND_CUSTOM_PACKET_OK};
					if (!tox_group_send_custom_packet(ctx->tox, 0, true, data.data(), data.size(), &send_err)) {
						std::cerr << "failed to send gossip packet " << send_err << "\n";
						assert(send_err != TOX_ERR_GROUP_SEND_CUSTOM_PACKET_TOO_LONG);
					} else {
						std::cout << "sent gossip of remote agent\n";
					}

					ctx->should_gossip_remote.erase(it);
				}
			}
		}

		std::this_thread::sleep_for(20ms);
	}

	tox_kill(ctx->tox);
}

} // namespace tox

std::ostream& operator<<(std::ostream& out, const ToxPubKey& id) {
	out << std::hex << static_cast<int>(id.front());

	return out;
}

std::ostream& operator<<(std::ostream& out, const std::optional<ListType::ListID>& id) {
	if (id.has_value()) {
		out << id.value().id << "-" << id.value().seq;
	} else {
		out << "null";
	}
	return out;
}

std::ostream& operator<<(std::ostream& out, const ListType::OpAdd& op) {
	out
		<< "Add{ id:" << op.id.id
		<< "-" << op.id.seq
		<< ", v:" << op.value
		<< ", l:" << op.parent_left
		<< ", r:" << op.parent_right
		<< " }"
	;
	return out;
}

std::ostream& operator<<(std::ostream& out, const ListType::OpDel& op) {
	out
		<< "Del{ id:" << op.id.id
		<< "-" << op.id.seq
		<< " }"
	;
	return out;
}

std::ostream& operator<<(std::ostream& out, const Doc::Op& op) {
	if (std::holds_alternative<ListType::OpAdd>(op)) {
		out << std::get<ListType::OpAdd>(op);
	} else if (std::holds_alternative<ListType::OpDel>(op)) {
		out << std::get<ListType::OpDel>(op);
	}
	return out;
}

std::ostream& operator<<(std::ostream& out, const std::optional<char>& id) {
	if (id.has_value()) {
		out << id.value();
	} else {
		out << "null";
	}
	return out;
}

std::ostream& operator<<(std::ostream& out, const ListType::Entry& e) {
	out
		<< "{ id:" << e.id.id
		<< "-" << e.id.seq
		<< ", v:" << e.value
		<< ", l:" << e.parent_left
		<< ", r:" << e.parent_right
		<< " }"
	;
	return out;
}

int main(void) {
	SharedContext ctx;
	std::cout << "starting tox thread\n";
	auto tox_thread = std::thread(tox::toxThread, &ctx);

	std::cout << "waiting for agent id\n";
	ctx.agent_set.get_future().wait();
	if (ctx.should_quit) {
		tox_thread.join(); // wait for thread
		return -1;
	}

	std::cout << "starting vim ipc server\n";

	if (zed_net_init() != 0) {
		ctx.should_quit.store(true);
		std::cerr << "zed_net_init failed: " << zed_net_get_error() << "\n";
		tox_thread.join(); // wait for thread
		return -1;
	}

	std::cout << "initialized zed_net\n";

	const uint16_t port_start {1337};
	const uint16_t port_end {1437};
	uint16_t port = port_start;
	zed_net_socket_t listen_socket;
	bool found_free_port {false};
	for (; port <= port_end; port++) {
		if (zed_net_tcp_socket_open(
			&listen_socket,
			port, // port
			0, // non blocking
			1 // listen
		) == 0) {
			found_free_port = true;
			break;
		}
	}

	if (!found_free_port) {
		ctx.should_quit.store(true);
		std::cerr << "zed_net_tcp_socket_open failed: " << zed_net_get_error() << "\n";
		zed_net_shutdown();
		tox_thread.join(); // wait for thread
		return -1;
	}

	std::cout << "listening on " << port << "\n";

	std::cout << "paste these commands into your vim for the current buffer:\n";
	std::cout << "  :let b:channel = ch_open('localhost:" << port << "')\n";

	std::cout << "paste this command to disconnect:\n  :call GreenCRDTStop()\n";

	zed_net_socket_t remote_socket;
	zed_net_address_t remote_address;
	if (zed_net_tcp_accept(&listen_socket, &remote_socket, &remote_address) != 0) {
		ctx.should_quit.store(true);
		std::cerr << "zed_net_tcp_accept failed: " << zed_net_get_error() << "\n";
		zed_net_socket_close(&listen_socket);
		zed_net_shutdown();
		tox_thread.join(); // wait for thread
		return -1;
	}

	std::cout << "got connection from " << zed_net_host_to_str(remote_address.host) << ":" << remote_address.port << "\n";

	std::cout << "sending setup\n";
	vim::sendSetup(&remote_socket);
	// send doauto text changed for inital buffer

	Doc doc;
	doc.local_agent = ctx.agent;

	while (true) {
		// 100MiB
		auto buffer = std::make_unique<std::array<uint8_t, 1024*1024*100>>();

		int64_t bytes_received {0};
		bytes_received = zed_net_tcp_socket_receive(&remote_socket, buffer->data(), buffer->size());
		if (bytes_received < 0) {
			ctx.should_quit.store(true);
			std::cerr << "zed_net_tcp_socket_receive failed: " << zed_net_get_error() << "\n";
			zed_net_socket_close(&remote_socket);
			zed_net_socket_close(&listen_socket);
			zed_net_shutdown();
			tox_thread.join(); // wait for thread
			return -1;
		} else if (bytes_received == 0) {
			std::cout << "got 0 bytes?\n";
			break; // connection closed
		}

		std::cout << "got " << bytes_received << " bytes\n";

		// expect json array(s separated by newlines)
		// TODO: new lines
		auto view = std::string_view{reinterpret_cast<const char*>(buffer->data()), static_cast<size_t>(bytes_received)};
		std::cout << "  raw: " << view;

		auto j = nlohmann::json::parse(view, nullptr, false);
		if (j.is_discarded()) {
			std::cerr << "invalid json\n";
			//break;
			continue; // whatever
		}

		//std::cout << "  j: " << j.dump() << "\n";

		if (!j.is_array()) {
			std::cerr << "json not array!\n";
			break;
		}

		int64_t command_seq = j.at(0);
		auto j_command_data = j.at(1);

		if (!j_command_data.is_array()) {
			std::cerr << "j_command_data not array!\n";
			break;
		}

		for (const auto& j_command : j_command_data) {
			if (!j_command.is_object()) {
				std::cerr << "j_command not obj!\n";
				break;
			}

			if (!j_command.count("cmd")) {
				std::cerr << "j_command does not have a 'cmd' field!\n";
				break;
			}

			if (!j_command.at("cmd").is_string()) {
				std::cerr << "command not a string!\n";
				break;
			}

			std::string command = j_command.at("cmd");

			if (command.empty()) {
				std::cerr << "command string empty!\n";
				break;
			} else if (command == "setup") { // setup callbacks etc, basically the plugin
				std::cout << "sending setup\n";
				vim::sendSetup(&remote_socket);
			} else if (command == "fetch_changes") { // setup callbacks etc, basically the plugin
				// apply changes (some) and gen vim inserts
				std::cout << "got fetch changes\n";

				bool changes {false};
				{ // apply changes
					// TODO: make less locky, we dont need to hold both at the same time etc
					//std::lock_guard lg {ctx.staging_mutex};
					std::scoped_lock sl {ctx.staging_mutex, ctx.command_lists_mutex};

					// TODO: limit number of ops for responsiveness
					for (const auto& [agent, staging_seq] : ctx.staging_frontier) {
						// check if remote newer
						if (!ctx.command_frontier.count(agent) || ctx.command_frontier.at(agent) < staging_seq) {
							uint64_t seq {0};
							if (ctx.command_frontier.count(agent)) {
								seq = ctx.command_frontier[agent] + 1;
							}
							for (; seq <= staging_seq; seq++) {
								assert(ctx.command_lists.count(agent));
								assert(ctx.command_lists.at(agent).count(seq));

								// !! this can get expensive, while we are holding locks :/
								bool r = doc.apply(ctx.command_lists.at(agent).at(seq).ops);
								// TODO: actually this can fail with missing parents of an agent we never heard about before
								if (!r) {
									std::cout << "faild to apply:\n";
									for (const auto& op : ctx.command_lists.at(agent).at(seq).ops) {
										std::cout << "  " << op << "\n";
									}
								}
								assert(r && "this should not happen");
							}

							ctx.command_frontier[agent] = staging_seq;
							changes = true;
						}
					}
				}

				auto j_res_line_list = nlohmann::json::array();
				if (changes) { // external changes
					const auto crdt_text = doc.getText();
					std::string_view text_view {crdt_text};
					for (int64_t i = 1; ; i++) {
						const auto nl_pos = text_view.find_first_of("\n");
						if (nl_pos == std::string_view::npos) {
							// no more lines
							j_res_line_list.push_back(nlohmann::json::array({i, text_view}));
							break;
						} else {
							const auto line = text_view.substr(0, nl_pos);
							j_res_line_list.push_back(nlohmann::json::array({i, line}));

							assert(text_view.size() >= nl_pos+1);
							text_view = text_view.substr(nl_pos+1);
						}
					}
				}

				vim::sendResponse(&remote_socket, command_seq, j_res_line_list);
			} else if (command == "full_buffer") { // vim is sending the full buffer
				// array of lines

				if (!j_command.count("lines")) {
					std::cerr << "lines list empty!\n";
					continue;
				}

				const auto& j_lines = j_command.at("lines");
				if (!j_lines.is_array()) {
					std::cerr << "lines list not an array!\n";
					continue;
				}

				std::string new_text;
				for (size_t i = 0; i < j_lines.size(); i++) {
					if (!j_lines.at(i).empty()) {
						new_text += static_cast<std::string>(j_lines.at(i));
					}

					if (i+1 < j_lines.size()) {
						new_text += "\n";
					}
				}

				//std::cout << "new_text:\n" << new_text << "\n";
				//std::cout << "old_text:\n" << doc.getText() << "\n";
				std::cout << "doc state: ";
				for (const auto& e : doc.state.list) {
					std::cout << e << " ";
				}
				std::cout << "\n";

				const auto ops = doc.merge(new_text);
				if (!ops.empty()) {
					std::cout << "ops.size: " << ops.size() << "\n";
					std::cout << "ops: ";
					for (const auto& op : ops) {
						std::cout << op << " ";
					}
					std::cout << "\n";
				}
				assert(doc.getText() == new_text);

				if (!ops.empty()) {
					// TODO: make something less locky
					// TODO: make method
					{
						std::lock_guard mg{ctx.command_lists_mutex};
						//assert(ctx.command_lists.size() == ctx.command_frontier.size());

						auto& local_command_list = ctx.command_lists[ctx.agent];

						uint64_t seq {0};
						if (ctx.command_frontier.count(ctx.agent)) { // get last own seq
							seq = ctx.command_frontier[ctx.agent] + 1;
						}

						// 5 can be too much
						// 3 seems save, but is slow
						const size_t max_ops {4}; // limit ops per command so we can fit them into packets
						size_t check_op_count {0};
						for (size_t i = 0; i < ops.size(); seq++) {
							// TODO: check
							//size_t chunk_size = std::min(max_ops, ops.size()-i);
							//std::vector<Doc::Op> tmp_ops {ops.cbegin()+i, ops.cbegin()+i+chunk_size};
							std::vector<Doc::Op> tmp_ops;
							for (auto it = ops.cbegin()+i; it != ops.cend() && tmp_ops.size() <= max_ops; it++) {
								tmp_ops.push_back(*it);
							}

							assert(!tmp_ops.empty());

							local_command_list.emplace(seq, Command{
								ctx.agent,
								seq,
								tmp_ops
							});
							ctx.command_frontier[ctx.agent] = seq;

							i += tmp_ops.size();
							check_op_count += tmp_ops.size();
						}
						assert(check_op_count == ops.size());
					}
					ctx.should_gossip_local.store(true);
				}
			} else {
				std::cout << "unknown command '" << command << "'\n";
			}
		}
	}

	std::cout << "shutting down\n";

	ctx.should_quit.store(true);

	tox_thread.join(); // wait for thread

	zed_net_socket_close(&remote_socket);
	zed_net_socket_close(&listen_socket);
	zed_net_shutdown();
	return 0;
}

namespace tox {

static void log_cb(Tox*, TOX_LOG_LEVEL level, const char *file, uint32_t line, const char *func, const char *message, void *) {
	std::cerr << "TOX " << level << " " << file << ":" << line << "(" << func << ") " << message << "\n";
}

static void self_connection_status_cb(Tox*, TOX_CONNECTION connection_status, void *) {
	std::cout << "self_connection_status_cb " << connection_status << "\n";
}

static void handle_pkg(SharedContext& ctx, const uint8_t* data, size_t length, uint32_t peer_id) {
	if (length < 2) {
		std::cerr << "got too short pkg " << length << "\n";
		return;
	}

	pkg::PKGID pkg_id = static_cast<pkg::PKGID>(data[0]);
	const auto p_j = nlohmann::json::from_msgpack(data+1, data+1 + (length-1), true, false);
	if (p_j.is_discarded()) {
		std::cerr << "got invalid msgpack for " << pkg_id << "\n";
		return;
	}

	// TODO: keep track of time/connected disconnected
	ctx.tox_seen_peers.emplace(peer_id);

	std::cout << "pkg " << pkg_id << " j:" << p_j.dump() << "\n";

	switch (pkg_id) {
		case pkg::PKGID::FRONTIER: {
			pkg::Frontier pkg = p_j;

			if (!ctx.heard_gossip.count(pkg.agent) || ctx.heard_gossip[pkg.agent] < pkg.seq) {
				ctx.heard_gossip[pkg.agent] = pkg.seq;
				std::cout << "new seq " << pkg.seq << " from " << pkg.agent << "\n";
			}
			break;
		}
		case pkg::PKGID::REQUEST_FRONTIER: {
			pkg::RequestFrontier pkg = p_j;
			ctx.requested_frontier.emplace(peer_id, pkg.agent);
			break;
		}
		case pkg::PKGID::COMMAND: {
			pkg::Command pkg = p_j;

			// push to buffer, if not in buffer
			if (!ctx.buffer[pkg.agent].count(pkg.seq)) {
				{ // also check staging frontier, if its not a dup
					std::lock_guard lg {ctx.staging_mutex};
					if (ctx.staging_frontier.count(pkg.agent) && pkg.seq <= ctx.staging_frontier.at(pkg.agent)) {
						break; // allready in staging or master
					}
				}
				ctx.buffer[pkg.agent].emplace(pkg.seq, pkg);
				std::cout << "pushed to buffer " << pkg.seq << " from " << pkg.agent << "\n";
			}
			// TODO: notify something?
			break;
		}
		case pkg::PKGID::REQUEST_COMMAND: {
			pkg::RequestCommand pkg = p_j;
			// TODO: this can lead to double requests
			// TODO: maybe settle for single seq requests for now?, since they are indivitual packets anyway
			ctx.requested_commands[peer_id] = pkg;
			break;
		}
		default:
			std::cerr << "unknown pkg id " << pkg_id << "\n";
			break;
	}
}

static void group_custom_packet_cb(Tox*, uint32_t group_number, uint32_t peer_id, const uint8_t* data, size_t length, void* user_data) {
	std::cout << "group_custom_packet_cb\n";
	SharedContext& ctx = *static_cast<SharedContext*>(user_data);
	assert(ctx.tox_group_number == group_number);
	handle_pkg(ctx, data, length, peer_id);
}

static void group_custom_private_packet_cb(Tox*, uint32_t group_number, uint32_t peer_id, const uint8_t* data, size_t length, void* user_data) {
	std::cout << "group_custom_private_packet_cb\n";
	SharedContext& ctx = *static_cast<SharedContext*>(user_data);
	assert(ctx.tox_group_number == group_number);
	handle_pkg(ctx, data, length, peer_id);
}

}

