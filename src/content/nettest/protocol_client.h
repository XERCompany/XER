#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "protocol_message.h"
#include "content/network/socket/tcp_client_socket.h"
#include "content/network/socket/socket_stream.h"

namespace XER::Content::NetTest {

struct ExchangeStats {
	std::uint64_t requests_sent = 0;
	std::uint64_t responses_ok = 0;
	std::uint64_t errors = 0;
	std::uint64_t timeouts = 0;
	std::uint64_t bytes_tx = 0;
	std::uint64_t bytes_rx = 0;
	double rtt_avg_ms = 0.0;
	double rtt_min_ms = 0.0;
	double rtt_max_ms = 0.0;
};

// Async line-protocol TCP client built on the existing XER network stack
// (content/network TcpClientSocket + SocketStream::ReadLine) with a
// dedicated reader thread and serialized writes.
//
// RequestResponse() pairs one outgoing serialized message with the next
// inbound line, measuring round-trip latency. Multiple concurrent
// waiters are matched FIFO.
class ProtocolClient {
public:
	ProtocolClient();
	~ProtocolClient();

	ProtocolClient(const ProtocolClient&) = delete;
	ProtocolClient& operator=(const ProtocolClient&) = delete;

	bool Connect(const std::string& host, std::uint16_t port,
		     int timeout_ms, std::string* error_out = nullptr);
	void Disconnect();
	bool IsConnected() const { return connected_.load(); }

	// Serialized request -> waits for one response line.
	bool RequestResponse(const ProtocolMessage& msg,
			     ProtocolMessage* response,
			     double* rtt_ms,
			     int timeout_ms,
			     std::string* error_out = nullptr);

	// Fire-and-forget send (still tracked in stats).
	bool Send(const ProtocolMessage& msg, std::string* error_out = nullptr);

	ExchangeStats Stats() const;
	void ResetStats();

private:
	struct Waiter {
		std::promise<ProtocolMessage> promise;
		std::chrono::steady_clock::time_point deadline;
		std::atomic<bool> cancelled{false};
	};

	void ReaderLoop();
	void HandleIncomingLine(const std::string& line);
	void FailWaiters(const std::string& reason);
	void RecordResponse(double rtt_ms);
	void RecordError();

	// Socket owned by this client; writes serialized via write_mutex_,
	// reads happen only on the reader thread via SocketStream::ReadLine.
	std::unique_ptr<XER::Network::Socket::TcpClientSocket> socket_;

	std::thread reader_thread_;
	std::atomic<bool> running_{false};
	std::atomic<bool> connected_{false};
	std::atomic<bool> stop_requested_{false};

	std::mutex rx_mutex_;
	std::deque<std::shared_ptr<Waiter>> waiters_;
	std::mutex write_mutex_;

	mutable std::mutex stats_mutex_;
	ExchangeStats stats_;
};

}  // namespace XER::Content::NetTest
