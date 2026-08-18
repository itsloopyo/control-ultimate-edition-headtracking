// Unit tests for the Coherent View slot table in src/hooks/view_slot_table.h.
//
// The table decides which View a reticle offset is pushed to, and it is a pure
// container over plain values - the caller passes the tick - so it runs headless
// with no game and no UI library.
//
// The property that matters is the one that was broken: Control creates and
// destroys Views for the length of a session, so a table that only ever CLAIMS
// slots fills with dead views and then drops every new one. The reticle stops
// being driven from that point on, silently, which looks exactly like reticle
// compensation never having worked.

#include "hooks/view_slot_table.h"

#include <cstdio>

using ControlHT::ViewPushState;
using ControlHT::ViewSlotTable;

static int g_failures = 0;

static void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

// Views are identified by address; any distinct non-zero values will do.
static uintptr_t View(int n) { return 0x40000000u + static_cast<uintptr_t>(n) * 0x1000u; }

static void TestSameViewKeepsItsSlot() {
    ViewSlotTable table;
    ViewPushState* a = table.Acquire(View(1), 1000);
    ViewPushState* b = table.Acquire(View(2), 1000);
    Check(a != nullptr && b != nullptr, "two views both get a slot");
    Check(a != b, "distinct views get distinct slots");
    Check(table.Acquire(View(1), 1016) == a, "a view keeps its slot across frames");
    Check(table.Acquire(View(2), 1016) == b, "and so does the other one");
}

static void TestFreshSlotStartsUnpushed() {
    ViewSlotTable table;
    ViewPushState* s = table.Acquire(View(1), 1000);
    Check(s->ndcX == ControlHT::kNoOffsetPushedYet && s->ndcY == ControlHT::kNoOffsetPushedYet,
          "a fresh slot is seeded outside any reachable NDC so the first push counts");
    Check(s->lastDefinition == 0, "and is due the shim definition immediately");
}

static void TestLiveViewsAreNotEvicted() {
    ViewSlotTable table;
    // Fill every slot, then keep them all alive by servicing them again.
    for (int i = 0; i < ViewSlotTable::kMaxViews; i++) {
        Check(table.Acquire(View(i), 1000) != nullptr, "table fills");
    }
    for (int i = 0; i < ViewSlotTable::kMaxViews; i++) {
        Check(table.Acquire(View(i), 5000) != nullptr, "every held view is still serviced");
    }
    // A new view arriving while all 64 are live is dropped rather than stealing
    // a slot from one of them: evicting a live view would reset its pushed state
    // and rebuild the whole shim script on the next frame, for every view, for
    // as long as the overflow lasted.
    Check(table.Acquire(View(999), 5000) == nullptr,
          "a 65th view is dropped while all 64 are still being serviced");
}

// The regression. Before reclamation, this returned nullptr forever.
static void TestIdleSlotsAreReclaimed() {
    ViewSlotTable table;
    for (int i = 0; i < ViewSlotTable::kMaxViews; i++) {
        Check(table.Acquire(View(i), 1000) != nullptr, "table fills");
    }
    // View 0 is the one that has gone quiet; everything else is still serviced.
    const uint32_t later = 1000 + ViewSlotTable::kIdleBeforeReclaimMs;
    for (int i = 1; i < ViewSlotTable::kMaxViews; i++) {
        table.Acquire(View(i), later);
    }

    ViewPushState* fresh = table.Acquire(View(999), later);
    Check(fresh != nullptr, "a new view reclaims the slot of one that stopped being serviced");
    if (fresh == nullptr) return;
    Check(fresh->ndcX == ControlHT::kNoOffsetPushedYet,
          "the reclaimed slot is reset, so the new view's first offset is pushed");
    Check(fresh->lastDefinition == 0, "and the new view is due the shim definition");
    Check(fresh->view.load() == View(999), "and the slot now names the new view");

    // The live views kept theirs; only the idle one was taken.
    for (int i = 1; i < ViewSlotTable::kMaxViews; i++) {
        Check(table.Acquire(View(i), later) != nullptr, "live views kept their slots");
    }
}

static void TestReclaimTakesTheStalestSlot() {
    ViewSlotTable table;
    for (int i = 0; i < ViewSlotTable::kMaxViews; i++) {
        // Staggered, so slot 0 is the stalest and slot 63 the freshest.
        table.Acquire(View(i), 1000 + static_cast<uint32_t>(i));
    }
    const uint32_t later = 1000 + ViewSlotTable::kIdleBeforeReclaimMs + 100;
    ViewPushState* fresh = table.Acquire(View(999), later);
    Check(fresh != nullptr, "an idle slot is available");

    // View 0 was the stalest, so it is the one that lost its slot. Asking for it
    // again claims what view 1 had only if the wrong slot was taken.
    Check(table.Acquire(View(1), later) != nullptr, "the second-stalest view still holds a slot");
    Check(fresh != table.Acquire(View(1), later), "the stalest view was the one evicted");
}

// GetTickCount wraps every 49.7 days and the game can be running across it.
static void TestAgeSurvivesTickWraparound() {
    ViewSlotTable table;
    const uint32_t beforeWrap = 0xFFFFFF00u;
    for (int i = 0; i < ViewSlotTable::kMaxViews; i++) {
        table.Acquire(View(i), beforeWrap);
    }
    // 0x200 ms later, which is 0x100 past the wrap. Every slot is now idle by
    // exactly that much - well short of the reclaim window - so nothing may be
    // evicted. Signed or truncating arithmetic here would compute a huge age and
    // evict a live view instead.
    const uint32_t afterWrap = 0x100u;
    Check(table.Acquire(View(999), afterWrap) == nullptr,
          "a tick count that wrapped does not make live slots look ancient");

    const uint32_t wellPast = ViewSlotTable::kIdleBeforeReclaimMs;
    Check(table.Acquire(View(999), wellPast) != nullptr,
          "and a genuinely idle slot across the wrap is still reclaimed");
}

static void TestZeroViewIsRejected() {
    ViewSlotTable table;
    // A null View would otherwise claim a slot that no later view could match,
    // and it is never a real one.
    Check(table.Acquire(0, 1000) == nullptr, "a null view never takes a slot");
    Check(table.Acquire(View(1), 1000) != nullptr, "and the table is untouched by it");
}

int main() {
    TestSameViewKeepsItsSlot();
    TestFreshSlotStartsUnpushed();
    TestLiveViewsAreNotEvicted();
    TestIdleSlotsAreReclaimed();
    TestReclaimTakesTheStalestSlot();
    TestAgeSurvivesTickWraparound();
    TestZeroViewIsRejected();

    if (g_failures == 0) {
        std::printf("All view_slot_table tests passed.\n");
        return 0;
    }
    std::printf("%d view_slot_table test(s) FAILED.\n", g_failures);
    return 1;
}
