// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Per-frame cache state of one sequence: which source frames are resident on
// the GPU, which are being decoded, and how many bytes each holds. It replaces
// the old contiguous "loaded run", so the loaded set may have holes.
//
// Plain C++ (no GL, Qt or libav), so any TU may include it. Every method takes
// the table's own mutex and is safe from any thread. Indices are 0-based source
// frame indices; an index out of range is ignored (reads return Unloaded/false).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

enum class gfcFrameState : uint8_t { Unloaded, Decoding, Resident, Evicting, Failed };

class gfcFrameTable {
public:
    // All Unloaded, bytes 0, pins and the on-screen frame cleared, ++version.
    void reset(int frameCount);
    int size() const;
    gfcFrameState state(int i) const;
    size_t bytes(int i) const;   // reserved while Decoding, held while Resident, else 0

    // Unloaded -> Decoding, reserving bytes for the decode; false from any other state.
    bool setDecoding(int i, size_t reservedBytes);
    // -> Resident with these bytes (a Decoding frame's reservation is released). ++version.
    void setResident(int i, size_t bytes);
    // Decoding -> Resident with these bytes (the reservation is released), as
    // one step under the table's lock. False (nothing changes) from any other
    // state: the table was reset or the load cancelled while the frame waited.
    bool setResidentIfDecoding(int i, size_t bytes);
    // Resident -> Evicting; its bytes are released now, the texture is freed
    // later on the GL thread. False from any other state.
    bool setEvicting(int i);
    // -> Unloaded from any state. ++version if it was Resident or Evicting.
    void setUnloaded(int i);
    // Decoding -> Failed (the decode did not produce a frame). Ignored otherwise.
    void setFailed(int i);

    // A pinned frame, and the frame on screen, are never chosen as victims.
    void setPinned(int i, bool on);
    bool pinned(int i) const;
    // Pinned frames in index order (cheap when none are pinned).
    void pinnedIndices(std::vector<int>& out) const;
    // Lock-free (atomic), so the GL thread can set it on every draw.
    void setOnScreen(int i);
    int onScreen() const;   // -1 none

    int residentCount() const;       // Resident frames (Evicting ones are already released); lock-free
    size_t residentBytes() const;    // bytes of Resident frames
    size_t reservedBytes() const;    // bytes reserved by Decoding frames
    // Moves whenever the set of Resident|Evicting frames may have changed.
    uint64_t version() const;        // lock-free
    // Inclusive 0-based runs of Resident|Evicting frames, in index order.
    void segments(std::vector<std::pair<int, int>>& out) const;
    // Resident|Evicting ends, -1 when none. Cached per version, so polling them
    // (the timeline asks every tick) takes no lock and walks nothing between changes.
    int firstResident() const;
    int lastResident() const;

private:
    struct Entry {
        gfcFrameState state = gfcFrameState::Unloaded;
        bool pinned = false;
        size_t bytes = 0;   // reserved while Decoding, held while Resident
    };
    bool inRange(int i) const { return i >= 0 && i < (int)entries_.size(); }
    void refreshEnds() const;   // mutex_ held

    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::atomic<int> onScreen_{-1};
    int pinnedCount_ = 0;
    // Atomic so the timeline can poll them without the mutex; writers still
    // change them only while holding mutex_.
    std::atomic<int> residentCount_{0};
    size_t residentBytes_ = 0;
    size_t reservedBytes_ = 0;
    std::atomic<uint64_t> version_{0};
    // The cached ends are atomic too, so a poll between changes takes no lock.
    mutable std::atomic<uint64_t> endsVersion_{~uint64_t(0)};
    mutable std::atomic<int> first_{-1};
    mutable std::atomic<int> last_{-1};
};
