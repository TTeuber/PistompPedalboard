// ui_events.h -- the bridge from the INPUT thread to the UI thread.
//
// LVGL is single-threaded and lives on the UI/main loop, so the input thread must
// never touch it. Instead the input thread translates raw hardware (encoders,
// switches) into small UiEvents and pushes them onto a lock-free single-producer/
// single-consumer ring; the UI loop drains the ring each frame and drives the
// UiController. Raw, page-agnostic events: the controller decides what an encoder
// turn MEANS on the current page (edit a param vs. move a pedal vs. master).

#pragma once

#include <atomic>
#include <cstdint>

struct UiEvent {
  enum Kind : uint8_t {
    NavRotate,    // nav encoder turn; value = +1 (CW) / -1 (CCW) -> move cursor
    NavSelect,    // nav encoder short push -> select / enter
    Back,         // enc1 push -> up one level
    Enc1Turn,     // tweak knob 1; value = +1 / -1 (param / move pedal, by page)
    Enc2Turn,     // tweak knob 2; value = +1 / -1
    Enc3Turn,     // tweak knob 3; value = +1 / -1 (master, or param on a control page)
    Footswitch,   // FS pressed edge; value = footswitch index 0..3   (phase 3)
    FsHold,       // FS long-press;  value = footswitch index 0..3   (phase 3)
  };
  uint8_t kind = NavRotate;
  int8_t  value = 0;
};

// Lock-free SPSC ring. Producer = input thread (push), consumer = UI thread (pop).
template <int N>
class EventQueue {
public:
  bool push(const UiEvent& e) noexcept {
    const int h = head_.load(std::memory_order_relaxed);
    const int nh = (h + 1) % N;
    if (nh == tail_.load(std::memory_order_acquire)) return false;  // full: drop
    buf_[h] = e;
    head_.store(nh, std::memory_order_release);
    return true;
  }

  bool pop(UiEvent& e) noexcept {
    const int t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false;   // empty
    e = buf_[t];
    tail_.store((t + 1) % N, std::memory_order_release);
    return true;
  }

private:
  UiEvent buf_[N];
  std::atomic<int> head_{0};
  std::atomic<int> tail_{0};
};
