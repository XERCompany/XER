#include "protocol_client.h"

#include <algorithm>

namespace XER::Content::NetTest {

ProtocolClient::ProtocolClient() = default;

ProtocolClient::~ProtocolClient() { Disconnect(); }

bool ProtocolClient::Connect(const std::string& host, std::uint16_t port,
			     int timeout_ms, std::string* error_out) {
	(void)timeout_ms;  // Connect is bounded by the OS TCP timeout.
	if (running_.load()) {
		if (error_out) *error_out = "already running";
		return false;
	}

	auto socket = std::make_unique<XER::Network::Socket::TcpClientSocket>();
	std::string err;
	if (!socket->Connect(host, port, &err)) {
		if (error_out) *error_out = err;
		return false;
	}
	if (!socket->stream()) {
		if (error_out) *error_out = "socket stream unavailable";
		return false;
	}

	socket_ = std::move(socket);
	stop_requested_ = false;
	running_ = true;
	connected_ = true;
	reader_thread_ = std::thread(&ProtocolClient::ReaderLoop, this);
	return true;
}

void ProtocolClient::Disconnect() {
	if (!running_.exchange(false)) {
		connected_ = false;
		return;
	}
	stop_requested_ = true;
	connected_ = false;
	FailWaiters("disconnected");

	// Closing the fd unblocks the reader thread's ReadLine.
	if (socket_) socket_->Disconnect();
	if (reader_thread_.joinable()) reader_thread_.join();
	socket_.reset();
}

bool ProtocolClient::Send(const ProtocolMessage& msg,
			  std::string* error_out) {
	if (!connected_.load() || !socket_) {
		if (error_out) *error_out = "not connected";
		return false;
	}
	std::string line = msg.Serialize();
	// SocketStream::ReadLine on the peer expects CRLF termination.
	line += "\r\n";

	std::lock_guard lock(write_mutex_);
	std::string err;
	if (!socket_->stream()->WriteAll(line, &err)) {
		{
			std::lock_guard slock(stats_mutex_);
			stats_.errors++;
		}
		if (error_out) *error_out = err;
		return false;
	}
	{
		std::lock_guard slock(stats_mutex_);
		stats_.requests_sent++;
		stats_.bytes_tx += line.size();
	}
	return true;
}

bool ProtocolClient::RequestResponse(const ProtocolMessage& msg,
				     ProtocolMessage* response,
				     double* rtt_ms, int timeout_ms,
				     std::string* error_out) {
	auto waiter = std::make_shared<Waiter>();
	waiter->deadline = std::chrono::steady_clock::now() +
			   std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 5000);

	{
		std::lock_guard lock(rx_mutex_);
		waiters_.push_back(waiter);
	}
	const auto t0 = std::chrono::steady_clock::now();

	if (!Send(msg, error_out)) {
		{
			std::lock_guard lock(rx_mutex_);
			for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
				if (*it == waiter) {
					waiters_.erase(it);
					break;
				}
			}
		}
		return false;
	}

	auto fut = waiter->promise.get_future();
	if (fut.wait_until(waiter->deadline) != std::future_status::ready) {
		waiter->cancelled = true;
		{
			std::lock_guard lock(stats_mutex_);
			stats_.timeouts++;
		}
		if (error_out) *error_out = "response timeout";
		return false;
	}
	try {
		if (response) *response = fut.get();
	} catch (const std::exception& e) {
		RecordError();
		if (error_out) *error_out = e.what();
		return false;
	}
	const double elapsed = std::chrono::duration<double, std::milli>(
	    std::chrono::steady_clock::now() - t0).count();
	if (rtt_ms) *rtt_ms = elapsed;
	RecordResponse(elapsed);
	return true;
}

ExchangeStats ProtocolClient::Stats() const {
	std::lock_guard lock(stats_mutex_);
	return stats_;
}

void ProtocolClient::ResetStats() {
	std::lock_guard lock(stats_mutex_);
	stats_ = ExchangeStats{};
}

// --- internals ---------------------------------------------------------

void ProtocolClient::ReaderLoop() {
	XER::Network::Socket::SocketStream* stream = socket_->stream();
	while (!stop_requested_.load()) {
		std::string line;
		std::string err;
		if (!stream->ReadLine(&line, &err)) break;
		if (!line.empty() && line.back() == '\n') line.pop_back();
		if (!line.empty() && line.back() == '\r') line.pop_back();
		HandleIncomingLine(line);
	}
	connected_ = false;
	FailWaiters("connection closed");
}

void ProtocolClient::HandleIncomingLine(const std::string& line) {
	if (line.empty()) return;
	ProtocolMessage msg;
	std::string parse_err;
	const bool ok = msg.Parse(line, &parse_err);
	{
		std::lock_guard lock(stats_mutex_);
		stats_.bytes_rx += line.size() + 1;
	}

	std::shared_ptr<Waiter> waiter;
	{
		std::lock_guard lock(rx_mutex_);
		while (!waiters_.empty()) {
			auto front = waiters_.front();
			waiters_.pop_front();
			if (!front->cancelled) {
				waiter = front;
				break;
			}
		}
	}
	if (!waiter) return;  // unsolicited line

	if (!ok) {
		waiter->promise.set_exception(std::make_exception_ptr(
		    std::runtime_error("response parse failed: " + parse_err)));
		RecordError();
		return;
	}
	waiter->promise.set_value(std::move(msg));
}

void ProtocolClient::FailWaiters(const std::string& reason) {
	std::deque<std::shared_ptr<Waiter>> drained;
	{
		std::lock_guard lock(rx_mutex_);
		drained.swap(waiters_);
	}
	for (auto& w : drained) {
		if (!w->cancelled) {
			w->promise.set_exception(std::make_exception_ptr(
			    std::runtime_error(reason)));
		}
	}
}

void ProtocolClient::RecordResponse(double rtt_ms) {
	std::lock_guard lock(stats_mutex_);
	stats_.responses_ok++;
	stats_.rtt_min_ms = stats_.responses_ok == 1
				   ? rtt_ms
				   : std::min(stats_.rtt_min_ms, rtt_ms);
	stats_.rtt_max_ms = std::max(stats_.rtt_max_ms, rtt_ms);
	stats_.rtt_avg_ms =
	    ((stats_.rtt_avg_ms * (stats_.responses_ok - 1)) + rtt_ms) /
	    stats_.responses_ok;
}

void ProtocolClient::RecordError() {
	std::lock_guard lock(stats_mutex_);
	stats_.errors++;
}

}  // namespace XER::Content::NetTest
