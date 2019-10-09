#pragma once

#include "Enum.h"
#include "GameClassification.h"
#include "Position.h"
#include "StorageHeader.h"

#include "lib/json/json.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace query
{
    // TODO: a lot of temporary strings are created because json lib doesn't
    //       support string_view. Find a remedy.

    // Specification of the position which is the basis for a query.
    // It can be either:
    // A FEN, in which case the position has no history
    // and for the query's purpose is interpreted as if the the game started
    // at this exact position.
    // A FEN with a move, in which case the position used as a root
    // is the positions that arises after the move is performed on the
    // position specified by the FEN. In this case root positions
    // is considered to have a history.
    struct RootPosition
    {
        std::string fen;

        // NOTE: If move is specified then the query is made on a 
        // position that arises from fen after the move is made.
        std::optional<std::string> move;

        friend void to_json(nlohmann::json& j, const RootPosition& query)
        {
            j["fen"] = query.fen;
            if (query.move.has_value())
            {
                j["move"] = *query.move;
            }
        }

        friend void from_json(const nlohmann::json& j, RootPosition& query)
        {
            j["fen"].get_to(query.fen);
            if (j["move"].is_null())
            {
                query.move.reset();
            }
            else
            {
                query.move = j["move"].get<std::string>();
            }
        }

        [[nodiscard]] std::optional<Position> tryGet() const
        {
            std::optional<Position> positionOpt = Position::tryFromFen(fen);

            if (positionOpt.has_value() && move.has_value())
            {
                const std::optional<Move> moveOpt = san::trySanToMove(*positionOpt, *move);
                if (moveOpt.has_value() && *moveOpt != Move::null())
                {
                    positionOpt->doMove(*moveOpt);
                }
            }

            return positionOpt;
        }
    };

    enum struct Category
    {
        Continuations,
        Transpositions,
        All
    };

    template <>
    struct EnumTraits<Category>
    {
        using IdType = int;
        using EnumType = Category;

        static constexpr int cardinality = 3;
        static constexpr bool isNaturalIndex = true;

        static constexpr std::array<EnumType, cardinality> values{
            Category::Continuations,
            Category::Transpositions,
            Category::All
        };

        [[nodiscard]] static constexpr IdType ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            return static_cast<EnumType>(id);
        }

        [[nodiscard]] static std::string_view toString(Category cat)
        {
            using namespace std::literals;

            switch (cat)
            {
            case Category::Continuations:
                return "continuations"sv;
            case Category::Transpositions:
                return "transpositions"sv;
            case Category::All:
                return "all"sv;
            }

            return ""sv;
        }

        [[nodiscard]] static std::optional<Category> fromString(std::string_view sv)
        {
            using namespace std::literals;

            if (sv == "continuations"sv) return Category::Continuations;
            if (sv == "transpositions"sv) return Category::Transpositions;
            if (sv == "all"sv) return Category::All;

            return {};
        }
    };

    struct FetchingOptions
    {
        bool fetchChildren;

        bool fetchFirstGame;
        bool fetchLastGame;

        bool fetchFirstGameForEachChild;
        bool fetchLastGameForEachChild;

        friend void to_json(nlohmann::json& j, const FetchingOptions& opt)
        {
            j = nlohmann::json{
                { "fetch_children", opt.fetchChildren},
                { "fetch_first_game", opt.fetchFirstGame },
                { "fetch_last_game", opt.fetchLastGame },
                { "fetch_first_game_for_each_child", opt.fetchFirstGameForEachChild },
                { "fetch_last_game_for_each_child", opt.fetchLastGameForEachChild }
            };
        }

        friend void from_json(const nlohmann::json& j, FetchingOptions& opt)
        {
            j["fetch_children"].get_to(opt.fetchChildren);
            j["fetch_first_game"].get_to(opt.fetchFirstGame);
            j["fetch_last_game"].get_to(opt.fetchLastGame);

            if (opt.fetchChildren)
            {
                j["fetch_first_game_for_each_child"].get_to(opt.fetchFirstGameForEachChild);
                j["fetch_last_game_for_each_child"].get_to(opt.fetchLastGameForEachChild);
            }
            else
            {
                opt.fetchFirstGameForEachChild = false;
                opt.fetchLastGameForEachChild = false;
            }
        }
    };

    struct Request
    {   
        // token can be used to match queries to results by the client
        std::string token;

        std::vector<RootPosition> positions;

        std::vector<GameLevel> levels;
        std::vector<GameResult> results;
        std::map<Category, FetchingOptions> fetchingOptions;

        friend void to_json(nlohmann::json& j, const Request& query)
        {
            j = nlohmann::json{
                { "token", query.token },
                { "positions", query.positions }
            };

            auto& levels = j["levels"] = nlohmann::json::array();
            auto& results = j["results"] = nlohmann::json::array();

            for (auto&& level : query.levels)
            {
                levels.emplace_back(toString(level));
            }

            for (auto&& result : query.results)
            {
                results.emplace_back(toString(GameResultWordFormat{}, result));
            }

            for (auto&& [cat, opt] : query.fetchingOptions)
            {
                j[std::string(toString(cat))] = opt;
            }
        }

        friend void from_json(const nlohmann::json& j, Request& query)
        {
            query.positions.clear();
            query.levels.clear();
            query.results.clear();
            query.fetchingOptions.clear();

            j["token"].get_to(query.token);
            j["positions"].get_to(query.positions);

            for (auto&& levelStr : j["levels"])
            {
                query.levels.emplace_back(fromString<GameLevel>(levelStr));
            }

            for (auto&& resultStr : j["results"])
            {
                query.results.emplace_back(fromString<GameResult>(GameResultWordFormat{}, resultStr));
            }

            for (const Category cat : values<Category>())
            {
                const auto catStr = std::string(toString(cat));
                if (j.contains(catStr))
                {
                    query.fetchingOptions.emplace(cat, j[catStr].get<FetchingOptions>());
                }
            }
        }
    };

    struct Entry
    {
        Entry(std::size_t count) :
            count(count)
        {
        }

        std::size_t count;
        std::optional<persistence::GameHeader> firstGame;
        std::optional<persistence::GameHeader> lastGame;

        friend void to_json(nlohmann::json& j, const Entry& entry)
        {
            j = nlohmann::json::object({
                { "count", entry.count }
            });

            if (entry.firstGame.has_value())
            {
                j["first_game"] = *entry.firstGame;
            }
            if (entry.lastGame.has_value())
            {
                j["last_game"] = *entry.lastGame;
            }
        }

        friend void from_json(const nlohmann::json& j, Entry& entry)
        {
            j["count"].get_to(entry.count);

            if (j.contains("first_game"))
            {
                entry.firstGame = j["first_game"].get<persistence::GameHeader>();
            }

            if (j.contains("last_game"))
            {
                entry.lastGame = j["last_game"].get<persistence::GameHeader>();
            }
        }
    };

    struct Entries
    {
    private:
        struct Origin
        {
            GameLevel level;
            GameResult result;
        };

    public:
        Entries() = default;

        template <typename... Args>
        decltype(auto) emplace(GameLevel level, GameResult result, Args&&... args)
        {
            m_entries.emplace_back(
                std::piecewise_construct,
                std::forward_as_tuple(Origin{level, result}),
                std::forward_as_tuple(std::forward<Args>(args)...)
            );
        }

        [[nodiscard]] decltype(auto) begin()
        {
            return m_entries.begin();
        }

        [[nodiscard]] decltype(auto) end()
        {
            return m_entries.end();
        }

        [[nodiscard]] decltype(auto) begin() const
        {
            return m_entries.begin();
        }

        [[nodiscard]] decltype(auto) end() const
        {
            return m_entries.end();
        }

        friend void to_json(nlohmann::json& j, const Entries& entries)
        {
            j = nlohmann::json::object();

            for (auto&& [origin, entry] : entries)
            {
                const auto levelStr = std::string(toString(origin.level));
                const auto resultStr = std::string(toString(GameResultWordFormat{}, origin.result));
                j[levelStr][resultStr] = entry;
            }
        }

    private:
        std::vector<std::pair<Origin, Entry>> m_entries;
    };

    struct Result
    {
        struct MoveCompareLess
        {
            [[nodiscard]] bool operator()(const Move& lhs, const Move& rhs) const noexcept
            {
                if (ordinal(lhs.from) < ordinal(rhs.from)) return true;
                if (ordinal(lhs.from) > ordinal(rhs.from)) return false;

                if (ordinal(lhs.to) < ordinal(rhs.to)) return true;
                if (ordinal(lhs.to) > ordinal(rhs.to)) return false;

                if (ordinal(lhs.type) < ordinal(rhs.type)) return true;
                if (ordinal(lhs.type) > ordinal(rhs.type)) return false;

                if (ordinal(lhs.promotedPiece) < ordinal(rhs.promotedPiece)) return true;

                return false;
            }
        };

        struct SubResult
        {
            Entries root;
            std::map<Move, Entries, MoveCompareLess> children;
        };

        Result(std::string fen) :
            position{ std::move(fen) }
        {
        }

        Result(std::string fen, std::string move) :
            position{ std::move(fen), std::move(move) }
        {
        }

        Result(std::string fen, std::optional<std::string> move) :
            position{ std::move(fen), std::move(move) }
        {
        }

        RootPosition position;
        std::map<Category, SubResult> resultsByCategory;

        friend void to_json(nlohmann::json& j, const Result& result)
        {
            const std::optional<Position> positionOpt = result.position.tryGet();
            if (!positionOpt.has_value())
            {
                return;
            }

            const auto& position = *positionOpt;

            // We have a valid object, fill it.
            j = nlohmann::json::object({
                { "position", result.position },
            });

            for (auto&& [cat, subresult] : result.resultsByCategory)
            {
                auto& jsonSubresult = j[std::string(toString(cat))];

                jsonSubresult["--"] = subresult.root;

                if (!subresult.children.empty())
                {
                    for (auto&& [move, entries] : subresult.children)
                    {
                        // Move as a key
                        const auto sanStr = san::moveToSan<san::SanSpec::Capture | san::SanSpec::Check | san::SanSpec::Compact>(position, move);
                        jsonSubresult[sanStr] = entries;
                    }
                }
            }
        }
    };

    struct Response
    {
        Request query;
        std::vector<Result> results;

        friend void to_json(nlohmann::json& j, const Response& response)
        {
            j = nlohmann::json{
                { "query", response.query },
                { "results", response.results }
            };
        }
    };
}
