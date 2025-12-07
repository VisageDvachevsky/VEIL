#include "transport/mux/retransmit_buffer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace veil::mux {

RetransmitBuffer::RetransmitBuffer(RetransmitConfig config, std::function<TimePoint()> now_fn)
    : config_(config),
      now_fn_(std::move(now_fn)),
      estimated_rtt_(config_.initial_rtt),
      current_rto_(config_.initial_rtt) {}

bool RetransmitBuffer::insert(std::uint64_t sequence, std::vector<std::uint8_t> data) {
  if (buffered_bytes_ + data.size() > config_.max_buffer_bytes) {
    return false;
  }
  if (pending_.count(sequence) != 0) {
    return false;  // Already tracking this sequence
  }

  const auto now = now_fn_();
  const auto rto = current_rto_;
  PendingPacket pkt{
      .sequence = sequence,
      .data = std::move(data),
      .first_sent = now,
      .last_sent = now,
      .next_retry = now + rto,
      .retry_count = 0,
  };

  buffered_bytes_ += pkt.data.size();
  stats_.bytes_sent += pkt.data.size();
  ++stats_.packets_sent;
  pending_.emplace(sequence, std::move(pkt));
  return true;
}

bool RetransmitBuffer::acknowledge(std::uint64_t sequence) {
  auto it = pending_.find(sequence);
  if (it == pending_.end()) {
    return false;
  }

  const auto& pkt = it->second;
  // Only update RTT if this wasn't retransmitted (Karn's algorithm).
  if (pkt.retry_count == 0) {
    const auto now = now_fn_();
    const auto rtt_sample = std::chrono::duration_cast<std::chrono::milliseconds>(now - pkt.first_sent);
    update_rtt(rtt_sample);
  }

  buffered_bytes_ -= pkt.data.size();
  ++stats_.packets_acked;
  pending_.erase(it);
  return true;
}

void RetransmitBuffer::acknowledge_cumulative(std::uint64_t sequence) {
  auto it = pending_.begin();
  while (it != pending_.end() && it->first <= sequence) {
    const auto& pkt = it->second;
    if (pkt.retry_count == 0) {
      const auto now = now_fn_();
      const auto rtt_sample =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - pkt.first_sent);
      update_rtt(rtt_sample);
    }
    buffered_bytes_ -= pkt.data.size();
    ++stats_.packets_acked;
    it = pending_.erase(it);
  }
}

std::vector<const PendingPacket*> RetransmitBuffer::get_packets_to_retransmit() {
  std::vector<const PendingPacket*> result;
  const auto now = now_fn_();
  for (const auto& [seq, pkt] : pending_) {
    if (now >= pkt.next_retry) {
      result.push_back(&pkt);
    }
  }
  return result;
}

bool RetransmitBuffer::mark_retransmitted(std::uint64_t sequence) {
  auto it = pending_.find(sequence);
  if (it == pending_.end()) {
    return false;
  }

  auto& pkt = it->second;
  ++pkt.retry_count;
  if (pkt.retry_count > config_.max_retries) {
    return false;  // Exceeded max retries
  }

  // Calculate backoff: RTO * backoff_factor^retry_count
  const auto rto_ms = static_cast<double>(current_rto_.count());
  const auto backoff = static_cast<std::int64_t>(
      rto_ms * std::pow(config_.backoff_factor, static_cast<double>(pkt.retry_count)));
  const auto capped_backoff =
      std::min<std::int64_t>(backoff, config_.max_rto.count());

  const auto now = now_fn_();
  pkt.last_sent = now;
  pkt.next_retry = now + std::chrono::milliseconds(capped_backoff);

  stats_.bytes_retransmitted += pkt.data.size();
  ++stats_.packets_retransmitted;
  return true;
}

void RetransmitBuffer::drop_packet(std::uint64_t sequence) {
  auto it = pending_.find(sequence);
  if (it == pending_.end()) {
    return;
  }
  buffered_bytes_ -= it->second.data.size();
  ++stats_.packets_dropped;
  pending_.erase(it);
}

void RetransmitBuffer::update_rtt(std::chrono::milliseconds sample) {
  if (!rtt_initialized_) {
    // First sample: initialize directly (RFC 6298 section 2.2)
    estimated_rtt_ = sample;
    rtt_variance_ = sample / 2;
    rtt_initialized_ = true;
  } else {
    // Subsequent samples: EWMA update (RFC 6298 section 2.3)
    // RTTVAR <- (1 - beta) * RTTVAR + beta * |SRTT - R'|
    // SRTT <- (1 - alpha) * SRTT + alpha * R'
    const auto diff = static_cast<double>(std::abs(estimated_rtt_.count() - sample.count()));
    const auto var_count = static_cast<double>(rtt_variance_.count());
    const auto est_count = static_cast<double>(estimated_rtt_.count());
    const auto samp_count = static_cast<double>(sample.count());
    rtt_variance_ = std::chrono::milliseconds(
        static_cast<std::int64_t>((1.0 - config_.rtt_beta) * var_count +
                                   config_.rtt_beta * diff));
    estimated_rtt_ = std::chrono::milliseconds(
        static_cast<std::int64_t>((1.0 - config_.rtt_alpha) * est_count +
                                   config_.rtt_alpha * samp_count));
  }
  current_rto_ = calculate_rto();
}

std::chrono::milliseconds RetransmitBuffer::calculate_rto() const {
  // RTO = SRTT + max(G, K * RTTVAR) where G is clock granularity, K = 4
  // We ignore G (assume fine-grained clock) and use K = 4.
  const auto rto = estimated_rtt_ + 4 * rtt_variance_;
  const auto clamped =
      std::clamp(rto.count(), config_.min_rto.count(), config_.max_rto.count());
  return std::chrono::milliseconds(clamped);
}

}  // namespace veil::mux
