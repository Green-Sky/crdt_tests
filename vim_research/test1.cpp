#include <crdt/text_document.hpp>

#include <memory>
#include <zed_net.h>

#include <iostream>
#include <cassert>

// single letter agent, for testing only
using Agent = char;
using DocType = GreenCRDT::TextDocument<Agent>;
using ListType = DocType::ListType;

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

	zed_net_socket_t remote_socket;
	zed_net_address_t remote_address;
	if (zed_net_tcp_accept(&listen_socket, &remote_socket, &remote_address) != 0) {
		std::cerr << "zed_net_tcp_accept failed: " << zed_net_get_error() << "\n";
		zed_net_socket_close(&listen_socket);
		zed_net_shutdown();
		return -1;
	}

	std::cout << "got connection from " << zed_net_host_to_str(remote_address.host) << ":" << remote_address.port << "\n";

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
		// do data decoding
	}

	std::cout << "shutting down\n";

	zed_net_socket_close(&listen_socket);
	zed_net_shutdown();
	return 0;
}

