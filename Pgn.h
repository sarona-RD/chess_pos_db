#pragma once

#include <vector>
#include <memory>
#include <optional>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>

#include "Assert.h"
#include "Position.h"
#include "San.h"

namespace pgn
{
    enum struct GameResult : std::uint8_t
    {
        WhiteWin,
        BlackWin,
        Draw,
        Unknown
    };

    namespace detail
    {
        // TODO: use pointer ranges and memchr (more than twice as fast) since we have region bounds

        bool isCommentBegin(const char* begin)
        {
            return *begin == '{' || *begin == ';';
        }

        bool isVariationBegin(const char* begin)
        {
            return *begin == '(';
        }

        // Returns a pointer to the closing brace of the comment
        // or a nullptr if the end is not found.
        // Comments cannot be recursive.
        const char* seekCommentEnd(const char* begin)
        {
            ASSERT(isCommentBegin(begin));

            if (*begin == '{')
            {
                return std::strchr(begin + 1u, '}');
            }
            else // if (*begin == ';')
            {
                return std::strchr(begin + 1u, '\n');
            }
        }

        // Returns a pointer to the closing parenthesis of the variation
        // or a nullptr if the end is not found.
        // Variations can be recursive.
        const char* seekVariationEnd(const char* begin)
        {
            ASSERT(isVariationBegin(begin));

            int numUnclosedParens = 1;

            while (numUnclosedParens)
            {
                ++begin;

                begin = std::strpbrk(begin, "(){;");
                if (begin == nullptr)
                {
                    return begin;
                }

                switch (*begin)
                {
                case '{':
                case ';':
                    begin = seekCommentEnd(begin);
                    break;

                case '(':
                    numUnclosedParens += 1;
                    break;

                case ')':
                    numUnclosedParens -= 1;
                    break;
                }
            }

            return begin;
        }

        const char* seekNextMove(const char* begin)
        {
            for (;;)
            {
                // skip characters we don't care about
                while (std::strchr("01234567890. $", *begin) != nullptr) ++begin;

                if (isCommentBegin(begin))
                {
                    begin = seekCommentEnd(begin) + 1u;
                }
                else if (isVariationBegin(begin))
                {
                    begin = seekVariationEnd(begin) + 1u;
                }
                else if (isValidSanMoveStart(*begin))
                {
                    return begin;
                }
                else
                {
                    return nullptr;
                }
            }
        }

        const char* seekTagValue(const char* begin, const char* tag)
        {
            begin = std::strstr(begin, tag);
            if (begin == nullptr)
            {
                return begin;
            }

            return strchr(begin, '\"');
        }

        // begin points to the " character before the result value.
        // It is assumed that the result value is correct
        GameResult parseGameResult(const char* begin)
        {
            ASSERT(*begin == '\"');

            // 3rd character is unique and always exists
            switch (*(begin + 3u))
            {
            case '0':
                return GameResult::WhiteWin;
            case '1':
                return GameResult::BlackWin;
            case '2':
                return GameResult::Draw;
            default:
                return GameResult::Unknown;
            }
        }
    }

    constexpr const char* tagRegionEndSequence = "\n\n";
    constexpr std::size_t tagRegionEndSequenceLength = 2;
    constexpr const char* moveRegionEndSequence = "\n\n";
    constexpr std::size_t moveRegionEndSequenceLength = 2;

    struct UnparsedRegion
    {
        UnparsedRegion(const char* begin, const char* end) noexcept :
            m_begin(begin),
            m_end(end)
        {
        }

        [[nodiscard]] const char* begin() const
        {
            return m_begin;
        }

        [[nodiscard]] const char* end() const
        {
            return m_end;
        }

    private:
        const char* m_begin;
        const char* m_end;
    };

    struct UnparsedGamePositions
    {
    private:

        struct UnparsedPositionsIterator
        {
            struct Sentinel {};

            using value_type = Position;
            using difference_type = std::ptrdiff_t;
            using reference = const Position &;
            using iterator_category = std::input_iterator_tag;
            using pointer = const Position*;

            UnparsedPositionsIterator(UnparsedRegion& moveRegion) noexcept :
                m_moveRegion(moveRegion),
                m_nextCharacter(moveRegion.begin()),
                m_position(Position::startPosition())
            {
                ASSERT(*m_moveRegion.begin() == '1');
                ASSERT(*m_moveRegion.end() == '\n');
            }

            const UnparsedPositionsIterator& operator++()
            {
                const char* sanBegin = detail::seekNextMove(m_nextCharacter);
                const char* sanEnd = sanBegin ? std::strchr(sanBegin, ' ') : nullptr;
                if (sanEnd == nullptr)
                {
                    m_nextCharacter = m_moveRegion.end();
                }
                else
                {
                    const std::size_t sanLength = sanEnd - sanBegin;
                    const Move move = sanToMove(m_position, sanBegin, sanLength);
                    m_position.doMove(move);

                    m_nextCharacter = sanEnd;
                }
            
                return *this;
            }

            [[nodiscard]] bool friend operator==(const UnparsedPositionsIterator& lhs, Sentinel rhs) noexcept
            {
                return lhs.m_nextCharacter == lhs.m_moveRegion.end();
            }

            [[nodiscard]] bool friend operator!=(const UnparsedPositionsIterator& lhs, Sentinel rhs) noexcept
            {
                return !(lhs == rhs);
            }

            [[nodiscard]] const Position& operator*() const
            {
                return m_position;
            }

            [[nodiscard]] const Position* operator->() const
            {
                return &m_position;
            }

        private:
            UnparsedRegion m_moveRegion;
            const char* m_nextCharacter;
            Position m_position;
        };

    public:

        using iterator = UnparsedPositionsIterator;
        using const_iterator = UnparsedPositionsIterator;

        UnparsedGamePositions(UnparsedRegion moveRegion) noexcept :
            m_moveRegion(moveRegion)
        {
            ASSERT(m_moveRegion.begin() != nullptr);
            ASSERT(m_moveRegion.end() != nullptr);
        }

        [[nodiscard]] UnparsedPositionsIterator begin()
        {
            return { m_moveRegion };
        }

        [[nodiscard]] UnparsedPositionsIterator::Sentinel end() const
        {
            return {};
        }

    private:
        UnparsedRegion m_moveRegion;
    };

    struct UnparsedGame
    {
        explicit UnparsedGame() :
            m_tagRegion(nullptr, nullptr),
            m_moveRegion(nullptr, nullptr)
        {
        }

        UnparsedGame(UnparsedRegion tagRegion, UnparsedRegion moveRegion) noexcept :
            m_tagRegion(tagRegion),
            m_moveRegion(moveRegion)
        {
            ASSERT(*m_tagRegion.begin() == '[');
            ASSERT(*m_tagRegion.end() == '\n');
            ASSERT(*m_moveRegion.begin() == '1');
            ASSERT(*m_moveRegion.end() == '\n');
        }

        [[nodiscard]] GameResult result() const
        {
            const char* resultString = detail::seekTagValue(m_tagRegion.begin(), "Result");
            if (resultString == nullptr)
            {
                return GameResult::Unknown;
            }

            return detail::parseGameResult(resultString);
        }

        [[nodiscard]] UnparsedRegion tagRegion() const
        {
            return m_tagRegion;
        }

        [[nodiscard]] UnparsedRegion moveRegion() const
        {
            return m_moveRegion;
        }

        [[nodiscard]] UnparsedGamePositions positions() const
        {
            return UnparsedGamePositions(m_moveRegion);
        }

    private:
        UnparsedRegion m_tagRegion;
        UnparsedRegion m_moveRegion;
    };

    // is supposed to work as a game iterator
    // stores the current game
    struct LazyPgnFileReader
    {
    private:

        struct LazyPgnFileReaderIterator
        {
            struct Sentinel {};

            using value_type = UnparsedGame;
            using difference_type = std::ptrdiff_t;
            using reference = const UnparsedGame &;
            using iterator_category = std::input_iterator_tag;
            using pointer = const UnparsedGame*;

            LazyPgnFileReaderIterator(LazyPgnFileReader& fr) noexcept :
                m_fileReader(&fr)
            {
            }

            const LazyPgnFileReaderIterator& operator++()
            {
                m_fileReader->moveToNextGame();
                return *this;
            }

            [[nodiscard]] bool friend operator==(const LazyPgnFileReaderIterator& lhs, Sentinel rhs) noexcept
            {
                return lhs.isEnd();
            }

            [[nodiscard]] bool friend operator!=(const LazyPgnFileReaderIterator& lhs, Sentinel rhs) noexcept
            {
                return !(lhs == rhs);
            }

            [[nodiscard]] const UnparsedGame& operator*() const
            {
                return m_fileReader->m_game;
            }

            [[nodiscard]] const UnparsedGame* operator->() const
            {
                return &m_fileReader->m_game;
            }

        private:
            LazyPgnFileReader* m_fileReader;

            [[nodiscard]] bool isEnd() const
            {
                return m_fileReader->m_buffer.front() == '\0';
            }
        };

    public:

        using iterator = LazyPgnFileReaderIterator;
        using const_iterator = LazyPgnFileReaderIterator;

        // currently bufferSize must be bigger than the maximum number of bytes taken by a single game
        // TODO: resize buffer when didn't process anything
        static constexpr std::size_t bufferSize = 1024 * 32;

        LazyPgnFileReader(const std::filesystem::path& path) :
            m_file(nullptr, &std::fclose),
            m_buffer(bufferSize + 1), // one spot for '\0',
            m_game{},
            m_firstUnprocessed(m_buffer.data())
        {
            auto strPath = path.string();
            m_file.reset(std::fopen(strPath.c_str(), "r"));

            if (m_file == nullptr)
            {
                m_buffer[0] = '\0';
                return;
            }

            const std::size_t numBytesRead = std::fread(m_buffer.data(), 1, bufferSize, m_file.get());
            m_buffer[numBytesRead] = '\0';

            // find the first game
            moveToNextGame();
        }

        [[nodiscard]] bool isOpen() const
        {
            return m_file != nullptr;
        }

        [[nodiscard]] LazyPgnFileReaderIterator begin()
        {
#if !defined(NDEBUG)
            // only one iterator can be constructed
            // because the file can only be traversed once
            ASSERT(!m_iteratorConstructed);
            m_iteratorConstructed = true;
#endif

            return { *this };
        }

        [[nodiscard]] LazyPgnFileReaderIterator::Sentinel end() const
        {
            return {};
        }

    private:
        void moveToNextGame()
        {
            while(m_buffer.front() != '\0')
            {
                // try find region bounds
                const char* tagend = std::strstr(m_firstUnprocessed, tagRegionEndSequence);
                if (tagend == nullptr)
                {
                    // fetch more data if needed
                    refillBuffer();
                    continue;
                }

                const char* moveend = std::strstr(tagend + tagRegionEndSequenceLength, moveRegionEndSequence);
                if (moveend == nullptr)
                {
                    refillBuffer();
                    continue;
                }

                // we only extract one game at the time

                m_game = UnparsedGame(
                    { m_firstUnprocessed, tagend }, 
                    { tagend + tagRegionEndSequenceLength, moveend }
                );

                m_firstUnprocessed = moveend + moveRegionEndSequenceLength;

                return;
            }
        }

    private:
        std::unique_ptr<FILE, decltype(&std::fclose)> m_file;
        std::vector<char> m_buffer;
        UnparsedGame m_game;
        const char* m_firstUnprocessed; // we don't go back to anything before that
#if !defined(NDEBUG)
        bool m_iteratorConstructed = false;
#endif

        void refillBuffer()
        {
            // copies unprocessed data to the beginning
            // and fills the rest with new data

            // copy to the beginning everything that was not yet read (commited)
            std::size_t numBytesProcessed = m_firstUnprocessed - m_buffer.data();
            if (numBytesProcessed == 0)
            {
                // if we were unable to process anything then scrap the whole buffer
                numBytesProcessed = bufferSize;
            }
            else if (numBytesProcessed != bufferSize)
            {
                std::memmove(m_buffer.data(), m_buffer.data() + numBytesProcessed, bufferSize - numBytesProcessed);
            }

            // fill the buffer and put '\0' at the end
            const std::size_t numBytesLeft = bufferSize - numBytesProcessed;
            const std::size_t numBytesRead = std::fread(m_buffer.data() + numBytesLeft, 1, numBytesProcessed, m_file.get());
            m_buffer[numBytesLeft + numBytesRead] = '\0';

            m_firstUnprocessed = m_buffer.data();
        }
    };


}
