#include <crdt/text_document.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string_view>
#include <zed_net.h>

#include <iostream>
#include <cassert>

// single letter agent, for testing only
using Agent = char;
using Doc = GreenCRDT::TextDocument<Agent>;
using ListType = Doc::ListType;

static bool send_command(zed_net_socket_t* remote_socket, const std::string_view mode, const std::string_view command) {
	auto j = nlohmann::json::array();

	j.push_back(mode);
	j.push_back(command);

	std::string str = j.dump();
	str += '\n';

	auto ret = zed_net_tcp_socket_send(remote_socket, str.data(), str.size());
	return ret == 0;
}

static bool send_setup(zed_net_socket_t* remote_socket) {
	return send_command(remote_socket, "ex",

// vars
R"(
let b:green_crdt_timer_can_send = v:true
let b:green_crdt_dirty = v:true
)"

R"(
function! GreenCRDTTimerCallback(timer) abort
	let b:green_crdt_timer_can_send = v:true
	call GreenCRDTCheckTimeAndSendState()
endfunction
)"

R"(
function! GreenCRDTCheckTimeAndSendState() abort
	if b:green_crdt_timer_can_send && b:green_crdt_dirty
		let b:green_crdt_timer_can_send = v:false
		call ch_sendexpr(b:channel, [{'cmd': 'full_buffer', 'lines': getbufline(bufnr(), 1, '$')}])
		let b:green_crdt_dirty = v:false
		call timer_start(100, 'GreenCRDTTimerCallback')
	endif
endfunction
)"

R"(
function! GreenCRDTChangeEvent()
	let b:green_crdt_dirty = v:true
	call GreenCRDTCheckTimeAndSendState()
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
	call ch_close(b:channel)
	delfunction GreenCRDTCheckTimeAndSendState
	delfunction GreenCRDTTimerCallback
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
echo 'setup done'
)");
}

int main(void) {
	std::cout << "starting vim ipc server\n";

	if (zed_net_init() != 0) {
		std::cerr << "zed_net_init failed: " << zed_net_get_error() << "\n";
		return -1;
	}

	std::cout << "initialized zed_net\n";

	const unsigned int port {1337};
	zed_net_socket_t listen_socket;
	if (zed_net_tcp_socket_open(
		&listen_socket,
		port, // port
		0, // non blocking
		1 // listen
	) != 0) {
		std::cerr << "zed_net_tcp_socket_open failed: " << zed_net_get_error() << "\n";
		zed_net_shutdown();
		return -1;
	}

	std::cout << "listening on " << port << "\n";

	std::cout << "paste these commands into your vim for the current buffer:\n";
	std::cout << "  :let b:channel = ch_open('localhost:" << port << "')\n";

	std::cout << "paste this command to disconnect:\n  :call GreenCRDTStop()\n";

	zed_net_socket_t remote_socket;
	zed_net_address_t remote_address;
	if (zed_net_tcp_accept(&listen_socket, &remote_socket, &remote_address) != 0) {
		std::cerr << "zed_net_tcp_accept failed: " << zed_net_get_error() << "\n";
		zed_net_socket_close(&listen_socket);
		zed_net_shutdown();
		return -1;
	}

	std::cout << "got connection from " << zed_net_host_to_str(remote_address.host) << ":" << remote_address.port << "\n";

	std::cout << "sending setup\n";
	send_setup(&remote_socket);
	// send doauto text changed for inital buffer

	Doc doc;
	while (true) {
		// 10MiB
		auto buffer = std::make_unique<std::array<uint8_t, 1024*1024*10>>();

		int64_t bytes_received {0};
		bytes_received = zed_net_tcp_socket_receive(&remote_socket, buffer->data(), buffer->size());
		if (bytes_received < 0) {
			std::cerr << "zed_net_tcp_socket_receive failed: " << zed_net_get_error() << "\n";
			zed_net_socket_close(&remote_socket);
			zed_net_socket_close(&listen_socket);
			zed_net_shutdown();
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
				send_setup(&remote_socket);
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
				for (const auto& line : j_lines) {
					new_text += line;
					new_text += '\n';
				}

				const auto ops = doc.merge(new_text);
				if (!ops.empty()) {
					std::cout << "ops.size: " << ops.size() << "\n";
				}
			} else {
				std::cout << "unknown command '" << command << "'\n";
			}
		}
	}

	std::cout << "shutting down\n";

	zed_net_socket_close(&remote_socket);
	zed_net_socket_close(&listen_socket);
	zed_net_shutdown();
	return 0;
}

