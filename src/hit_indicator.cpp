#include "hit_indicator.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>

#include "cameraunlock/hooks/hook_manager.h"
#include "build_profile.h"
#include "debug_log.h"
#include "rui_transform.h"

namespace headtracking {

namespace {

// uint32_t RuiCreate(RuiAsset* asset, uint32_t topologyId, char drawGroup,
//                    int16_t sortKey)
using RuiCreateFn = uint32_t(*)(void*, uint32_t, char, int16_t);
RuiCreateFn g_original = nullptr;

// What the loaded asset calls itself. Script asks for `$"ui/hit_indicator.rpak"`
// but the asset carries the bare stem, so this is matched as a substring rather
// than compared - and bounded, because the pointer it is read through is the
// game's.
constexpr char kAssetName[] = "hit_indicator";
constexpr int kMaxNameLength = 128;

// The client's RUI instance table, which RuiCreate fills before returning: the
// handle at entry+0x00 (instance index in the low 11 bits, a generation counter
// above it), the topology id at +0x04, the instance itself at +0x08. Comparing
// the handle back against the entry is what makes the lookup safe - a stale or
// impossible handle indexes an entry that does not carry it.
constexpr uint32_t kHandleIndexMask = 0x7FFu;
constexpr uint32_t kEntryStride = 0x20u;
constexpr uint32_t kEntryInstance = 0x08u;
constexpr uint32_t kNoHandle = 0xFFFFFFFFu;

// A transform block is a header and two identical records, each an origin and
// the two axes that span the frame: {origin(x,y,z,1), xAxis(w,0,0,0),
// yAxis(0,h,0,0)}, in PIXELS. The profile pins where the first record's origin
// sits; the rest is the shape of the record. The game's own per-frame topology
// update writes both records the same, so both get the offset - leaving one
// behind would be the odd choice, and which record an asset draws through is the
// asset's business.
constexpr uint32_t kAxisXFromOrigin = 0x10u;
constexpr uint32_t kAxisYFromOrigin = 0x20u;
constexpr uint32_t kRecordStride = 0x30u;
constexpr size_t kBlockSize = 0x100u;

// The asset, once one has been seen. Identifying by pointer after that is a
// compare rather than a string walk per RUI the game creates, and there is one
// hit indicator asset per session.
const void* g_asset = nullptr;

// The game's own block for the topology the indicator is created in, and our
// private copy of it with the crosshair's offset added. The copy is what the
// adopted instances are pointed at; the topology itself is never written, so the
// rest of the HUD sharing it stays where it is.
const uint8_t* g_topology = nullptr;
alignas(16) uint8_t g_block[kBlockSize];

// Written by the render hook, read wherever the block is refreshed. RuiCreate
// runs on the thread the client script runs on, the publish on the one that
// renders; the only datum crossing between them is the block pointer, whose
// write is a single aligned store. A frame that reads the block mid-refresh
// draws the mark a few pixels out for that frame, which is the same race the
// game's own per-frame topology update already has.
bool g_visible = false;
float g_ndcX = 0.0f;
float g_ndcY = 0.0f;

// Each logged once. Between them they say which half of the path is live
// without needing a screenshot taken during the half-second the mark is on
// screen: the asset line says the indicator was recognised, the basis line says
// it is being drawn through a block we are moving, and either of the two failure
// lines says which step gave up.
bool g_loggedShape = false;
bool g_loggedBasis = false;
bool g_loggedLookup = false;

// A screen-space basis: a homogeneous origin, an x axis that is the frame's
// width along x and a y axis that is its height along y. That is the block the
// crosshair's own offset was measured against, and it is the only shape the
// pixel offset below means anything in - a topology that turned out to be a
// homography onto a titan cockpit would need the offset resolved in its plane,
// not added to its origin.
bool ShapeIsScreenBasis(const float* record) {
    const float* origin = record;
    const float* axisX = record + kAxisXFromOrigin / sizeof(float);
    const float* axisY = record + kAxisYFromOrigin / sizeof(float);
    return origin[3] == 1.0f && axisX[3] == 0.0f && axisY[3] == 0.0f
        && axisX[0] > 0.0f && axisX[1] == 0.0f && axisX[2] == 0.0f
        && axisY[1] > 0.0f && axisY[0] == 0.0f && axisY[2] == 0.0f;
}

// Copies the topology the game would have drawn the indicator through and adds
// the crosshair's offset. Run every frame rather than once, because the topology
// is rebuilt every frame by the game and a copy taken at creation would go stale
// the moment anything resized it.
//
// Split out with no C++ objects in it so the __try has nothing to unwind past.
void RefreshBlock() {
    const uint8_t* topology = g_topology;
    if (!topology) return;
    const uint32_t org = ActiveProfile().offsets.rui_transform_origin;
    const size_t length = org + 2 * kRecordStride;
    if (length > kBlockSize) return;

    __try {
        std::memcpy(g_block, topology, length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // The topology table is static storage in client.dll, so this does not
        // happen on a build whose profile is right - and on one whose profile is
        // wrong, dropping the pointer leaves every indicator where the game put
        // it instead of faulting once per frame.
        g_topology = nullptr;
        return;
    }

    float* recordA = reinterpret_cast<float*>(g_block + org);
    if (!ShapeIsScreenBasis(recordA)) {
        if (!g_loggedShape) {
            g_loggedShape = true;
            HT_LOG("[hit] the hit indicator's topology is not a screen-space basis - it stays in "
                   "the middle of the frame. Please report this log.");
        }
        g_topology = nullptr;
        return;
    }

    if (!g_loggedBasis) {
        g_loggedBasis = true;
        HT_LOG("[hit] hit marks now follow the crosshair; the frame they are placed in is "
               "%.0f x %.0f pixels",
               recordA[kAxisXFromOrigin / sizeof(float)],
               recordA[kAxisYFromOrigin / sizeof(float) + 1]);
    }

    float px = 0.0f, py = 0.0f;
    if (g_visible) {
        RuiNdcToPixels(g_ndcX, g_ndcY, recordA[kAxisXFromOrigin / sizeof(float)],
                       recordA[kAxisYFromOrigin / sizeof(float) + 1], px, py);
    }
    float* recordB = recordA + kRecordStride / sizeof(float);
    recordA[0] += px;
    recordA[1] += py;
    recordB[0] += px;
    recordB[1] += py;
}

// Points one freshly created instance at our block, and remembers the block the
// game gave it so the copy has something to track. Nothing logs from in here -
// the __try has to stay clear of anything that takes a lock, because SEH runs no
// destructors and a fault taken inside the logger would leave its mutex held.
bool Adopt(uint32_t handle) {
    if (handle == kNoHandle) return true;
    uint8_t* entry = reinterpret_cast<uint8_t*>(ClientBase()
                                                + ActiveProfile().offsets.rui_instance_table)
                   + (handle & kHandleIndexMask) * kEntryStride;
    const uint32_t transformOffset = ActiveProfile().offsets.rui_instance_transform;

    __try {
        if (*reinterpret_cast<const uint32_t*>(entry) != handle) return false;
        uint8_t* instance = *reinterpret_cast<uint8_t**>(entry + kEntryInstance);
        if (!instance) return false;
        uint8_t** transform = reinterpret_cast<uint8_t**>(instance + transformOffset);
        // An instance created with no topology has nowhere to be moved to, and
        // one already pointing at our block would make the block a copy of
        // itself.
        if (!*transform || *transform == g_block) return true;
        g_topology = *transform;
        *transform = g_block;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Bounded, and stops at the first byte that is not printable: the pointer is the
// game's and the name is only a name.
bool NameMatches(const void* asset) {
    __try {
        const char* name = *reinterpret_cast<const char* const*>(asset);
        if (!name) return false;
        int length = 0;
        while (length < kMaxNameLength) {
            const char c = name[length];
            if (c == '\0') break;
            if (c < 0x20 || c > 0x7e) return false;
            ++length;
        }
        const int needle = static_cast<int>(sizeof(kAssetName)) - 1;
        for (int i = 0; i + needle <= length; ++i) {
            if (std::memcmp(name + i, kAssetName, needle) == 0) return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint32_t Hook_RuiCreate(void* asset, uint32_t topologyId, char drawGroup, int16_t sortKey) {
    const uint32_t handle = g_original(asset, topologyId, drawGroup, sortKey);
    if (!asset) return handle;
    // The pointer is only a fast path for the asset we matched last: a level
    // transition unloads the rpak and the next one comes back at a different
    // address, so the name still has to be read whenever the pointer is new.
    if (asset != g_asset) {
        if (!NameMatches(asset)) return handle;
        g_asset = asset;
        HT_LOG("[hit] hit indicator asset found; hit marks now land on the crosshair "
               "instead of the middle of the frame");
    }
    if (!Adopt(handle) && !g_loggedLookup) {
        g_loggedLookup = true;
        HT_LOG("[hit] the RUI instance table does not hold the handle RuiCreate returned - hit "
               "marks stay in the middle of the frame. Please report this log.");
    }
    // The instance is drawn this frame, so the block has to be right now rather
    // than at the next publish.
    RefreshBlock();
    return handle;
}

}  // namespace

void PublishHitIndicator(bool visible, float ndcX, float ndcY) {
    g_visible = visible;
    g_ndcX = ndcX;
    g_ndcY = ndcY;
    RefreshBlock();
}

bool InstallHitIndicatorHook() {
    const auto& off = ActiveProfile().offsets;
    if (off.rui_create_rva == 0 || off.rui_instance_table == 0) {
        HT_LOG("[hit] no RUI create offsets for this build - hit marks stay in the middle of "
               "the frame");
        return false;
    }
    void* target = reinterpret_cast<void*>(ClientBase() + off.rui_create_rva);
    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    const auto st = hooks.CreateHook(target, reinterpret_cast<void*>(&Hook_RuiCreate),
                                     reinterpret_cast<void**>(&g_original));
    if (st != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[hit] CreateHook failed: %s", cameraunlock::hooks::HookStatusToString(st));
        return false;
    }
    if (hooks.EnableHook(target) != cameraunlock::hooks::HookStatus::Ok) {
        HT_LOG("[hit] EnableHook failed");
        return false;
    }
    HT_LOG("[hit] RUI create hook installed at %p", target);
    return true;
}

}  // namespace headtracking
