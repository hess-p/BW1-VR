#pragma once
#include <windows.h>
#include "protocol.h"

namespace bwvr::bridge {
// Elapsed CPU time includes blocking and scheduling. It is not GPU time.
class Timing {
 public:
  static constexpr unsigned kCopyLeft = 15, kCopyRight = 16,
      kDrainLeft = 17, kDrainRight = 18, kDrainPanel = 19,
      kProducerWait = 20, kPreparePixels = 21, kPrepareCard = 22, kVulkanRelay = 23, kDrainPair = 25, kSlots = 26;
  static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
  void Add(unsigned slot, LONGLONG start) {
    const LONGLONG elapsed = Now() - start;
    if (slot >= kSlots) return;
    auto& s = samples_[slot]; ++s.count; s.total += elapsed;
    if (elapsed > s.maximum) s.maximum = elapsed;
  }
  void Frame(LogFn log, const char* side) {
    ++frames_; if (frames_ == 450) Report(log, side);
  }
  void Report(LogFn log, const char* side) {
    if (!frames_) return;
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    const double ms = 1000.0 / frequency.QuadPart;
    log("bridge time  : %s, window %u, %u End calls. CPU elapsed ms, overlapping categories", side, ++window_, frames_);
    static const char* names[kSlots] = {"unused", "instance", "system", "session",
      "begin", "eye requests", "eye pixels", "panel request", "panel quads", "card",
      "window", "end", "haptic", "stop", "probe", "left copy API", "right copy API",
      "left GPU wait", "right GPU wait", "panel GPU wait", "producer GPU wait",
      "prepare pixels", "prepare card", "Vulkan relay", "eye pair request", "eye pair GPU wait"};
    for (unsigned i = 0; i < kSlots; ++i) {
      const auto& s = samples_[i];
      if (s.count) log("bridge time  : %s %s: %u calls, %.3f ms/call, %.3f ms/End, max %.3f ms",
          side, names[i], s.count, s.total * ms / s.count, s.total * ms / frames_, s.maximum * ms);
    }
    for (auto& s : samples_) s = {};
    frames_ = 0;
  }
 private:
  struct Sample { unsigned count = 0; LONGLONG total = 0, maximum = 0; };
  Sample samples_[kSlots] = {};
  unsigned frames_ = 0, window_ = 0;
};
}
