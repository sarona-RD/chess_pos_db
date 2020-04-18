#include "Bcgn.h"

#include "Bitboard.h"
#include "Chess.h"
#include "Date.h"
#include "Eco.h"
#include "GameClassification.h"
#include "MoveIndex.h"
#include "Position.h"

#include "enum/EnumArray.h"

#include "external_storage/External.h"

#include "util/UnsignedCharBufferView.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace bcgn
{
    void BcgnFileHeader::readFrom(const unsigned char* str)
    {
        if (str[0] != 'B'
            || str[1] != 'C'
            || str[2] != 'G'
            || str[3] != 'N')
        {
            invalidHeader();
        }

        const std::uint8_t version_ = str[4];
        const std::uint8_t compressionLevel_ = str[5];
        const std::uint8_t auxCompression_ = str[6];

        if (version_ >= static_cast<unsigned>(BcgnVersion::SIZE)
            || compressionLevel_ >= static_cast<unsigned>(BcgnCompressionLevel::SIZE)
            || auxCompression_ >= static_cast<unsigned>(BcgnAuxCompression::SIZE))
        {
            invalidHeader();
        }

        for (int i = 7; i < traits::bcgnFileHeaderLength; ++i)
        {
            if (str[i] != '\0')
            {
                invalidHeader();
            }
        }

        version = static_cast<BcgnVersion>(version_);
        compressionLevel = static_cast<BcgnCompressionLevel>(compressionLevel_);
        auxCompression = static_cast<BcgnAuxCompression>(auxCompression_);
    }

    [[nodiscard]] std::size_t BcgnFileHeader::writeTo(unsigned char* data)
    {
        std::memset(data, 0, traits::bcgnFileHeaderLength);

        *data++ = 'B';
        *data++ = 'C';
        *data++ = 'G';
        *data++ = 'N';
        *data++ = static_cast<unsigned char>(version);
        *data++ = static_cast<unsigned char>(compressionLevel);
        *data++ = static_cast<unsigned char>(auxCompression);

        return traits::bcgnFileHeaderLength;
    }

    [[noreturn]] void BcgnFileHeader::invalidHeader() const
    {
        throw std::runtime_error("Invalid header.");
    }

    BcgnFlags::BcgnFlags() :
        m_hasCustomStartPos(false),
        m_hasAdditionalTags(false)
    {
    }

    [[nodiscard]] BcgnFlags BcgnFlags::decode(std::uint8_t v)
    {
        return BcgnFlags(
        (v >> 1) & 1,
            v & 1
            );
    }

    void BcgnFlags::clear()
    {
        m_hasCustomStartPos = false;
        m_hasAdditionalTags = false;
    }

    void BcgnFlags::setHasCustomStartPos(bool v)
    {
        m_hasCustomStartPos = v;
    }

    void BcgnFlags::setHasAdditionalTags(bool v)
    {
        m_hasAdditionalTags = v;
    }

    [[nodiscard]] bool BcgnFlags::hasCustomStartPos() const
    {
        return m_hasCustomStartPos;
    }

    [[nodiscard]] bool BcgnFlags::hasAdditionalTags() const
    {
        return m_hasAdditionalTags;
    }

    [[nodiscard]] std::uint8_t BcgnFlags::encode() const
    {
        return
            ((std::uint8_t)m_hasCustomStartPos << 1)
            | (std::uint8_t)m_hasAdditionalTags;
    }

    BcgnFlags::BcgnFlags(bool hasCustomStartPos, bool hasAdditionalTags) :
        m_hasCustomStartPos(hasCustomStartPos),
        m_hasAdditionalTags(hasAdditionalTags)
    {
    }

    namespace detail
    {
        BcgnGameEntryBuffer::BcgnGameEntryBuffer() :
            m_date{},
            m_whiteElo{},
            m_blackElo{},
            m_round{},
            m_eco{},
            m_customStartPos{},
            m_result{},
            m_additionalTags{},
            m_whiteLength{},
            m_white{},
            m_blackLength{},
            m_black{},
            m_eventLength{},
            m_event{},
            m_siteLength{},
            m_site{},
            m_numPlies{},
            m_flags{},
            m_movetext{}
        {
            m_additionalTags.reserve(8);
            m_movetext.reserve(512);
        }

        void BcgnGameEntryBuffer::clear()
        {
            m_date = {};
            m_whiteElo = 0;
            m_blackElo = 0;
            m_round = 0;
            m_eco = {};
            m_customStartPos = {};
            m_result = {};
            m_additionalTags.clear();
            m_whiteLength = 0;
            m_blackLength = 0;
            m_eventLength = 0;
            m_siteLength = 0;
            m_numPlies = 0;
            m_flags.clear();
            m_movetext.clear();
        }

        void BcgnGameEntryBuffer::setDate(const Date& date)
        {
            m_date = date;
        }

        void BcgnGameEntryBuffer::setWhiteElo(std::uint16_t elo)
        {
            m_whiteElo = elo;
        }

        void BcgnGameEntryBuffer::setBlackElo(std::uint16_t elo)
        {
            m_blackElo = elo;
        }

        void BcgnGameEntryBuffer::setRound(std::uint16_t round)
        {
            m_round = round;
        }

        void BcgnGameEntryBuffer::setEco(Eco eco)
        {
            m_eco = eco;
        }

        void BcgnGameEntryBuffer::setCustomStartPos(const Position& pos)
        {
            m_customStartPos = pos.compress();
        }

        void BcgnGameEntryBuffer::resetCustomStartPos()
        {
            m_customStartPos = {};
        }

        void BcgnGameEntryBuffer::setResult(GameResult result)
        {
            m_result = result;
        }

        void BcgnGameEntryBuffer::resetResult()
        {
            m_result = {};
        }

        void BcgnGameEntryBuffer::setAdditionalTag(
            std::string&& name, 
            std::string&& value
            )
        {
            if (m_additionalTags.size() >= 255) return;
            m_additionalTags.emplace_back(std::move(name), std::move(value));
        }

        void BcgnGameEntryBuffer::setAdditionalTag(
            const std::string& name, 
            const std::string& value
            )
        {
            if (m_additionalTags.size() >= 255) return;
            m_additionalTags.emplace_back(name, value);
        }

        void BcgnGameEntryBuffer::setWhitePlayer(const std::string_view sv)
        {
            m_whiteLength = (std::uint8_t)std::min(traits::maxStringLength, sv.size());
            std::memcpy(m_white, sv.data(), m_whiteLength);
        }

        void BcgnGameEntryBuffer::setBlackPlayer(const std::string_view sv)
        {
            m_blackLength = (std::uint8_t)std::min(traits::maxStringLength, sv.size());
            std::memcpy(m_black, sv.data(), m_blackLength);
        }

        void BcgnGameEntryBuffer::setEvent(const std::string_view sv)
        {
            m_eventLength = (std::uint8_t)std::min(traits::maxStringLength, sv.size());
            std::memcpy(m_event, sv.data(), m_eventLength);
        }

        void BcgnGameEntryBuffer::setSite(const std::string_view sv)
        {
            m_siteLength = (std::uint8_t)std::min(traits::maxStringLength, sv.size());
            std::memcpy(m_site, sv.data(), m_siteLength);
        }

        void BcgnGameEntryBuffer::addShortMove(unsigned char move)
        {
            ++m_numPlies;
            m_movetext.push_back(move);
        }

        void BcgnGameEntryBuffer::addLongMove(std::uint16_t move)
        {
            ++m_numPlies;
            m_movetext.push_back(move >> 8);
            m_movetext.push_back(move & 0xFF);
        }

        void BcgnGameEntryBuffer::addCompressedMove(const CompressedMove& move)
        {
            unsigned char c[2];
            move.writeToBigEndian(c);
            m_movetext.push_back(c[0]);
            m_movetext.push_back(c[1]);
            ++m_numPlies;
        }

        // returns number of bytes written
        [[nodiscard]] std::size_t BcgnGameEntryBuffer::writeTo(unsigned char* buffer)
        {
            const std::size_t headerLength = computeHeaderLength();
            const std::size_t movetextLength = m_movetext.size();
            const std::size_t totalLength = headerLength + movetextLength;
            if (totalLength >= std::numeric_limits<std::uint16_t>::max())
            {
                throw std::runtime_error("Game text must not be longer than 65535 bytes.");
            }

            m_flags.setHasAdditionalTags(!m_additionalTags.empty());
            m_flags.setHasCustomStartPos(m_customStartPos.has_value());

            writeBigEndian(buffer, (std::uint16_t)totalLength);
            writeBigEndian(buffer, (std::uint16_t)headerLength);

            *buffer++ = m_numPlies >> 6; // 8 highest (of 14) bits
            *buffer++ = (m_numPlies << 2) | mapResultToInt();

            writeBigEndian(buffer, m_date.year());
            *buffer++ = m_date.month();
            *buffer++ = m_date.day();

            writeBigEndian(buffer, m_whiteElo);
            writeBigEndian(buffer, m_blackElo);
            writeBigEndian(buffer, m_round);
            *buffer++ = m_eco.category();
            *buffer++ = m_eco.index();

            *buffer++ = m_flags.encode();

            if (m_customStartPos.has_value())
            {
                m_customStartPos->writeToBigEndian(buffer);
                buffer += sizeof(CompressedPosition);
            }

            writeString(buffer, m_white, m_whiteLength);
            writeString(buffer, m_black, m_blackLength);
            writeString(buffer, m_event, m_eventLength);
            writeString(buffer, m_site, m_siteLength);

            if (!m_additionalTags.empty())
            {
                *buffer++ = (std::uint8_t)m_additionalTags.size();
                for (auto&& [name, value] : m_additionalTags)
                {
                    writeString(buffer, name);
                    writeString(buffer, value);
                }
            }

            writeMovetext(buffer);

            return totalLength;
        }

        void BcgnGameEntryBuffer::writeMovetext(unsigned char*& buffer)
        {
            const std::size_t length = m_movetext.size();;
            std::memcpy(buffer, m_movetext.data(), length);
            buffer += length;
        }

        void BcgnGameEntryBuffer::writeString(
            unsigned char*& buffer, 
            const std::string& str
            ) const
        {
            const std::size_t length = std::min(traits::maxStringLength, str.size());
            *buffer++ = (std::uint8_t)length;
            std::memcpy(buffer, str.c_str(), length);
            buffer += length;
        }

        void BcgnGameEntryBuffer::writeString(
            unsigned char*& buffer, 
            const char* str, 
            std::uint8_t length
            ) const
        {
            *buffer++ = length;
            std::memcpy(buffer, str, length);
            buffer += length;
        }

        [[nodiscard]] unsigned BcgnGameEntryBuffer::mapResultToInt() const
        {
            if (!m_result.has_value())
            {
                return 0;
            }
            else
            {
                switch (m_result.value())
                {
                case GameResult::WhiteWin:
                    return 1;
                case GameResult::BlackWin:
                    return 2;
                case GameResult::Draw:
                    return 3;
                }
            }

            ASSERT(false);
            return 0;
        }

        FORCEINLINE void BcgnGameEntryBuffer::writeBigEndian(
            unsigned char*& buffer, 
            std::uint16_t value
            )
        {
            *buffer++ = value >> 8;
            *buffer++ = value & 0xFF;
        }

        [[nodiscard]] std::size_t BcgnGameEntryBuffer::computeHeaderLength() const
        {
            constexpr std::size_t lengthOfMandatoryFixedLengthFields =
                2 + 2 + // lengths
                2 + // ply + result
                4 + // date
                2 + 2 + 2 + 2 + // white elo, black elo, round, eco
                1 + // flags
                4; // lengths of 4 mandatory strings

            std::size_t length = lengthOfMandatoryFixedLengthFields;

            if (m_customStartPos.has_value())
            {
                static_assert(sizeof(CompressedPosition) == 24);
                length += sizeof(CompressedPosition);
            }

            length += m_whiteLength;
            length += m_blackLength;
            length += m_eventLength;
            length += m_siteLength;

            if (!m_additionalTags.empty())
            {
                length += 1;
                for (auto&& [name, value] : m_additionalTags)
                {
                    length += 2; // for two length specifications
                    length += std::min(traits::maxStringLength, name.size());
                    length += std::min(traits::maxStringLength, value.size());
                }
            }

            return length;
        }
    }

    BcgnFileWriter::BcgnFileWriter(
        const std::filesystem::path& path,
        BcgnFileHeader header,
        FileOpenMode mode,
        std::size_t bufferSize
        ) :
        m_header(header),
        m_game(std::make_unique<detail::BcgnGameEntryBuffer>()),
        m_file(nullptr, &std::fclose),
        m_path(path),
        m_buffer(std::max(bufferSize, traits::minBufferSize)),
        m_numBytesUsedInFrontBuffer(0),
        m_numBytesBeingWritten(0),
        m_future{}
    {
        const bool needsHeader = 
            (mode != FileOpenMode::Append) 
            || !std::filesystem::exists(path);

        auto strPath = path.string();
        m_file.reset(std::fopen(
            strPath.c_str(),
            mode == FileOpenMode::Append ? "ab" : "wb"
            ));

        if (needsHeader)
        {
            writeHeader();
        }
    }

    void BcgnFileWriter::beginGame()
    {
        m_game->clear();
    }

    void BcgnFileWriter::resetGame()
    {
        m_game->clear();
    }

    void BcgnFileWriter::setDate(const Date& date)
    {
        m_game->setDate(date);
    }

    void BcgnFileWriter::setWhiteElo(std::uint16_t elo)
    {
        m_game->setWhiteElo(elo);
    }

    void BcgnFileWriter::setBlackElo(std::uint16_t elo)
    {
        m_game->setBlackElo(elo);
    }

    void BcgnFileWriter::setRound(std::uint16_t round)
    {
        m_game->setRound(round);
    }

    void BcgnFileWriter::setEco(Eco eco)
    {
        m_game->setEco(eco);
    }

    void BcgnFileWriter::setCustomStartPos(const Position& pos)
    {
        m_game->setCustomStartPos(pos);
    }

    void BcgnFileWriter::resetCustomStartPos()
    {
        m_game->resetCustomStartPos();
    }

    void BcgnFileWriter::setResult(GameResult result)
    {
        m_game->setResult(result);
    }

    void BcgnFileWriter::resetResult()
    {
        m_game->resetResult();
    }

    void BcgnFileWriter::setAdditionalTag(
        std::string&& name, 
        std::string&& value
        )
    {
        m_game->setAdditionalTag(std::move(name), std::move(value));
    }

    void BcgnFileWriter::setAdditionalTag(
        const std::string& name, 
        const std::string& value
        )
    {
        m_game->setAdditionalTag(name, value);
    }

    void BcgnFileWriter::setWhitePlayer(const std::string_view sv)
    {
        m_game->setWhitePlayer(sv);
    }

    void BcgnFileWriter::setBlackPlayer(const std::string_view sv)
    {
        m_game->setBlackPlayer(sv);
    }

    void BcgnFileWriter::setEvent(const std::string_view sv)
    {
        m_game->setEvent(sv);
    }

    void BcgnFileWriter::setSite(const std::string_view sv)
    {
        m_game->setSite(sv);
    }

    void BcgnFileWriter::addMove(const Position& pos, const Move& move)
    {
        switch (m_header.compressionLevel)
        {
        case BcgnCompressionLevel::Level_0:
            m_game->addCompressedMove(move.compress());
            break;

        case BcgnCompressionLevel::Level_1:
            if (move_index::requiresLongMoveIndex(pos))
            {
                m_game->addLongMove(move_index::moveToLongIndex(pos, move));
            }
            else
            {
                m_game->addShortMove(move_index::moveToShortIndex(pos, move));
            }

            break;
        }
    }

    void BcgnFileWriter::endGame()
    {
        writeCurrentGame();

        // We don't know how much the next game will take
        // and we don't want to compute the size before writing.
        // So we ensure that we always have enough space in the buffer.
        // The buffer is not big anyway so this shouldn't be an issue.
        if (!enoughSpaceForNextGame())
        {
            swapAndPersistFrontBuffer();
        }
    }

    void BcgnFileWriter::flush()
    {
        swapAndPersistFrontBuffer();

        if (m_future.valid())
        {
            m_future.get();
        }
    }

    BcgnFileWriter::~BcgnFileWriter()
    {
        flush();
    }

    void BcgnFileWriter::writeHeader()
    {
        unsigned char* data = m_buffer.data();
        m_numBytesUsedInFrontBuffer += m_header.writeTo(data);
    }

    void BcgnFileWriter::writeCurrentGame()
    {
        const auto bytesWritten = 
            m_game->writeTo(m_buffer.data() + m_numBytesUsedInFrontBuffer);
        m_numBytesUsedInFrontBuffer += bytesWritten;
    }

    [[nodiscard]] bool BcgnFileWriter::enoughSpaceForNextGame() const
    {
        return m_buffer.size() - m_numBytesUsedInFrontBuffer >= traits::maxGameLength;
    }

    void BcgnFileWriter::swapAndPersistFrontBuffer()
    {
        if (!m_numBytesUsedInFrontBuffer)
        {
            return;
        }

        if (m_future.valid())
        {
            m_future.get();
        }

        m_buffer.swap();
        m_numBytesBeingWritten = m_numBytesUsedInFrontBuffer;
        m_numBytesUsedInFrontBuffer = 0;

        m_future = std::async(std::launch::async, [this]() {
            return std::fwrite(
                m_buffer.back_data(), 
                1, 
                m_numBytesBeingWritten, 
                m_file.get()
                );
            });
    }

    UnparsedBcgnGameMoves::UnparsedBcgnGameMoves(
        BcgnFileHeader header, 
        util::UnsignedCharBufferView movetext
        ) noexcept :
        m_header(header),
        m_encodedMovetext(movetext)
    {
    }

    [[nodiscard]] bool UnparsedBcgnGameMoves::hasNext() const
    {
        return !m_encodedMovetext.empty();
    }

    [[nodiscard]] Move UnparsedBcgnGameMoves::next(const Position& pos)
    {
        switch (m_header.compressionLevel)
        {
        case BcgnCompressionLevel::Level_0:
        {
            const CompressedMove compressedMove = 
                CompressedMove::readFromBigEndian(m_encodedMovetext.data());
            m_encodedMovetext.remove_prefix(2);
            return compressedMove.decompress();
        }

        case BcgnCompressionLevel::Level_1:
        {
            if (move_index::requiresLongMoveIndex(pos))
            {
                const std::uint16_t index = 
                    ((m_encodedMovetext[0]) << 8) 
                    | m_encodedMovetext[1];
                const Move move = move_index::longIndexToMove(pos, index);
                m_encodedMovetext.remove_prefix(2);
                return move;
            }
            else
            {
                const std::uint8_t index = m_encodedMovetext[0];
                const Move move = move_index::shortIndexToMove(pos, index);
                m_encodedMovetext.remove_prefix(1);
                return move;
            }
        }
        }

        ASSERT(false);
        return Move::null();
    }

    UnparsedBcgnGamePositions::iterator::iterator(
        BcgnFileHeader header, 
        util::UnsignedCharBufferView movetext
        ) noexcept :
        m_position(Position::startPosition()),
        m_moveProvider(header, movetext)
    {
    }

    UnparsedBcgnGamePositions::iterator::iterator(
        BcgnFileHeader header, 
        const Position& pos, 
        util::UnsignedCharBufferView movetext
        ) noexcept :
        m_position(pos),
        m_moveProvider(header, movetext)
    {
    }

    const UnparsedBcgnGamePositions::iterator& 
        UnparsedBcgnGamePositions::iterator::operator++()
    {
        const auto move = m_moveProvider.next(m_position);
        m_position.doMove(move);
        return *this;
    }

    bool operator==(
        const UnparsedBcgnGamePositions::iterator& lhs, 
        UnparsedBcgnGamePositions::iterator::sentinel rhs
        ) noexcept
    {
        return !lhs.m_moveProvider.hasNext();
    }

    bool operator!=(
        const UnparsedBcgnGamePositions::iterator& lhs, 
        UnparsedBcgnGamePositions::iterator::sentinel rhs
        ) noexcept
    {
        return lhs.m_moveProvider.hasNext();
    }

    [[nodiscard]] const Position& UnparsedBcgnGamePositions::iterator::operator*() const
    {
        return m_position;
    }

    [[nodiscard]] const Position* UnparsedBcgnGamePositions::iterator::operator->() const
    {
        return &m_position;
    }

    UnparsedBcgnGamePositions::UnparsedBcgnGamePositions(
        BcgnFileHeader header, 
        util::UnsignedCharBufferView movetext
        ) noexcept :
        m_header(header),
        m_startpos(Position::startPosition()),
        m_encodedMovetext(movetext)
    {

    }

    UnparsedBcgnGamePositions::UnparsedBcgnGamePositions(
        BcgnFileHeader header, 
        const Position& startpos, 
        util::UnsignedCharBufferView movetext
        ) noexcept :
        m_header(header),
        m_startpos(startpos),
        m_encodedMovetext(movetext)
    {

    }

    [[nodiscard]] UnparsedBcgnGamePositions::iterator 
        UnparsedBcgnGamePositions::begin()
    {
        return iterator(m_header, m_startpos, m_encodedMovetext);
    }

    [[nodiscard]] UnparsedBcgnGamePositions::iterator::sentinel 
        UnparsedBcgnGamePositions::end() const
    {
        return {};
    }

    UnparsedBcgnAdditionalTags::iterator::iterator(const char* data) :
        m_data(data),
        m_countLeft(0)
    {
        if (m_data)
        {
            m_countLeft = *m_data++;
        }
    }

    const UnparsedBcgnAdditionalTags::iterator& 
        UnparsedBcgnAdditionalTags::iterator::operator++()
    {
        const auto nameLength = m_data[0];
        const auto valueLength = m_data[1 + nameLength];

        m_kv = std::make_pair(
            std::string_view(m_data + 1, nameLength),
            std::string_view(m_data + 1 + nameLength + 1, valueLength)
            );

        m_countLeft -= 1;
        m_data += 1 + nameLength + 1 + valueLength;

        return *this;
    }

    bool operator==(
        const UnparsedBcgnAdditionalTags::iterator& lhs, 
        UnparsedBcgnAdditionalTags::iterator::sentinel rhs
        ) noexcept
    {
        return lhs.m_countLeft == 0;
    }

    bool operator!=(
        const UnparsedBcgnAdditionalTags::iterator& lhs, 
        UnparsedBcgnAdditionalTags::iterator::sentinel rhs
        ) noexcept
    {
        return lhs.m_countLeft != 0;
    }

    [[nodiscard]] UnparsedBcgnAdditionalTags::iterator::reference 
        UnparsedBcgnAdditionalTags::iterator::operator*() const
    {
        return m_kv;
    }

    [[nodiscard]] UnparsedBcgnAdditionalTags::iterator::pointer 
        UnparsedBcgnAdditionalTags::iterator::operator->() const
    {
        return &m_kv;
    }

    UnparsedBcgnAdditionalTags::UnparsedBcgnAdditionalTags(
        const unsigned char* data
        ) :
        m_data(reinterpret_cast<const char*>(data))
    {
    }

    [[nodiscard]] UnparsedBcgnAdditionalTags::iterator 
        UnparsedBcgnAdditionalTags::begin() const
    {
        return iterator(m_data);
    }

    [[nodiscard]] UnparsedBcgnAdditionalTags::iterator::sentinel 
        UnparsedBcgnAdditionalTags::end() const
    {
        return {};
    }

    void UnparsedBcgnGame::setFileHeader(BcgnFileHeader header)
    {
        m_header = header;
    }

    void UnparsedBcgnGame::setGameData(util::UnsignedCharBufferView sv)
    {
        m_data = sv;
        prereadData();
    }

    [[nodiscard]] std::uint16_t UnparsedBcgnGame::numPlies() const
    {
        return m_numPlies;
    }

    [[nodiscard]] std::optional<GameResult> UnparsedBcgnGame::result() const
    {
        return m_result;
    }

    [[nodiscard]] const Date& UnparsedBcgnGame::date() const
    {
        return m_date;
    }

    [[nodiscard]] std::uint16_t UnparsedBcgnGame::whiteElo() const
    {
        return m_whiteElo;
    }

    [[nodiscard]] std::uint16_t UnparsedBcgnGame::blackElo() const
    {
        return m_blackElo;
    }

    [[nodiscard]] std::uint16_t UnparsedBcgnGame::round() const
    {
        return m_round;
    }

    [[nodiscard]] Eco UnparsedBcgnGame::eco() const
    {
        return m_eco;
    }

    [[nodiscard]] std::string_view UnparsedBcgnGame::whitePlayer() const
    {
        return m_whitePlayer;
    }

    [[nodiscard]] std::string_view UnparsedBcgnGame::blackPlayer() const
    {
        return m_blackPlayer;
    }

    [[nodiscard]] std::string_view UnparsedBcgnGame::event() const
    {
        return m_event;
    }

    [[nodiscard]] std::string_view UnparsedBcgnGame::site() const
    {
        return m_site;
    }

    [[nodiscard]] bool UnparsedBcgnGame::hasCustomStartPosition() const
    {
        return m_flags.hasCustomStartPos();
    }

    [[nodiscard]] std::string_view UnparsedBcgnGame::getAdditionalTagValue(
        std::string_view namesv
        ) const
    {
        const auto name = util::UnsignedCharBufferView::fromStringView(namesv);

        if (!m_flags.hasAdditionalTags())
        {
            return {};
        }

        std::size_t offset = m_additionalTagsOffset;
        const std::uint8_t numAdditionalTags = m_data[offset];
        offset += 1;
        for (int i = 0; i < numAdditionalTags; ++i)
        {
            const std::uint8_t nameLength = m_data[offset];
            const auto currentName = m_data.substr(offset + 1, nameLength);
            const auto valueLength = m_data[offset + 1 + nameLength];

            if (currentName == name)
            {
                return m_data.substr(
                    offset + 1 + nameLength + 1, 
                    valueLength
                    ).toStringView();
            }

            offset += 2 + nameLength + valueLength;
        }

        return {};
    }

    [[nodiscard]] Position UnparsedBcgnGame::startPosition() const
    {
        if (m_flags.hasCustomStartPos())
        {
            return getCustomStartPos();
        }
        else
        {
            return Position::startPosition();
        }
    }

    [[nodiscard]] UnparsedBcgnGameMoves UnparsedBcgnGame::moves() const
    {
        return UnparsedBcgnGameMoves(m_header, encodedMovetext());
    }

    [[nodiscard]] UnparsedBcgnGamePositions UnparsedBcgnGame::positions() const
    {
        return UnparsedBcgnGamePositions(m_header, encodedMovetext());
    }

    [[nodiscard]] UnparsedBcgnAdditionalTags UnparsedBcgnGame::additionalTags() const
    {
        return UnparsedBcgnAdditionalTags(
            m_flags.hasAdditionalTags()
            ? m_data.data() + m_additionalTagsOffset
            : nullptr
            );
    }

    [[nodiscard]] std::size_t UnparsedBcgnGame::getStringsOffset() const
    {
        return 19 + 24 * m_flags.hasCustomStartPos();
    }

    [[nodiscard]] Position UnparsedBcgnGame::getCustomStartPos() const
    {
        const auto pos = CompressedPosition::readFromBigEndian(m_data.data() + 19);
        return pos.decompress();
    }

    void UnparsedBcgnGame::prereadData()
    {
        m_headerLength = readHeaderLength();
        // we convert to unsigned char to prevent sign extension.
        m_numPlies = (m_data[4] << 6) | (m_data[5] >> 2);
        m_result = mapIntToResult(m_data[5] & 3);
        m_date = Date((m_data[6] << 8) | m_data[7], m_data[8], m_data[9]);

        m_whiteElo = (m_data[10] << 8) | m_data[11];
        m_blackElo = (m_data[12] << 8) | m_data[13];
        m_round = (m_data[14] << 8) | m_data[15];

        m_eco = Eco(m_data[16], m_data[17]);

        m_flags = BcgnFlags::decode(m_data[18]);

        std::size_t offset = getStringsOffset();
        m_whitePlayer = m_data.substr(offset + 1, m_data[offset]).toStringView();
        offset += m_data[offset] + 1;
        m_blackPlayer = m_data.substr(offset + 1, m_data[offset]).toStringView();
        offset += m_data[offset] + 1;
        m_event = m_data.substr(offset + 1, m_data[offset]).toStringView();
        offset += m_data[offset] + 1;
        m_site = m_data.substr(offset + 1, m_data[offset]).toStringView();

        m_additionalTagsOffset = offset + m_data[offset] + 1;
    }

    [[nodiscard]] util::UnsignedCharBufferView 
        UnparsedBcgnGame::encodedMovetext() const
    {
        return m_data.substr(m_headerLength);
    }

    [[nodiscard]] std::uint16_t UnparsedBcgnGame::readHeaderLength() const
    {
        return (m_data[2] << 8) | m_data[3];
    }

    [[nodiscard]] std::optional<GameResult> 
        UnparsedBcgnGame::mapIntToResult(unsigned v) const
    {
        switch (v)
        {
        case 0:
            return {};
        case 1:
            return GameResult::WhiteWin;
        case 2:
            return GameResult::BlackWin;
        case 3:
            return GameResult::Draw;
        }

        ASSERT(false);
        return {};
    }

    BcgnFileReader::iterator::iterator(
        const std::filesystem::path& path, 
        std::size_t bufferSize
        ) :
        m_header{},
        m_file(nullptr, &std::fclose),
        m_path(path),
        m_buffer(bufferSize),
        m_bufferView{},
        m_future{},
        m_game{},
        m_isEnd(false)
    {
        auto strPath = path.string();
        m_file.reset(std::fopen(strPath.c_str(), "rb"));

        if (m_file == nullptr)
        {
            m_isEnd = true;
            return;
        }

        refillBuffer();

        if (!isEnd())
        {
            fileFileHeader();

            m_game.setFileHeader(m_header);

            prepareFirstGame();
        }
    }

    const BcgnFileReader::iterator& BcgnFileReader::iterator::operator++()
    {
        prepareNextGame();
        return *this;
    }

    bool operator==(
        const BcgnFileReader::iterator& lhs, 
        BcgnFileReader::iterator::sentinel rhs
        ) noexcept
    {
        return lhs.isEnd();
    }

    bool operator!=(
        const BcgnFileReader::iterator& lhs, 
        BcgnFileReader::iterator::sentinel rhs
        ) noexcept
    {
        return !lhs.isEnd();
    }

    [[nodiscard]] const UnparsedBcgnGame& BcgnFileReader::iterator::operator*() const
    {
        return m_game;
    }

    [[nodiscard]] const UnparsedBcgnGame* BcgnFileReader::iterator::operator->() const
    {
        return &m_game;
    }

    void BcgnFileReader::iterator::refillBuffer()
    {
        // We know that the biggest possible unprocessed 
        // amount of bytes is traits::maxGameLength - 1.
        // Using this information we can only fill the buffer starting from 
        // position traits::maxGameLength and prepend any unprocessed data
        // in front of it.
        // This way we minimize copying between buffers.

        const std::size_t usableReadBufferSpace = 
            m_buffer.size() - traits::maxGameLength;

        const std::size_t numUnprocessedBytes = m_bufferView.size();
        if (numUnprocessedBytes >= traits::maxGameLength)
        {
            // This should never happen. There should always be a game in there.
            throw std::runtime_error("Unprocessed block longer than maxGameLength.");
        }

        const std::size_t freeSpace = traits::maxGameLength - numUnprocessedBytes;
        if (numUnprocessedBytes)
        {
            // memcpy is safe because the buffers are disjoint.
            std::memcpy(
                m_buffer.back_data() + freeSpace, 
                m_bufferView.data(), 
                numUnprocessedBytes
                );
        }

        // If this is the first read then we read data to back_data,
        // swap the buffers, and schedule async read to new back_data.
        // If this is a subsequent read then we wait for write to back_data
        // to finish, swap the buffers, and scherule a read to the new back_data.

        const auto numBytesRead =
            m_future.valid()
            ? m_future.get()
            : std::fread(
                m_buffer.back_data() + traits::maxGameLength, 
                1, 
                usableReadBufferSpace, 
                m_file.get()
                );

        if (numBytesRead == 0)
        {
            m_isEnd = true;
            return;
        }

        m_buffer.swap();

        m_future = std::async(std::launch::async, [this, usableReadBufferSpace]() {
            return std::fread(
                m_buffer.back_data() + traits::maxGameLength, 
                1, 
                usableReadBufferSpace, 
                m_file.get()
                );
            });

        m_bufferView = util::UnsignedCharBufferView(
            m_buffer.data() + freeSpace, 
            numBytesRead + numUnprocessedBytes
            );
    }

    void BcgnFileReader::iterator::fileFileHeader()
    {
        if (m_bufferView.size() < traits::bcgnFileHeaderLength)
        {
            m_isEnd = true;
        }
        else
        {
            m_header.readFrom(m_bufferView.data());
            m_bufferView.remove_prefix(traits::bcgnFileHeaderLength);
        }
    }

    void BcgnFileReader::iterator::prepareFirstGame()
    {
        // If we fail here we can just set isEnd and don't bother.
        // The buffer should always be big enough to have at least one game.

        if (m_bufferView.size() < traits::minHeaderLength)
        {
            m_isEnd = true;
            return;
        }

        const auto size = readNextGameEntrySize();
        if (m_bufferView.size() < size)
        {
            m_isEnd = true;
            return;
        }

        m_game.setGameData(m_bufferView.substr(0, size));
        m_bufferView.remove_prefix(size);
    }

    void BcgnFileReader::iterator::prepareNextGame()
    {
        while (!isEnd())
        {
            if (m_bufferView.size() < 2)
            {
                // we cannot read the entry size, request more data
                refillBuffer();
                continue;
            }

            const auto size = readNextGameEntrySize();
            if (m_bufferView.size() < size)
            {
                refillBuffer();
                continue;
            }

            // Here we are guaranteed to have the whole game in the buffer.
            m_game.setGameData(m_bufferView.substr(0, size));
            m_bufferView.remove_prefix(size);
            return;
        }
    }

    [[nodiscard]] bool BcgnFileReader::iterator::isEnd() const
    {
        return m_isEnd;
    }

    [[nodiscard]] std::size_t BcgnFileReader::iterator::readNextGameEntrySize() const
    {
        // We assume here that there are 2 bytes in the buffer.
        return (m_bufferView[0] << 8) | m_bufferView[1];
    }

    BcgnFileReader::BcgnFileReader(const std::filesystem::path& path, std::size_t bufferSize) :
        m_file(nullptr, &std::fclose),
        m_path(path),
        m_bufferSize(bufferSize)
    {
        auto strPath = path.string();
        m_file.reset(std::fopen(strPath.c_str(), "rb"));
    }

    [[nodiscard]] bool BcgnFileReader::isOpen() const
    {
        return m_file != nullptr;
    }

    [[nodiscard]] BcgnFileReader::iterator BcgnFileReader::begin()
    {
        return iterator(m_path, m_bufferSize);
    }

    [[nodiscard]] BcgnFileReader::iterator::sentinel BcgnFileReader::end() const
    {
        return {};
    }
}
