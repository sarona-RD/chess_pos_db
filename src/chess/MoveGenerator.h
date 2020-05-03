#pragma once

#include "Bitboard.h"
#include "Chess.h"
#include "Position.h"

#include <vector>

// TODO: iterators

namespace movegen
{
    // For a pseudo-legal move the following are true:
    //  - the moving piece has the pos.sideToMove() color
    //  - the destination square is either empty or has a piece of the opposite color
    //  - if it is a pawn move it is valid (but may be illegal due to discovered checks)
    //  - if it is not a pawn move then the destination square is contained in attacks()
    //  - if it is a castling it is legal
    //  - a move other than castling may create a discovered attack on the king
    //  - a king may walk into a check

    template <typename FuncT>
    inline void forEachPseudoLegalPawnMove(const Position& pos, FuncT&& f)
    {
        const Color sideToMove = pos.sideToMove();
        const Square epSquare = pos.epSquare();
        const Bitboard ourPieces = pos.piecesBB(sideToMove);
        const Bitboard theirPieces = pos.piecesBB(!sideToMove);
        const Bitboard occupied = ourPieces | theirPieces;
        const Bitboard pawns = pos.piecesBB(Piece(PieceType::Pawn, sideToMove));

        auto generate = [&](Square fromSq)
        {
            Bitboard attackTargets = theirPieces;
            if (epSquare != Square::none())
            {
                attackTargets |= epSquare;
            }

            const Bitboard attacks = bb::pawnAttacks(Bitboard::square(fromSq), sideToMove) & attackTargets;

            const Rank startRank = sideToMove == Color::White ? rank2 : rank7;
            const Rank secondToLastRank = sideToMove == Color::White ? rank7 : rank2;
            const Offset forward = Offset{ 0, static_cast<std::int8_t>(sideToMove == Color::White ? 1 : -1) };

            // promotions
            if (fromSq.rank() == secondToLastRank)
            {
                // capture promotions
                for (Square toSq : attacks)
                {
                    for (PieceType pt : { PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen })
                    {
                        Move move{ fromSq, toSq, MoveType::Promotion, Piece(pt, sideToMove) };
                        f(move);
                    }
                }

                // push promotions
                const Square toSq = fromSq + forward;
                if (!occupied.isSet(toSq))
                {
                    for (PieceType pt : { PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen })
                    {
                        Move move{ fromSq, toSq, MoveType::Promotion, Piece(pt, sideToMove) };
                        f(move);
                    }
                }
            }
            else
            {
                // captures
                for (Square toSq : attacks)
                {
                    Move move{ fromSq, toSq, (toSq == epSquare) ? MoveType::EnPassant : MoveType::Normal };
                    f(move);
                }

                const Square toSq = fromSq + forward;

                // single push
                if (!occupied.isSet(toSq))
                {
                    if (fromSq.rank() == startRank)
                    {
                        // double push
                        const Square toSq2 = toSq + forward;
                        if (!occupied.isSet(toSq2))
                        {
                            Move move{ fromSq, toSq2 };
                            f(move);
                        }
                    }

                    Move move{ fromSq, toSq };
                    f(move);
                }
            }
        };

        for (Square fromSq : pawns)
        {
            generate(fromSq);
        }
    }

    template <PieceType PieceTypeV, typename FuncT>
    inline void forEachPseudoLegalPieceMove(const Position& pos, FuncT&& f)
    {
        static_assert(PieceTypeV != PieceType::None);

        if constexpr (PieceTypeV == PieceType::Pawn)
        {
            forEachPseudoLegalPawnMove(pos, f);
        }
        else
        {
            const Color sideToMove = pos.sideToMove();
            const Bitboard ourPieces = pos.piecesBB(sideToMove);
            const Bitboard theirPieces = pos.piecesBB(!sideToMove);
            const Bitboard occupied = ourPieces | theirPieces;
            const Bitboard pieces = pos.piecesBB(Piece(PieceTypeV, sideToMove));
            for (Square fromSq : pieces)
            {
                const Bitboard attacks = bb::attacks<PieceTypeV>(fromSq, occupied) & ~ourPieces;
                for (Square toSq : attacks)
                {
                    Move move{ fromSq, toSq };
                    f(move);
                }
            }
        }
    }

    template <typename FuncT>
    inline void forEachCastlingMove(const Position& pos, FuncT&& f)
    {
        // all square on a castling path must be empty
        constexpr EnumArray2<Color, CastleType, Bitboard> castlingPaths = { 
            { 
                {{ Bitboard::square(f1) | g1, Bitboard::square(b1) | c1 | d1 }},
                {{ Bitboard::square(f8) | g8, Bitboard::square(b8) | c8 | d8 }}
            } 
        };

        // this square must not be attacked by the enemy
        constexpr EnumArray2<Color, CastleType, Square> squarePassedByKing = {
            {
                {{ f1, d1 }},
                {{ f8, d8 }}
            }
        };

        // we can't use CastlingRights directly as it is a flag set
        constexpr EnumArray2<Color, CastleType, CastlingRights> castlingRightsMap = {
            {
                {{ CastlingRights::WhiteKingSide, CastlingRights::WhiteQueenSide }},
                {{ CastlingRights::BlackKingSide, CastlingRights::BlackQueenSide }}
            }
        };

        CastlingRights rights = pos.castlingRights();
        if (rights == CastlingRights::None)
        {
            return;
        }

        const Color sideToMove = pos.sideToMove();
        const Bitboard ourPieces = pos.piecesBB(sideToMove);
        const Bitboard theirPieces = pos.piecesBB(!sideToMove);
        const Bitboard occupied = ourPieces | theirPieces;

        // we first reduce the set of legal castlings by checking the paths for pieces
        if ((castlingPaths[Color::White][CastleType::Short] & occupied).any()) rights &= ~CastlingRights::WhiteKingSide;
        if ((castlingPaths[Color::White][CastleType::Long] & occupied).any()) rights &= ~CastlingRights::WhiteQueenSide;
        if ((castlingPaths[Color::Black][CastleType::Short] & occupied).any()) rights &= ~CastlingRights::BlackKingSide;
        if ((castlingPaths[Color::Black][CastleType::Long] & occupied).any()) rights &= ~CastlingRights::BlackQueenSide;

        if (rights == CastlingRights::None)
        {
            return;
        }

        // King must not be in check. Done here because it is quite expensive.
        const Square ksq = pos.kingSquare(sideToMove);
        if (pos.isSquareAttacked(ksq, !sideToMove))
        {
            return;
        }

        // Loop through all possible castlings.
        for (CastleType castlingType : values<CastleType>())
        {
            const CastlingRights right = castlingRightsMap[sideToMove][castlingType];

            if (!contains(rights, right))
            {
                continue;
            }

            // If we have this castling right
            // we check whether the king passes an attacked square.
            const Square passedSquare = squarePassedByKing[sideToMove][castlingType];
            if (pos.isSquareAttacked(passedSquare, !sideToMove))
            {
                continue;
            }

            // If it's a castling move then the change in square occupation
            // cannot have an effect because otherwise there would be
            // a slider attacker attacking the castling king.
            if (pos.isSquareAttacked(Move::kingCastleDestinations[sideToMove][castlingType], !sideToMove))
            {
                continue;
            }

            // If not we can castle.
            Move move = Move::castle(castlingType, sideToMove);
            f(move);
        }
    }

    template <typename FuncT>
    inline void forEachPseudoLegalMove(const Position& pos, FuncT&& func)
    {
        if (!pos.isLegal()) return;

        forEachPseudoLegalPieceMove<PieceType::Pawn>(pos, func);
        forEachPseudoLegalPieceMove<PieceType::Knight>(pos, func);
        forEachPseudoLegalPieceMove<PieceType::Bishop>(pos, func);
        forEachPseudoLegalPieceMove<PieceType::Rook>(pos, func);
        forEachPseudoLegalPieceMove<PieceType::Queen>(pos, func);
        forEachPseudoLegalPieceMove<PieceType::King>(pos, func);
        forEachCastlingMove(pos, func);
    }

    template <typename FuncT>
    inline void forEachLegalMove(const Position& pos, FuncT&& func)
    {
        if (!pos.isLegal()) return;

        auto funcIfLegal = [&](Move move) {
            if (pos.isPseudoLegalMoveLegal(move))
            {
                func(move);
            }
        };

        forEachPseudoLegalPieceMove<PieceType::Pawn>(pos, funcIfLegal);
        forEachPseudoLegalPieceMove<PieceType::Knight>(pos, funcIfLegal);
        forEachPseudoLegalPieceMove<PieceType::Bishop>(pos, funcIfLegal);
        forEachPseudoLegalPieceMove<PieceType::Rook>(pos, funcIfLegal);
        forEachPseudoLegalPieceMove<PieceType::Queen>(pos, funcIfLegal);
        forEachPseudoLegalPieceMove<PieceType::King>(pos, funcIfLegal);
        forEachCastlingMove(pos, func);
    }

    // pos must not have a 'king capture' available
    [[nodiscard]] std::vector<Move> generatePseudoLegalMoves(const Position& pos);
    [[nodiscard]] std::vector<Move> generateLegalMoves(const Position& pos);
}
