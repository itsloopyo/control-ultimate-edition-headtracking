#pragma once

#include <atomic>
#include <cstdint>

namespace ControlHT {

// What was last pushed to one Coherent View, so an unchanged offset can be
// skipped without a shared threshold hiding the update from every view but the
// first.
//
// Per view rather than global, and reclaimed rather than held forever: Control
// creates and destroys Views as pages come and go, so the addresses seen over a
// session are unbounded while the table is not. Without reclamation the table
// fills with dead views and every later one is dropped, which stops the reticle
// being driven for the rest of the session - silently, because a crosshair that
// has stopped following the aim point looks exactly like one that was never
// wired up.
//
// A pure container over plain values so it runs headless in tests: the caller
// passes the current tick rather than the table reading a clock.

constexpr float kNoOffsetPushedYet = 1e9f;

struct ViewPushState {
    // Claimed with a compare-exchange because Coherent services views on its own
    // threads and nothing here proves there is only one of them.
    std::atomic<uintptr_t> view{0};
    // Seeded outside any reachable NDC so the first update always counts as a
    // change and reaches the view.
    float ndcX = kNoOffsetPushedYet;
    float ndcY = kNoOffsetPushedYet;
    bool visible = true;
    uint32_t lastDefinition = 0;
    uint32_t lastServiced = 0;
};

class ViewSlotTable {
public:
    // Views serviced per frame that offsets are tracked for. Several exist at
    // once (HUD, menus).
    static constexpr int kMaxViews = 64;

    // A slot is only taken from another view once it has gone this long without
    // being serviced. A view that is still being serviced is still alive, and
    // evicting one of those would reset a live view's pushed state every frame -
    // trading a table that fills up for a script rebuilt on every frame.
    static constexpr uint32_t kIdleBeforeReclaimMs = 2000;

    // The slot for `view`, claiming a free one or reclaiming the
    // least-recently-serviced idle one. Null only when the table is full of
    // views that are all still being serviced.
    ViewPushState* Acquire(uintptr_t view, uint32_t nowMs) {
        if (view == 0) return nullptr;

        int reclaim = -1;
        uint32_t reclaimAge = 0;

        for (int i = 0; i < kMaxViews; i++) {
            const uintptr_t held = m_slots[i].view.load(std::memory_order_relaxed);
            if (held == view) {
                m_slots[i].lastServiced = nowMs;
                return &m_slots[i];
            }
            if (held == 0) {
                uintptr_t expected = 0;
                if (m_slots[i].view.compare_exchange_strong(expected, view,
                                                            std::memory_order_relaxed,
                                                            std::memory_order_relaxed)) {
                    Reset(m_slots[i], nowMs);
                    return &m_slots[i];
                }
                // Lost the claim to another thread. It may have been claiming
                // this very view, in which case the slot is still ours to use.
                if (expected == view) {
                    m_slots[i].lastServiced = nowMs;
                    return &m_slots[i];
                }
                continue;
            }
            // Unsigned arithmetic, so a tick count that has wrapped past
            // 0xFFFFFFFF still measures the age correctly.
            const uint32_t age = nowMs - m_slots[i].lastServiced;
            if (age >= kIdleBeforeReclaimMs && age >= reclaimAge) {
                reclaim = i;
                reclaimAge = age;
            }
        }

        if (reclaim < 0) return nullptr;

        ViewPushState& slot = m_slots[reclaim];
        uintptr_t stale = slot.view.load(std::memory_order_relaxed);
        if (stale == view) {
            slot.lastServiced = nowMs;
            return &slot;
        }
        if (!slot.view.compare_exchange_strong(stale, view, std::memory_order_relaxed,
                                               std::memory_order_relaxed)) {
            // Another thread took this slot in the meantime. Dropping this one
            // push is harmless - the heartbeat brings the next one along.
            return nullptr;
        }
        Reset(slot, nowMs);
        return &slot;
    }

private:
    static void Reset(ViewPushState& slot, uint32_t nowMs) {
        slot.ndcX = kNoOffsetPushedYet;
        slot.ndcY = kNoOffsetPushedYet;
        slot.visible = true;
        // Zero rather than nowMs so the shim definition goes out on the first
        // push to a freshly claimed view instead of a heartbeat later.
        slot.lastDefinition = 0;
        slot.lastServiced = nowMs;
    }

    ViewPushState m_slots[kMaxViews];
};

} // namespace ControlHT
