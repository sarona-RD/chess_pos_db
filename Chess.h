#pragma once

#include "Enum.h"

#include <cstdint>

enum struct Color : std::uint8_t
{
    White,
    Black
};

template <>
struct EnumTraits<Color>
{
    using IdType = int;
    using EnumType = Color;

    static constexpr int cardinality = 2;
    static constexpr bool isNaturalIndex = true;

    static constexpr std::array<EnumType, cardinality> values{
        Color::White,
        Color::Black
    };

    [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
    {
        return static_cast<IdType>(c);
    }

    [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
    {
        return static_cast<EnumType>(id);
    }
};

constexpr Color opposite(Color c)
{
    return c == Color::White ? Color::Black : Color::White;
}

enum struct PieceType : std::uint8_t
{
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King,

    None
};

template <>
struct EnumTraits<PieceType>
{
    using IdType = int;
    using EnumType = PieceType;

    static constexpr int cardinality = 6; // None is special
    static constexpr bool isNaturalIndex = true;

    static constexpr std::array<EnumType, cardinality> values{
        PieceType::Pawn,
        PieceType::Knight,
        PieceType::Bishop,
        PieceType::Rook,
        PieceType::Queen,
        PieceType::King
    };

    [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
    {
        return static_cast<IdType>(c);
    }

    [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
    {
        return static_cast<EnumType>(id);
    }
};

struct Piece
{
    static constexpr Piece none()
    {
        return Piece(PieceType::None, Color::White);
    }

    constexpr Piece() :
        Piece(PieceType::None, Color::White)
    {

    }

    constexpr Piece(PieceType type, Color color) :
        m_id((ordinal(type) << 1) | ordinal(color))
    {
    }

    constexpr Piece& operator=(const Piece& other) = default;

    constexpr friend bool operator==(Piece lhs, Piece rhs)
    {
        return lhs.m_id == rhs.m_id;
    }

    constexpr friend bool operator!=(Piece lhs, Piece rhs)
    {
        return !(lhs == rhs);
    }

    constexpr PieceType type() const
    {
        return fromOrdinal<PieceType>(m_id >> 1);
    }

    constexpr Color color() const
    {
        return fromOrdinal<Color>(m_id & 1);
    }

    constexpr explicit operator int() const
    {
        return static_cast<int>(m_id);
    }

private:
    std::uint8_t m_id; // lowest bit is a color, 7 highest bits are a piece type
};

constexpr Piece whitePawn = Piece(PieceType::Pawn, Color::White);
constexpr Piece whiteKnight = Piece(PieceType::Knight, Color::White);
constexpr Piece whiteBishop = Piece(PieceType::Bishop, Color::White);
constexpr Piece whiteRook = Piece(PieceType::Rook, Color::White);
constexpr Piece whiteQueen = Piece(PieceType::Queen, Color::White);
constexpr Piece whiteKing = Piece(PieceType::King, Color::White);

constexpr Piece blackPawn = Piece(PieceType::Pawn, Color::Black);
constexpr Piece blackKnight = Piece(PieceType::Knight, Color::Black);
constexpr Piece blackBishop = Piece(PieceType::Bishop, Color::Black);
constexpr Piece blackRook = Piece(PieceType::Rook, Color::Black);
constexpr Piece blackQueen = Piece(PieceType::Queen, Color::Black);
constexpr Piece blackKing = Piece(PieceType::King, Color::Black);

template <>
struct EnumTraits<Piece>
{
    using IdType = int;
    using EnumType = Piece;

    static constexpr int cardinality = 13;
    static constexpr bool isNaturalIndex = true;

    static constexpr std::array<EnumType, cardinality> values{
        whitePawn,
        blackPawn,
        whiteKnight,
        blackKnight,
        whiteBishop,
        blackBishop,
        whiteRook,
        blackRook,
        whiteQueen,
        blackQueen,
        whiteKing,
        blackKing,
        Piece::none()
    };

    [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
    {
        return static_cast<IdType>(c);
    }
};

constexpr char toChar(Piece piece)
{
    constexpr char chars[] = {
        'P', 'p',
        'N', 'n',
        'B', 'b',
        'R', 'r',
        'Q', 'q',
        'K', 'k',
        '.', '.',
    };

    return chars[ordinal(piece)];
}

template <typename TagT>
struct Coord
{
    constexpr explicit Coord(int i) :
        m_i(i)
    {
    }

    constexpr explicit operator int() const
    {
        return static_cast<int>(m_i);
    }

    constexpr friend Coord& operator++(Coord& c)
    {
        ++c.m_i;
        return c;
    }

    constexpr friend Coord& operator--(Coord& c)
    {
        --c.m_i;
        return c;
    }

    constexpr friend Coord& operator+=(Coord& c, int d)
    {
        c.m_i += d;
        return c;
    }

    constexpr friend Coord& operator-=(Coord& c, int d)
    {
        c.m_i -= d;
        return c;
    }

    constexpr friend Coord operator+(const Coord& c, int d)
    {
        Coord cpy(c);
        cpy += d;
        return cpy;
    }

    constexpr friend Coord operator-(const Coord& c, int d)
    {
        Coord cpy(c);
        cpy -= d;
        return cpy;
    }

    constexpr friend int operator-(const Coord& c1, const Coord& c2)
    {
        return c1.m_i - c2.m_i;
    }

    constexpr friend bool operator==(const Coord& c1, const Coord& c2)
    {
        return c1.m_i == c2.m_i;
    }

    constexpr friend bool operator!=(const Coord& c1, const Coord& c2)
    {
        return c1.m_i != c2.m_i;
    }

    constexpr friend bool operator<(const Coord& c1, const Coord& c2)
    {
        return c1.m_i < c2.m_i;
    }

    constexpr friend bool operator<=(const Coord& c1, const Coord& c2)
    {
        return c1.m_i <= c2.m_i;
    }

    constexpr friend bool operator>(const Coord& c1, const Coord& c2)
    {
        return c1.m_i > c2.m_i;
    }

    constexpr friend bool operator>=(const Coord& c1, const Coord& c2)
    {
        return c1.m_i >= c2.m_i;
    }

private:
    std::int8_t m_i;
};

struct FileTag;
struct RankTag;
using File = Coord<FileTag>;
using Rank = Coord<RankTag>;

constexpr File fileA = File(0);
constexpr File fileB = File(1);
constexpr File fileC = File(2);
constexpr File fileD = File(3);
constexpr File fileE = File(4);
constexpr File fileF = File(5);
constexpr File fileG = File(6);
constexpr File fileH = File(7);

constexpr Rank rank1 = Rank(0);
constexpr Rank rank2 = Rank(1);
constexpr Rank rank3 = Rank(2);
constexpr Rank rank4 = Rank(3);
constexpr Rank rank5 = Rank(4);
constexpr Rank rank6 = Rank(5);
constexpr Rank rank7 = Rank(6);
constexpr Rank rank8 = Rank(7);

template <>
struct EnumTraits<File>
{
    using IdType = int;
    using EnumType = File;

    static constexpr int cardinality = 8;
    static constexpr bool isNaturalIndex = true;

    [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
    {
        return static_cast<IdType>(c);
    }

    [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
    {
        return static_cast<EnumType>(id);
    }
};

template <>
struct EnumTraits<Rank>
{
    using IdType = int;
    using EnumType = Rank;

    static constexpr int cardinality = 8;
    static constexpr bool isNaturalIndex = true;

    [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
    {
        return static_cast<IdType>(c);
    }

    [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
    {
        return static_cast<EnumType>(id);
    }
};

// files east
// ranks north
struct FlatSquareOffset
{
    int value;

    constexpr FlatSquareOffset(int files, int ranks) :
        value(files + ranks * cardinality<File>())
    {

    }
};

struct Offset
{
    int file;
    int rank;

    constexpr FlatSquareOffset flat() const
    {
        return { file, rank };
    }
};

struct SquareCoords
{
    File file;
    Rank rank;

    constexpr SquareCoords(File f, Rank r) :
        file(f),
        rank(r)
    {
    }

    constexpr friend SquareCoords& operator+=(SquareCoords& c, Offset offset)
    {
        c.file += offset.file;
        c.rank += offset.rank;
        return c;
    }

    constexpr friend SquareCoords operator+(const SquareCoords& c, Offset offset)
    {
        SquareCoords cpy(c);
        cpy.file += offset.file;
        cpy.rank += offset.rank;
        return cpy;
    }

    constexpr bool isOk() const
    {
        return file >= fileA && file <= fileH && rank >= rank1 && rank <= rank8;
    }
};

struct Square
{
    static constexpr Square none()
    {
        return Square(cardinality<Rank>() * cardinality<File>());
    }

    constexpr explicit Square(int idx) :
        m_id(idx)
    {
    }

    constexpr Square(File file, Rank rank) :
        m_id(ordinal(file) + ordinal(rank) * cardinality<File>())
    {

    }

    constexpr explicit Square(SquareCoords coords) :
        Square(coords.file, coords.rank)
    {
    }

    constexpr friend bool operator<=(Square lhs, Square rhs)
    {
        return lhs.m_id <= rhs.m_id;
    }

    constexpr friend bool operator>=(Square lhs, Square rhs)
    {
        return lhs.m_id >= rhs.m_id;
    }

    constexpr friend bool operator==(Square lhs, Square rhs)
    {
        return lhs.m_id == rhs.m_id;
    }

    constexpr friend bool operator!=(Square lhs, Square rhs)
    {
        return !(lhs == rhs);
    }

    constexpr friend Square& operator++(Square& sq)
    {
        ++sq.m_id;
        return sq;
    }

    constexpr friend Square& operator--(Square& sq)
    {
        --sq.m_id;
        return sq;
    }

    constexpr friend Square operator+(Square sq, FlatSquareOffset offset)
    {
        Square sqCpy = sq;
        sq += offset;
        return sqCpy;
    }

    constexpr friend Square& operator+=(Square& sq, FlatSquareOffset offset)
    {
        sq.m_id += offset.value;
        return sq;
    }

    constexpr friend Square operator+(Square sq, Offset offset)
    {
        return operator+(sq, offset.flat());
    }

    constexpr friend Square& operator+=(Square& sq, Offset offset)
    {
        return operator+=(sq, offset.flat());
    }

    constexpr explicit operator int() const
    {
        return m_id;
    }

    constexpr File file() const
    {
        return File(m_id % cardinality<Rank>());
    }

    constexpr Rank rank() const
    {
        return Rank(m_id / cardinality<Rank>());
    }

    constexpr SquareCoords coords() const
    {
        return { file(), rank() };
    }

    constexpr Color color() const
    {
        return fromOrdinal<Color>((~m_id) & 1);
    }

    constexpr void flipVertically()
    {
        m_id ^= 0b111;
    }

    constexpr void flipHorizontally()
    {
        m_id ^= 0b111000;
    }

    constexpr bool isOk() const
    {
        return m_id >= 0 && m_id < Square::none().m_id;
    }

private:
    std::int8_t m_id;
};

constexpr Square A1(fileA, rank1);
constexpr Square A2(fileA, rank2);
constexpr Square A3(fileA, rank3);
constexpr Square A4(fileA, rank4);
constexpr Square A5(fileA, rank5);
constexpr Square A6(fileA, rank6);
constexpr Square A7(fileA, rank7);
constexpr Square A8(fileA, rank8);

constexpr Square B1(fileB, rank1);
constexpr Square B2(fileB, rank2);
constexpr Square B3(fileB, rank3);
constexpr Square B4(fileB, rank4);
constexpr Square B5(fileB, rank5);
constexpr Square B6(fileB, rank6);
constexpr Square B7(fileB, rank7);
constexpr Square B8(fileB, rank8);

constexpr Square C1(fileC, rank1);
constexpr Square C2(fileC, rank2);
constexpr Square C3(fileC, rank3);
constexpr Square C4(fileC, rank4);
constexpr Square C5(fileC, rank5);
constexpr Square C6(fileC, rank6);
constexpr Square C7(fileC, rank7);
constexpr Square C8(fileC, rank8);

constexpr Square D1(fileD, rank1);
constexpr Square D2(fileD, rank2);
constexpr Square D3(fileD, rank3);
constexpr Square D4(fileD, rank4);
constexpr Square D5(fileD, rank5);
constexpr Square D6(fileD, rank6);
constexpr Square D7(fileD, rank7);
constexpr Square D8(fileD, rank8);

constexpr Square E1(fileE, rank1);
constexpr Square E2(fileE, rank2);
constexpr Square E3(fileE, rank3);
constexpr Square E4(fileE, rank4);
constexpr Square E5(fileE, rank5);
constexpr Square E6(fileE, rank6);
constexpr Square E7(fileE, rank7);
constexpr Square E8(fileE, rank8);

constexpr Square F1(fileF, rank1);
constexpr Square F2(fileF, rank2);
constexpr Square F3(fileF, rank3);
constexpr Square F4(fileF, rank4);
constexpr Square F5(fileF, rank5);
constexpr Square F6(fileF, rank6);
constexpr Square F7(fileF, rank7);
constexpr Square F8(fileF, rank8);

constexpr Square G1(fileG, rank1);
constexpr Square G2(fileG, rank2);
constexpr Square G3(fileG, rank3);
constexpr Square G4(fileG, rank4);
constexpr Square G5(fileG, rank5);
constexpr Square G6(fileG, rank6);
constexpr Square G7(fileG, rank7);
constexpr Square G8(fileG, rank8);

constexpr Square H1(fileH, rank1);
constexpr Square H2(fileH, rank2);
constexpr Square H3(fileH, rank3);
constexpr Square H4(fileH, rank4);
constexpr Square H5(fileH, rank5);
constexpr Square H6(fileH, rank6);
constexpr Square H7(fileH, rank7);
constexpr Square H8(fileH, rank8);

template <>
struct EnumTraits<Square>
{
    using IdType = int;
    using EnumType = Square;

    static constexpr int cardinality = cardinality<Rank>() * cardinality<File>();
    static constexpr bool isNaturalIndex = true;

    [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
    {
        return static_cast<IdType>(c);
    }

    [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
    {
        return static_cast<EnumType>(id);
    }
};

struct Move
{
    Square from;
    Square to;
    Piece promotedPiece = Piece::none();
};
