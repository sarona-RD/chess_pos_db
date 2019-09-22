#pragma once

#include "Date.h"
#include "Eco.h"
#include "External.h"
#include "Pgn.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>

namespace persistence
{
    struct HeaderEntry
    {
        static constexpr std::uint16_t unknownPlyCount = std::numeric_limits<std::uint16_t>::max();

        HeaderEntry(ext::Vector<char>& headers, std::size_t offset)
        {
            // there may be garbage at the end
            // we don't care because we have sizes serialized
            (void)headers.readSome(reinterpret_cast<char*>(this), offset, sizeof(HeaderEntry));
        }

        HeaderEntry(const pgn::UnparsedGame& game, std::uint16_t plyCount) :
            m_date(game.date()),
            m_eco(game.eco()),
            m_plyCount(plyCount)
        {
            fillPackedStrings(game);
        }

        HeaderEntry(const pgn::UnparsedGame& game) :
            HeaderEntry(game, game.plyCount(unknownPlyCount))
        {
        }

        [[nodiscard]] const char* data() const
        {
            return reinterpret_cast<const char*>(this);
        }

        [[nodiscard]] std::size_t size() const
        {
            return m_size;
        }

        [[nodiscard]] Date date() const
        {
            return m_date;
        }

        [[nodiscard]] Eco eco() const
        {
            return m_eco;
        }

        [[nodiscard]] std::uint16_t plyCount() const
        {
            return m_plyCount;
        }

        [[nodiscard]] std::string_view event() const
        {
            const std::uint8_t length = m_packedStrings[0];
            return std::string_view(reinterpret_cast<const char*>(&m_packedStrings[1]), length);
        }

        [[nodiscard]] std::string_view white() const
        {
            const std::uint8_t length0 = m_packedStrings[0];
            const std::uint8_t length = m_packedStrings[length0 + 1];
            return std::string_view(reinterpret_cast<const char*>(&m_packedStrings[length0 + 2]), length);
        }

        [[nodiscard]] std::string_view black() const
        {
            const std::uint8_t length0 = m_packedStrings[0];
            const std::uint8_t length1 = m_packedStrings[length0 + 1];
            const std::uint8_t length = m_packedStrings[length0 + length1 + 2];
            return std::string_view(reinterpret_cast<const char*>(&m_packedStrings[length0 + length1 + 3]), length);
        }

    private:
        static constexpr std::size_t maxStringLength = 255;
        static constexpr std::size_t numPackedStrings = 3;

        static_assert(maxStringLength < 256); // it's nice to require only one byte for length

        // We just read sizeof(HeaderEntry), we don't touch anything
        // in packed strings that would be considered 'garbage'
        std::uint16_t m_size;

        Date m_date;
        Eco m_eco;
        std::uint16_t m_plyCount;

        // strings for event, white, black
        // strings are preceeded with length
        std::uint8_t m_packedStrings[(maxStringLength + 1) * numPackedStrings];

        void fillPackedStrings(const pgn::UnparsedGame& game)
        {
            using namespace std::literals;

            const std::string_view event = game.tag("Event"sv);
            const std::string_view white = game.tag("White"sv);
            const std::string_view black = game.tag("Black"sv);

            const std::uint8_t eventSize = static_cast<std::uint8_t>(std::min(event.size(), maxStringLength));
            const std::uint8_t whiteSize = static_cast<std::uint8_t>(std::min(white.size(), maxStringLength));
            const std::uint8_t blackSize = static_cast<std::uint8_t>(std::min(black.size(), maxStringLength));

            std::uint8_t i = 0;
            m_packedStrings[i++] = eventSize;
            event.copy(reinterpret_cast<char*>(&m_packedStrings[i]), eventSize);
            i += eventSize;

            m_packedStrings[i++] = whiteSize;
            white.copy(reinterpret_cast<char*>(&m_packedStrings[i]), whiteSize);
            i += whiteSize;

            m_packedStrings[i++] = blackSize;
            black.copy(reinterpret_cast<char*>(&m_packedStrings[i]), blackSize);
            i += blackSize;

            m_size = sizeof(HeaderEntry) - sizeof(m_packedStrings) + i;
        }
    };
    static_assert(sizeof(HeaderEntry) == 2 + 4 + 2 + 2 + 768);

    struct Header
    {
        static inline const std::filesystem::path headerPath = "header";
        static inline const std::filesystem::path indexPath = "index";

        Header(std::filesystem::path path, std::size_t memory) :
            // here we use operator, to create directories before we try to
            // create files there
            m_path((std::filesystem::create_directories(path), std::move(path))),
            m_header({ m_path / headerPath, ext::OpenMode::Append }, ext::Buffer<char>(ext::numObjectsPerBufferUnit<char>(memory, 2))),
            m_index({ m_path / indexPath, ext::OpenMode::Append }, ext::Buffer<std::size_t>(ext::numObjectsPerBufferUnit<std::size_t>(memory, 2)))
        {
        }

        [[nodiscard]] std::uint32_t addGame(const pgn::UnparsedGame& game)
        {
            return addHeader(HeaderEntry(game));
        }

        [[nodiscard]] std::uint32_t addGame(const pgn::UnparsedGame& game, std::uint16_t plyCount)
        {
            return addHeader(HeaderEntry(game, plyCount));
        }

        [[nodiscard]] std::uint32_t nextGameId() const
        {
            return static_cast<std::uint32_t>(m_index.size());
        }

        [[nodiscard]] std::vector<HeaderEntry> query(const std::vector<std::size_t>& indices)
        {
            std::vector<HeaderEntry> headers;
            headers.reserve(indices.size());

            for (std::size_t idx : indices)
            {
                headers.emplace_back(m_header, m_index[idx]);
            }

            return headers;
        }

    private:
        std::filesystem::path m_path;
        ext::Vector<char> m_header;
        ext::Vector<std::size_t> m_index;

        std::mutex m_mutex;

        // returns the index of the header (not the address)
        [[nodiscard]] std::uint32_t addHeader(const HeaderEntry& entry)
        {
            const std::size_t headerSizeBytes = m_header.size();

            std::unique_lock<std::mutex> lock(m_mutex);
            m_header.append(entry.data(), entry.size());
            m_index.emplace_back(headerSizeBytes);
            return static_cast<std::uint32_t>(m_index.size() - 1u);
        }
    };
}
