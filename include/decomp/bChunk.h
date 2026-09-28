#pragma once

#include <cstdint>

// Portable subset of VishDec's bChunk layout.
// Serialized NFSMW chunks use an 8-byte header: { id, payload_size }.
struct bChunk {
    std::uint32_t ID;
    std::int32_t Size;

    std::uint32_t GetID() const { return ID; }
    std::int32_t GetSize() const { return Size; }
    bool IsNestedChunk() const { return (ID & 0x80000000u) != 0; }
};

static_assert(sizeof(bChunk) == 8, "NFSMW bChunk header must remain 8 bytes");
