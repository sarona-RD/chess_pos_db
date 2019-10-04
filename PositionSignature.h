#pragma once

#include "Position.h"

#include "lib/xxhash/xxhash_cpp.h"

#include <cstring>
#include <functional>

// currently only uses hash
// currently doesn't differentiate positions by available legal moves
// TODO: loseless compression later

struct PositionSignature
{
    using StorageType = std::array<std::uint32_t, 4>;

    PositionSignature() = default;

    PositionSignature(const Position& pos)
    {
        auto h = xxhash::XXH3_128bits(pos.piecesRaw(), 64);
        std::memcpy(m_hash.data(), &h, sizeof(StorageType));
        m_hash[0] ^= ordinal(pos.sideToMove());
    }

    PositionSignature(const PositionSignature&) = default;
    PositionSignature(PositionSignature&&) = default;
    PositionSignature& operator=(const PositionSignature&) = default;
    PositionSignature& operator=(PositionSignature&&) = default;

    [[nodiscard]] friend bool operator==(const PositionSignature& lhs, const PositionSignature& rhs) noexcept
    {
        return
            lhs.m_hash[0] == rhs.m_hash[0]
            && lhs.m_hash[1] == rhs.m_hash[1]
            && lhs.m_hash[2] == rhs.m_hash[2]
            && lhs.m_hash[3] == rhs.m_hash[3];
    }

    [[nodiscard]] friend bool operator<(const PositionSignature& lhs, const PositionSignature& rhs) noexcept
    {
        if (lhs.m_hash[0] < rhs.m_hash[0]) return true;
        else if (lhs.m_hash[0] > rhs.m_hash[0]) return false;

        if (lhs.m_hash[1] < rhs.m_hash[1]) return true;
        else if (lhs.m_hash[1] > rhs.m_hash[1]) return false;

        if (lhs.m_hash[2] < rhs.m_hash[2]) return true;
        else if (lhs.m_hash[2] > rhs.m_hash[2]) return false;

        return (lhs.m_hash[3] < rhs.m_hash[3]);
    }

    [[nodiscard]] const StorageType& hash() const
    {
        return m_hash;
    }

private:
    StorageType m_hash;
};
static_assert(sizeof(PositionSignature) == 16);

namespace std
{
    template<> struct hash<PositionSignature>
    {
        using argument_type = PositionSignature;
        using result_type = std::size_t;
        [[nodiscard]] result_type operator()(const argument_type& s) const noexcept
        {
            return (static_cast<std::size_t>(s.hash()[0]) << 32) | s.hash()[1];
        }
    };
}

struct PositionSignatureWithReverseMove
{
    using StorageType = std::array<std::uint32_t, 4>;

    PositionSignatureWithReverseMove() = default;

    PositionSignatureWithReverseMove(const Position& pos, const ReverseMove& reverseMove = ReverseMove{})
    {
        auto h = xxhash::XXH3_128bits(pos.piecesRaw(), 64);
        std::memcpy(m_hash.data(), &h, sizeof(StorageType));
        m_hash[0] ^= ordinal(pos.sideToMove());

        auto packedReverseMove = PackedReverseMove(reverseMove);
        // m_hash[0] is the most significant quad, m_hash[3] is the least significant
        // We want entries ordered with reverse move to also be ordered by just hash
        // so we have to modify the lowest bits.
        m_hash[3] = (m_hash[3] & ~PackedReverseMove::mask) | packedReverseMove.packed();
    }

    PositionSignatureWithReverseMove(const PositionSignatureWithReverseMove&) = default;
    PositionSignatureWithReverseMove(PositionSignatureWithReverseMove&&) = default;
    PositionSignatureWithReverseMove& operator=(const PositionSignatureWithReverseMove&) = default;
    PositionSignatureWithReverseMove& operator=(PositionSignatureWithReverseMove&&) = default;

    [[nodiscard]] const StorageType& hash() const
    {
        return m_hash;
    }

    struct CompareLessWithReverseMove
    {
        [[nodiscard]] bool operator()(const PositionSignatureWithReverseMove& lhs, const PositionSignatureWithReverseMove& rhs) noexcept
        {
            if (lhs.m_hash[0] < rhs.m_hash[0]) return true;
            else if (lhs.m_hash[0] > rhs.m_hash[0]) return false;

            if (lhs.m_hash[1] < rhs.m_hash[1]) return true;
            else if (lhs.m_hash[1] > rhs.m_hash[1]) return false;

            if (lhs.m_hash[2] < rhs.m_hash[2]) return true;
            else if (lhs.m_hash[2] > rhs.m_hash[2]) return false;

            return (lhs.m_hash[3] < rhs.m_hash[3]);
        }
    };

    struct CompareLessWithoutReverseMove
    {
        [[nodiscard]] bool operator()(const PositionSignatureWithReverseMove& lhs, const PositionSignatureWithReverseMove& rhs) noexcept
        {
            if (lhs.m_hash[0] < rhs.m_hash[0]) return true;
            else if (lhs.m_hash[0] > rhs.m_hash[0]) return false;

            if (lhs.m_hash[1] < rhs.m_hash[1]) return true;
            else if (lhs.m_hash[1] > rhs.m_hash[1]) return false;

            if (lhs.m_hash[2] < rhs.m_hash[2]) return true;
            else if (lhs.m_hash[2] > rhs.m_hash[2]) return false;

            return ((lhs.m_hash[3] & ~PackedReverseMove::mask) < (rhs.m_hash[3] & ~PackedReverseMove::mask));
        }
    };

    struct CompareEqualWithReverseMove
    {
        [[nodiscard]] bool operator()(const PositionSignatureWithReverseMove& lhs, const PositionSignatureWithReverseMove& rhs) const noexcept
        {
            return
                lhs.m_hash[0] == rhs.m_hash[0]
                && lhs.m_hash[1] == rhs.m_hash[1]
                && lhs.m_hash[2] == rhs.m_hash[2]
                && lhs.m_hash[3] == rhs.m_hash[3];
        }
    };

    struct CompareEqualWithoutReverseMove
    {
        [[nodiscard]] bool operator()(const PositionSignatureWithReverseMove& lhs, const PositionSignatureWithReverseMove& rhs) const noexcept
        {
            return
                lhs.m_hash[0] == rhs.m_hash[0]
                && lhs.m_hash[1] == rhs.m_hash[1]
                && lhs.m_hash[2] == rhs.m_hash[2]
                && (lhs.m_hash[3] & ~PackedReverseMove::mask) == (rhs.m_hash[3] & ~PackedReverseMove::mask);
        }
    };

private:
    // All bits of the hash are created equal, so we can specify some ordering.
    // Elements ordered from least significant to most significant are [3][2][1][0]
    StorageType m_hash;
};
static_assert(sizeof(PositionSignatureWithReverseMove) == 16);

namespace std
{
    template<> struct hash<PositionSignatureWithReverseMove>
    {
        using argument_type = PositionSignatureWithReverseMove;
        using result_type = std::size_t;
        [[nodiscard]] result_type operator()(const argument_type& s) const noexcept
        {
            // We modify the lowest and highest uint32s so they should be the best
            // candidate for the hash.
            return (static_cast<std::size_t>(s.hash()[0]) << 32) | s.hash()[3];
        }
    };
}
