#include "Position.h"

#include "detail/ParserBits.h"

#include "Bitboard.h"
#include "Chess.h"

#include "enum/EnumArray.h"

#include "util/Assert.h"

#include <iterator>
#include <optional>
#include <string>

#include <random>

#include "xxhash/xxhash_cpp.h"

[[nodiscard]] bool Board::isSquareAttacked(Square sq, Color attackerColor) const
{
    ASSERT(sq.isOk());

    const Bitboard occupied = piecesBB();
    const Bitboard bishops = piecesBB(Piece(PieceType::Bishop, attackerColor));
    const Bitboard rooks = piecesBB(Piece(PieceType::Rook, attackerColor));
    const Bitboard queens = piecesBB(Piece(PieceType::Queen, attackerColor));

    const Bitboard allSliders = (bishops | rooks | queens);
    if ((bb::pseudoAttacks<PieceType::Queen>(sq) & allSliders).any())
    {
        if (bb::isAttackedBySlider(
            sq,
            bishops,
            rooks,
            queens,
            occupied
        ))
        {
            return true;
        }
    }

    const Bitboard king = piecesBB(Piece(PieceType::King, attackerColor));
    if ((bb::pseudoAttacks<PieceType::King>(sq) & king).any())
    {
        return true;
    }

    const Bitboard knights = piecesBB(Piece(PieceType::Knight, attackerColor));
    if ((bb::pseudoAttacks<PieceType::Knight>(sq) & knights).any())
    {
        return true;
    }

    const Bitboard pawns = piecesBB(Piece(PieceType::Pawn, attackerColor));
    const Bitboard pawnAttacks = bb::pawnAttacks(pawns, attackerColor);

    return pawnAttacks.isSet(sq);
}

[[nodiscard]] bool Board::isSquareAttackedAfterMove(Move move, Square sq, Color attackerColor) const
{
    const Bitboard occupiedChange = Bitboard::square(move.from) | move.to;

    Bitboard occupied = piecesBB() ^ occupiedChange;

    Bitboard bishops = piecesBB(Piece(PieceType::Bishop, attackerColor));
    Bitboard rooks = piecesBB(Piece(PieceType::Rook, attackerColor));
    Bitboard queens = piecesBB(Piece(PieceType::Queen, attackerColor));
    Bitboard king = piecesBB(Piece(PieceType::King, attackerColor));
    Bitboard knights = piecesBB(Piece(PieceType::Knight, attackerColor));
    Bitboard pawns = piecesBB(Piece(PieceType::Pawn, attackerColor));

    if (move.type == MoveType::EnPassant)
    {
        const Square capturedPawnSq(move.to.file(), move.from.rank());
        occupied ^= capturedPawnSq;
    }
    else if (pieceAt(move.to) != Piece::none())
    {
        const Bitboard notCaptured = ~Bitboard::square(move.to);
        bishops &= notCaptured;
        rooks &= notCaptured;
        queens &= notCaptured;
    }

    // Potential attackers may have moved.
    const Piece movedPiece = pieceAt(move.from);
    if (movedPiece.color() == attackerColor)
    {
        switch (movedPiece.type())
        {
        case PieceType::Pawn:
            pawns ^= occupiedChange;
            break;
        case PieceType::Knight:
            knights ^= occupiedChange;
            break;
        case PieceType::Bishop:
            bishops ^= occupiedChange;
            break;
        case PieceType::Rook:
            rooks ^= occupiedChange;
            break;
        case PieceType::Queen:
            queens ^= occupiedChange;
            break;
        case PieceType::King:
        {
            if (move.type == MoveType::Castle)
            {
                const CastleType castleType = 
                    (move.to.file() == fileH)
                    ? CastleType::Short 
                    : CastleType::Long;

                king ^= move.from;
                king ^= m_kingCastleDestinations[attackerColor][castleType];
                rooks ^= move.to;
                rooks ^= m_rookCastleDestinations[attackerColor][castleType];

                break;
            }
            else
            {
                king ^= occupiedChange;
            }
        }
        }
    }

    // If it's a castling move then the change in square occupation
    // cannot have an effect because otherwise there would be
    // a slider attacker attacking the castling king.
    // (It could have an effect in chess960 if the slider
    // attacker was behind the rook involved in castling,
    // but we don't care about chess960.)

    const Bitboard allSliders = (bishops | rooks | queens);
    if ((bb::pseudoAttacks<PieceType::Queen>(sq) & allSliders).any())
    {
        if (bb::isAttackedBySlider(
            sq,
            bishops,
            rooks,
            queens,
            occupied
        ))
        {
            return true;
        }
    }

    if ((bb::pseudoAttacks<PieceType::King>(sq) & king).any())
    {
        return true;
    }

    if ((bb::pseudoAttacks<PieceType::Knight>(sq) & knights).any())
    {
        return true;
    }

    const Bitboard pawnAttacks = bb::pawnAttacks(pawns, attackerColor);

    return pawnAttacks.isSet(sq);
}

[[nodiscard]] bool Board::isPieceAttacked(Square sq) const
{
    const Piece piece = pieceAt(sq);

    if (piece == Piece::none())
    {
        return false;
    }

    return isSquareAttacked(sq, !piece.color());
}

[[nodiscard]] bool Board::isPieceAttackedAfterMove(Move move, Square sq) const
{
    const Piece piece = pieceAt(sq);

    if (piece == Piece::none())
    {
        return false;
    }

    if (sq == move.from)
    {
        // We moved the piece we're interested in.
        // For every move the piece ends up on the move.to except
        // for the case of castling moves.
        // But we know pseudo legal castling moves
        // are already legal, so the king cannot be in check after.
        if (move.type == MoveType::Castle)
        {
            return false;
        }

        // So update the square we're interested in.
        sq = move.to;
    }
    
    return isSquareAttackedAfterMove(move, sq, !piece.color());
}

[[nodiscard]] bool Board::isOwnKingAttackedAfterMove(Move move) const
{
    if (move.type == MoveType::Castle)
    {
        // Pseudo legal castling moves are already legal.
        // This is ensured by the move generator.
        return false;
    }

    const Piece movedPiece = pieceAt(move.from);

    return isPieceAttackedAfterMove(move, kingSquare(movedPiece.color()));
}

[[nodiscard]] Bitboard Board::attacks(Square sq) const
{
    const Piece piece = pieceAt(sq);
    if (piece == Piece::none())
    {
        return Bitboard::none();
    }

    if (piece.type() == PieceType::Pawn)
    {
        return bb::pawnAttacks(Bitboard::square(sq), piece.color());
    }
    else
    {
        return bb::attacks(piece.type(), sq, piecesBB());
    }
}

[[nodiscard]] Bitboard Board::attackers(Square sq, Color attackerColor) const
{
    // En-passant square is not included.

    Bitboard allAttackers = Bitboard::none();

    const Bitboard occupied = piecesBB();

    const Bitboard bishops = piecesBB(Piece(PieceType::Bishop, attackerColor));
    const Bitboard rooks = piecesBB(Piece(PieceType::Rook, attackerColor));
    const Bitboard queens = piecesBB(Piece(PieceType::Queen, attackerColor));

    const Bitboard bishopLikePieces = (bishops | queens);
    const Bitboard bishopAttacks = bb::attacks<PieceType::Bishop>(sq, occupied);
    allAttackers |= bishopAttacks & bishopLikePieces;

    const Bitboard rookLikePieces = (rooks | queens);
    const Bitboard rookAttacks = bb::attacks<PieceType::Rook>(sq, occupied);
    allAttackers |= rookAttacks & rookLikePieces;

    const Bitboard king = piecesBB(Piece(PieceType::King, attackerColor));
    allAttackers |= bb::pseudoAttacks<PieceType::King>(sq) & king;

    const Bitboard knights = piecesBB(Piece(PieceType::Knight, attackerColor));
    allAttackers |= bb::pseudoAttacks<PieceType::Knight>(sq) & knights;

    const Bitboard pawns = piecesBB(Piece(PieceType::Pawn, attackerColor));
    allAttackers |= bb::pawnAttacks(Bitboard::square(sq), !attackerColor) & pawns;

    return allAttackers;
}

const Piece* Board::piecesRaw() const
{
    return m_pieces.data();
}

namespace detail::lookup
{
    static constexpr EnumArray<Piece, char> fenPiece = []() {
        EnumArray<Piece, char> fenPiece{};

        fenPiece[whitePawn] = 'P';
        fenPiece[blackPawn] = 'p';
        fenPiece[whiteKnight] = 'N';
        fenPiece[blackKnight] = 'n';
        fenPiece[whiteBishop] = 'B';
        fenPiece[blackBishop] = 'b';
        fenPiece[whiteRook] = 'R';
        fenPiece[blackRook] = 'r';
        fenPiece[whiteQueen] = 'Q';
        fenPiece[blackQueen] = 'q';
        fenPiece[whiteKing] = 'K';
        fenPiece[blackKing] = 'k';
        fenPiece[Piece::none()] = 'X';

        return fenPiece;
    }();
}

[[nodiscard]] std::string Board::fen() const
{
    std::string fen;
    fen.reserve(96); // longest fen is probably in range of around 88

    Rank rank = rank8;
    File file = fileA;
    std::uint8_t emptyCounter = 0;

    for (;;)
    {
        const Square sq(file, rank);
        const Piece piece = m_pieces[sq];

        if (piece == Piece::none())
        {
            ++emptyCounter;
        }
        else
        {
            if (emptyCounter != 0)
            {
                fen.push_back(static_cast<char>(emptyCounter) + '0');
                emptyCounter = 0;
            }

            fen.push_back(detail::lookup::fenPiece[piece]);
        }

        ++file;
        if (file > fileH)
        {
            file = fileA;
            --rank;
            if (rank < rank1)
            {
                break;
            }

            if (emptyCounter != 0)
            {
                fen.push_back(static_cast<char>(emptyCounter) + '0');
                emptyCounter = 0;
            }
            fen.push_back('/');
        }
    }

    return fen;
}

void Position::set(const char* fen)
{
    const char* s = BaseType::set(fen);

    s += 1;
    m_sideToMove = (*s == 'w') ? Color::White : Color::Black;

    s += 2;
    m_castlingRights = parser_bits::readCastlingRights(s);

    s += 1;
    m_epSquare = (*s == '-') ? Square::none() : parser_bits::parseSquare(s);

    nullifyEpSquareIfNotPossible();
}

// Returns false if the fen was not valid
// If the returned value was false the position
// is in unspecified state.
[[nodiscard]] bool Position::trySet(std::string_view fen)
{
    // Lazily splits by ' '. Returns empty string views if at the end.
    auto nextPart = [fen, start = std::size_t{ 0 }]() mutable {
        std::size_t end = fen.find(' ', start);
        if (end == std::string::npos)
        {
            std::string_view substr = fen.substr(start);
            start = fen.size();
            return substr;
        }
        else
        {
            std::string_view substr = fen.substr(start, end - start);
            start = end + 1; // to skip whitespace
            return substr;
        }
    };

    if (!BaseType::trySet(nextPart())) return false;

    {
        const auto side = nextPart();
        if (side == std::string_view("w")) m_sideToMove = Color::White;
        else if (side == std::string_view("b")) m_sideToMove = Color::Black;
        else return false;

        if (isSquareAttacked(kingSquare(!m_sideToMove), m_sideToMove)) return false;
    }

    {
        const auto castlingRights = nextPart();
        auto castlingRightsOpt = parser_bits::tryParseCastlingRights(castlingRights);
        if (!castlingRightsOpt.has_value())
        {
            return false;
        }
        else
        {
            m_castlingRights = *castlingRightsOpt;
        }
    }

    {
        const auto epSquare = nextPart();
        auto epSquareOpt = parser_bits::tryParseEpSquare(epSquare);
        if (!epSquareOpt.has_value())
        {
            return false;
        }
        else
        {
            m_epSquare = *epSquareOpt;
        }
    }

    nullifyEpSquareIfNotPossible();

    return true;
}

[[nodiscard]] Position Position::fromFen(const char* fen)
{
    Position pos{};
    pos.set(fen);
    return pos;
}

[[nodiscard]] std::optional<Position> Position::tryFromFen(std::string_view fen)
{
    Position pos{};
    if (pos.trySet(fen)) return pos;
    else return {};
}

[[nodiscard]] Position Position::startPosition()
{
    static const Position pos = fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    return pos;
}

[[nodiscard]] std::string Position::fen() const
{
    std::string fen = Board::fen();

    fen += ' ';
    fen += m_sideToMove == Color::White ? 'w' : 'b';

    fen += ' ';
    parser_bits::appendCastlingRightsToString(m_castlingRights, fen);

    fen += ' ';
    parser_bits::appendEpSquareToString(m_epSquare, fen);

    return fen;
}

namespace detail::lookup
{
    static constexpr EnumArray<Square, CastlingRights> preservedCastlingRights = []() {
        EnumArray<Square, CastlingRights> preservedCastlingRights{};
        for (CastlingRights& rights : preservedCastlingRights)
        {
            rights = ~CastlingRights::None;
        }

        preservedCastlingRights[e1] = ~CastlingRights::White;
        preservedCastlingRights[e8] = ~CastlingRights::Black;

        preservedCastlingRights[h1] = ~CastlingRights::WhiteKingSide;
        preservedCastlingRights[a1] = ~CastlingRights::WhiteQueenSide;
        preservedCastlingRights[h8] = ~CastlingRights::BlackKingSide;
        preservedCastlingRights[a8] = ~CastlingRights::BlackQueenSide;

        return preservedCastlingRights;
    }();
}

ReverseMove Position::doMove(const Move& move)
{
    ASSERT(move.from.isOk() && move.to.isOk());

    const PieceType movedPiece = pieceAt(move.from).type();
    const Square oldEpSquare = m_epSquare;
    const CastlingRights oldCastlingRights = m_castlingRights;
    m_castlingRights &= detail::lookup::preservedCastlingRights[move.from];
    m_castlingRights &= detail::lookup::preservedCastlingRights[move.to];

    m_epSquare = Square::none();
    // for double pushes move index differs by 16 or -16;
    if((movedPiece == PieceType::Pawn) & ((ordinal(move.to) ^ ordinal(move.from)) == 16))
    {
        const Square potentialEpSquare = fromOrdinal<Square>((ordinal(move.to) + ordinal(move.from)) >> 1);
        // Even though the move has not yet been made we can safely call
        // this function and get the right result because the position of the
        // pawn to be captured is not really relevant.
        if (isEpPossible(potentialEpSquare, !m_sideToMove))
        {
            m_epSquare = potentialEpSquare;
        }
    }

    const Piece captured = BaseType::doMove(move);
    m_sideToMove = !m_sideToMove;
    return { move, captured, oldEpSquare, oldCastlingRights };
}

[[nodiscard]] bool Position::isLegal() const
{
    return piecesBB(Piece(PieceType::King, Color::White)).count() == 1
        && piecesBB(Piece(PieceType::King, Color::Black)).count() == 1
        && !isSquareAttacked(kingSquare(!m_sideToMove), m_sideToMove);
}

[[nodiscard]] bool Position::isCheck(Move move) const
{
    return BaseType::isSquareAttackedAfterMove(move, kingSquare(!m_sideToMove), m_sideToMove);
}

[[nodiscard]] Position Position::afterMove(Move move) const
{
    Position cpy(*this);
    auto pc = cpy.doMove(move);

    (void)pc;
    //ASSERT(cpy.beforeMove(move, pc) == *this); // this assert would result in infinite recursion

    return cpy;
}

[[nodiscard]] std::array<std::uint32_t, 4> Position::hash() const
{
    constexpr std::uint32_t epSquareShift = 1;
    constexpr std::uint32_t castlingRightsShift = 1 + 7;

    std::array<std::uint32_t, 4> arrh;
    auto h = xxhash::XXH3_128bits(piecesRaw(), 64);
    std::memcpy(arrh.data(), &h, sizeof(std::uint32_t) * 4);
    const std::uint32_t mod =
        ordinal(m_sideToMove)
        | (ordinal(m_epSquare) << epSquareShift) // epSquare can be 64
        | (ordinal(m_castlingRights) << castlingRightsShift);
    arrh[0] ^= mod;
    return arrh;
}

[[nodiscard]] FORCEINLINE bool Position::isEpPossible(Square epSquare, Color sideToMove) const
{
    const Bitboard pawnsAttackingEpSquare =
        bb::pawnAttacks(Bitboard::square(epSquare), !sideToMove)
        & piecesBB(Piece(PieceType::Pawn, sideToMove));

    if (!pawnsAttackingEpSquare.any())
    {
        return false;
    }

    return isEpPossibleColdPath(epSquare, pawnsAttackingEpSquare, sideToMove);
}

[[nodiscard]] NOINLINE bool Position::isEpPossibleColdPath(Square epSquare, Bitboard pawnsAttackingEpSquare, Color sideToMove) const
{
    // only set m_epSquare when it matters, ie. when
    // the opposite side can actually capture
    for (Square sq : pawnsAttackingEpSquare)
    {
        // If we're here the previous move by other side
        // was a double pawn move so our king is either not in check
        // or is attacked only by the moved pawn - in which
        // case it can be captured by our pawn if it doesn't
        // create a discovered check on our king.
        // So overall we only have to check whether our king
        // ends up being uncovered to a slider attack.

        const Square ksq = kingSquare(sideToMove);

        const Bitboard bishops = piecesBB(Piece(PieceType::Bishop, !sideToMove));
        const Bitboard rooks = piecesBB(Piece(PieceType::Rook, !sideToMove));
        const Bitboard queens = piecesBB(Piece(PieceType::Queen, !sideToMove));

        const Bitboard relevantAttackers = bishops | rooks | queens;
        const Bitboard pseudoSliderAttacksFromKing = bb::pseudoAttacks<PieceType::Queen>(ksq);
        if ((relevantAttackers & pseudoSliderAttacksFromKing).isEmpty())
        {
            // It's enough that one pawn can capture.
            return true;
        }

        const Square capturedPawnSq(epSquare.file(), sq.rank());
        const Bitboard occupied = ((piecesBB() ^ sq) | epSquare) ^ capturedPawnSq;

        if (!bb::isAttackedBySlider(
            ksq,
            bishops,
            rooks,
            queens,
            occupied
        ))
        {
            // It's enough that one pawn can capture.
            return true;
        }
    }

    return false;
}

void Position::nullifyEpSquareIfNotPossible()
{
    if (m_epSquare != Square::none() && !isEpPossible(m_epSquare, m_sideToMove))
    {
        m_epSquare = Square::none();
    }
}

void PositionWithZobrist::set(const char* fen)
{
    Position::set(fen);
    initZobrist();
}

[[nodiscard]] bool PositionWithZobrist::trySet(std::string_view fen)
{
    bool b = Position::trySet(fen);
    if (b)
    {
        initZobrist();
    }
    return b;
}

[[nodiscard]] PositionWithZobrist PositionWithZobrist::fromFen(const char* fen)
{
    PositionWithZobrist pos{};
    pos.set(fen);
    return pos;
}

[[nodiscard]] std::optional<PositionWithZobrist> PositionWithZobrist::tryFromFen(std::string_view fen)
{
    PositionWithZobrist pos{};
    if (pos.trySet(fen)) return pos;
    else return {};
}

[[nodiscard]] PositionWithZobrist PositionWithZobrist::startPosition()
{
    static const PositionWithZobrist pos = fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    return pos;
}

ReverseMove PositionWithZobrist::doMove(const Move& move)
{
    ASSERT(move.from.isOk() && move.to.isOk());

    const PieceType movedPiece = pieceAt(move.from).type();
    const Square oldEpSquare = m_epSquare;
    const CastlingRights oldCastlingRights = m_castlingRights;
    m_castlingRights &= detail::lookup::preservedCastlingRights[move.from];
    m_castlingRights &= detail::lookup::preservedCastlingRights[move.to];

    if (oldCastlingRights != m_castlingRights)
    {
        m_zobrist ^=
            Zobrist::castling[static_cast<unsigned>(oldCastlingRights)]
            ^ Zobrist::castling[static_cast<unsigned>(m_castlingRights)];
    }

    if (m_epSquare != Square::none())
    {
        m_zobrist ^= Zobrist::enpassant[m_epSquare.file()];
        m_epSquare = Square::none();
    }
    // for double pushes move index differs by 16 or -16;
    if ((movedPiece == PieceType::Pawn) & ((ordinal(move.to) ^ ordinal(move.from)) == 16))
    {
        const Square potentialEpSquare = fromOrdinal<Square>((ordinal(move.to) + ordinal(move.from)) >> 1);
        // Even though the move has not yet been made we can safely call
        // this function and get the right result because the position of the
        // pawn to be captured is not really relevant.
        if (isEpPossible(potentialEpSquare, !m_sideToMove))
        {
            m_epSquare = potentialEpSquare;
            m_zobrist ^= Zobrist::enpassant[potentialEpSquare.file()];
        }
    }

    const Piece captured = BaseType::doMove(move, m_zobrist);
    m_sideToMove = !m_sideToMove;
    m_zobrist ^= Zobrist::blackToMove;
    return { move, captured, oldEpSquare, oldCastlingRights };
}

[[nodiscard]] ZobristKey PositionWithZobrist::zobrist() const
{
    return m_zobrist;
}

[[nodiscard]] PositionWithZobrist PositionWithZobrist::afterMove(Move move) const
{
    PositionWithZobrist cpy(*this);
    auto pc = cpy.doMove(move);

    (void)pc;

    return cpy;
}
