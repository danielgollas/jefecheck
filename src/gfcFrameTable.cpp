// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcFrameTable.h"

namespace {
// Resident and Evicting frames still hold a texture: both show in segments.
bool hasTexture(gfcFrameState s) { return s == gfcFrameState::Resident || s == gfcFrameState::Evicting; }
}  // namespace

void gfcFrameTable::reset(int frameCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.assign(frameCount > 0 ? size_t(frameCount) : 0, Entry());
    onScreen_ = -1;
    pinnedCount_ = 0;
    residentCount_ = 0;
    residentBytes_ = 0;
    reservedBytes_ = 0;
    ++version_;
}

int gfcFrameTable::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)entries_.size();
}

gfcFrameState gfcFrameTable::state(int i) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inRange(i) ? entries_[i].state : gfcFrameState::Unloaded;
}

size_t gfcFrameTable::bytes(int i) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inRange(i) ? entries_[i].bytes : 0;
}

bool gfcFrameTable::setDecoding(int i, size_t reservedBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inRange(i) || entries_[i].state != gfcFrameState::Unloaded) return false;
    entries_[i].state = gfcFrameState::Decoding;
    entries_[i].bytes = reservedBytes;
    reservedBytes_ += reservedBytes;
    return true;
}

void gfcFrameTable::setResident(int i, size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inRange(i)) return;
    Entry& e = entries_[i];
    if (e.state == gfcFrameState::Decoding) reservedBytes_ -= e.bytes;
    else if (e.state == gfcFrameState::Resident) { residentBytes_ -= e.bytes; --residentCount_; }
    e.state = gfcFrameState::Resident;
    e.bytes = bytes;
    residentBytes_ += bytes;
    ++residentCount_;
    ++version_;
}

bool gfcFrameTable::setEvicting(int i) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inRange(i) || entries_[i].state != gfcFrameState::Resident) return false;
    Entry& e = entries_[i];
    residentBytes_ -= e.bytes;
    --residentCount_;
    e.bytes = 0;
    e.state = gfcFrameState::Evicting;
    return true;
}

void gfcFrameTable::setUnloaded(int i) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inRange(i)) return;
    Entry& e = entries_[i];
    const bool hadTexture = hasTexture(e.state);
    if (e.state == gfcFrameState::Decoding) reservedBytes_ -= e.bytes;
    else if (e.state == gfcFrameState::Resident) { residentBytes_ -= e.bytes; --residentCount_; }
    e.state = gfcFrameState::Unloaded;
    e.bytes = 0;
    if (hadTexture) ++version_;
}

void gfcFrameTable::setFailed(int i) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inRange(i) || entries_[i].state != gfcFrameState::Decoding) return;
    reservedBytes_ -= entries_[i].bytes;
    entries_[i].bytes = 0;
    entries_[i].state = gfcFrameState::Failed;
}

void gfcFrameTable::setPinned(int i, bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inRange(i) || entries_[i].pinned == on) return;
    entries_[i].pinned = on;
    pinnedCount_ += on ? 1 : -1;
}

void gfcFrameTable::pinnedIndices(std::vector<int>& out) const {
    out.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (pinnedCount_ == 0) return;
    for (int i = 0; i < (int)entries_.size(); ++i)
        if (entries_[i].pinned) out.push_back(i);
}

bool gfcFrameTable::pinned(int i) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inRange(i) && entries_[i].pinned;
}

void gfcFrameTable::setOnScreen(int i) {
    std::lock_guard<std::mutex> lock(mutex_);
    onScreen_ = inRange(i) ? i : -1;
}

int gfcFrameTable::onScreen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return onScreen_;
}

int gfcFrameTable::residentCount() const {
    return residentCount_.load(std::memory_order_acquire);
}

size_t gfcFrameTable::residentBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return residentBytes_;
}

size_t gfcFrameTable::reservedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reservedBytes_;
}

uint64_t gfcFrameTable::version() const {
    return version_.load(std::memory_order_acquire);
}

void gfcFrameTable::segments(std::vector<std::pair<int, int>>& out) const {
    out.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    const int n = (int)entries_.size();
    for (int i = 0; i < n;) {
        if (!hasTexture(entries_[i].state)) { ++i; continue; }
        int j = i;
        while (j + 1 < n && hasTexture(entries_[j + 1].state)) ++j;
        out.emplace_back(i, j);
        i = j + 1;
    }
}

void gfcFrameTable::refreshEnds() const {
    const uint64_t v = version_.load();
    if (endsVersion_.load() == v) return;
    int first = -1, last = -1;
    for (int i = 0; i < (int)entries_.size(); ++i)
        if (hasTexture(entries_[i].state)) { if (first < 0) first = i; last = i; }
    first_.store(first);
    last_.store(last);
    endsVersion_.store(v, std::memory_order_release);
}

int gfcFrameTable::firstResident() const {
    // Fast path: the cache is current, no lock.
    const uint64_t v = version_.load(std::memory_order_acquire);
    if (endsVersion_.load(std::memory_order_acquire) == v) return first_.load();
    std::lock_guard<std::mutex> lock(mutex_);
    refreshEnds();
    return first_.load();
}

int gfcFrameTable::lastResident() const {
    const uint64_t v = version_.load(std::memory_order_acquire);
    if (endsVersion_.load(std::memory_order_acquire) == v) return last_.load();
    std::lock_guard<std::mutex> lock(mutex_);
    refreshEnds();
    return last_.load();
}
