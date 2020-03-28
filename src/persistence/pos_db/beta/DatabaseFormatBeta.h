#pragma once

#include "persistence/pos_db/Database.h"
#include "persistence/pos_db/Query.h"
#include "persistence/pos_db/StorageHeader.h"

#include "chess/GameClassification.h"

#include "enum/EnumArray.h"

#include "external_storage/External.h"

#include "util/MemoryAmount.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <execution>
#include <filesystem>
#include <future>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct Position;
struct ReverseMove;
struct Move;

namespace persistence
{
    namespace db_beta
    {
        namespace detail
        {
            static constexpr bool usePacked = true;

            // Have ranges of mixed values be at most this long
            extern const std::size_t indexGranularity;

            static constexpr std::uint64_t invalidGameOffset = std::numeric_limits<std::uint64_t>::max();

            struct Key
            {
                // Hash:96, PackedReverseMove:27, GameLevel:2, GameResult:2, padding:1

                static constexpr std::size_t levelBits = 2;
                static constexpr std::size_t resultBits = 2;

                static constexpr std::uint32_t reverseMoveShift = 32 - PackedReverseMove::numBits;
                static constexpr std::uint32_t levelShift = reverseMoveShift - levelBits;
                static constexpr std::uint32_t resultShift = levelShift - resultBits;

                static constexpr std::uint32_t levelMask = 0b11;
                static constexpr std::uint32_t resultMask = 0b11;

                static_assert(PackedReverseMove::numBits + levelBits + resultBits <= 32);

                using StorageType = std::array<std::uint32_t, 4>;

                Key() = default;

                Key(const Position& pos, const ReverseMove& reverseMove = ReverseMove{});

                Key(const Position& pos, const ReverseMove& reverseMove, GameLevel level, GameResult result);

                Key(const Key&) = default;
                Key(Key&&) = default;
                Key& operator=(const Key&) = default;
                Key& operator=(Key&&) = default;

                [[nodiscard]] const StorageType& hash() const;

                [[nodiscard]] GameLevel level() const;

                [[nodiscard]] GameResult result() const;

                struct CompareLessWithReverseMove
                {
                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        if (lhs.m_hash[0] < rhs.m_hash[0]) return true;
                        else if (lhs.m_hash[0] > rhs.m_hash[0]) return false;

                        if (lhs.m_hash[1] < rhs.m_hash[1]) return true;
                        else if (lhs.m_hash[1] > rhs.m_hash[1]) return false;

                        if (lhs.m_hash[2] < rhs.m_hash[2]) return true;
                        else if (lhs.m_hash[2] > rhs.m_hash[2]) return false;

                        return ((lhs.m_hash[3] & (PackedReverseMove::mask << reverseMoveShift)) < (rhs.m_hash[3] & (PackedReverseMove::mask << reverseMoveShift)));
                    }
                };

                struct CompareLessWithoutReverseMove
                {
                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        if (lhs.m_hash[0] < rhs.m_hash[0]) return true;
                        else if (lhs.m_hash[0] > rhs.m_hash[0]) return false;

                        if (lhs.m_hash[1] < rhs.m_hash[1]) return true;
                        else if (lhs.m_hash[1] > rhs.m_hash[1]) return false;

                        return (lhs.m_hash[2] < rhs.m_hash[2]);
                    }
                };

                struct CompareLessFull
                {
                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
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

                struct CompareEqualWithReverseMove
                {
                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        return
                            lhs.m_hash[0] == rhs.m_hash[0]
                            && lhs.m_hash[1] == rhs.m_hash[1]
                            && lhs.m_hash[2] == rhs.m_hash[2]
                            && (lhs.m_hash[3] & (PackedReverseMove::mask << reverseMoveShift)) == (rhs.m_hash[3] & (PackedReverseMove::mask << reverseMoveShift));
                    }
                };

                struct CompareEqualWithoutReverseMove
                {
                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        return
                            lhs.m_hash[0] == rhs.m_hash[0]
                            && lhs.m_hash[1] == rhs.m_hash[1]
                            && lhs.m_hash[2] == rhs.m_hash[2];
                    }
                };

                struct CompareEqualFull
                {
                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        return
                            lhs.m_hash[0] == rhs.m_hash[0]
                            && lhs.m_hash[1] == rhs.m_hash[1]
                            && lhs.m_hash[2] == rhs.m_hash[2]
                            && lhs.m_hash[3] == rhs.m_hash[3];
                    }
                };

            private:
                // All bits of the hash are created equal, so we can specify some ordering.
                // Elements ordered from least significant to most significant are [3][2][1][0]
                StorageType m_hash;
            };
            static_assert(sizeof(Key) == 16);

            struct PackedCountAndGameOffset;

            struct SingleGame {};

            struct CountAndGameOffset
            {
                CountAndGameOffset();

                CountAndGameOffset(std::uint64_t count, std::uint64_t gameOffset);

                CountAndGameOffset(SingleGame, std::uint64_t gameOffset);

                CountAndGameOffset& operator+=(std::uint64_t rhs);

                CountAndGameOffset operator+(std::uint64_t rhs);

                void combine(const CountAndGameOffset& rhs);

                void combine(const PackedCountAndGameOffset& rhs);

                [[nodiscard]] std::uint64_t count() const;

                [[nodiscard]] std::uint64_t gameOffset() const;

            private:
                std::uint64_t m_count;
                std::uint64_t m_gameOffset;
            };

            static_assert(sizeof(CountAndGameOffset) == 16);

            struct PackedCountAndGameOffset
            {
                // game offset is invalid if we don't have enough bits to store it
                // ie. count takes all the bits
                static constexpr std::uint64_t numSizeBits = 6;

                // numCountBits should always be at least 1 to avoid shifting by 64
                static constexpr std::uint64_t numDataBits = 64 - numSizeBits;

                static constexpr std::uint64_t mask = std::numeric_limits<std::uint64_t>::max();
                static constexpr std::uint64_t sizeMask = 0b111111;

                PackedCountAndGameOffset();

                PackedCountAndGameOffset(const CountAndGameOffset& unpacked);

                PackedCountAndGameOffset(std::uint64_t count, std::uint64_t gameOffset);

                PackedCountAndGameOffset(SingleGame, std::uint64_t gameOffset);

                [[nodiscard]] CountAndGameOffset unpack() const;

                PackedCountAndGameOffset& operator+=(std::uint64_t rhs);

                void combine(const PackedCountAndGameOffset& rhs);

                void combine(const CountAndGameOffset& rhs);

                [[nodiscard]] std::uint64_t count() const;

                [[nodiscard]] std::uint64_t gameOffset() const;

            private:
                // from least significant:
                // 6 bits for number N of count bits, at most 58
                // N bits for count
                // 58-N bits for game offset

                std::uint64_t m_packed;

                void setNone();

                void pack(std::uint64_t count, std::uint64_t gameOffset);

                void pack(SingleGame, std::uint64_t gameOffset);

                void pack(const CountAndGameOffset& rhs);

                [[nodiscard]] std::uint64_t countLength() const;
            };

            static_assert(sizeof(PackedCountAndGameOffset) == 8);

            using CountAndGameOffsetType = std::conditional_t<usePacked, PackedCountAndGameOffset, CountAndGameOffset>;

            struct Entry
            {
                Entry() = default;

                Entry(const Position& pos, const ReverseMove& reverseMove, GameLevel level, GameResult result, std::uint64_t gameOffset);

                Entry(const Entry&) = default;
                Entry(Entry&&) = default;
                Entry& operator=(const Entry&) = default;
                Entry& operator=(Entry&&) = default;

                struct CompareLessWithoutReverseMove
                {
                    [[nodiscard]] bool operator()(const Entry& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareLessWithoutReverseMove{}(lhs.m_key, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Entry& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareLessWithoutReverseMove{}(lhs.m_key, rhs);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareLessWithoutReverseMove{}(lhs, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareLessWithoutReverseMove{}(lhs, rhs);
                    }
                };

                struct CompareEqualWithoutReverseMove
                {
                    [[nodiscard]] bool operator()(const Entry& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareEqualWithoutReverseMove{}(lhs.m_key, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Entry& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareEqualWithoutReverseMove{}(lhs.m_key, rhs);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareEqualWithoutReverseMove{}(lhs, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareEqualWithoutReverseMove{}(lhs, rhs);
                    }
                };

                struct CompareLessWithReverseMove
                {
                    [[nodiscard]] bool operator()(const Entry& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareLessWithReverseMove{}(lhs.m_key, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Entry& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareLessWithReverseMove{}(lhs.m_key, rhs);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareLessWithReverseMove{}(lhs, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareLessWithReverseMove{}(lhs, rhs);
                    }
                };

                struct CompareEqualWithReverseMove
                {
                    [[nodiscard]] bool operator()(const Entry& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareEqualWithReverseMove{}(lhs.m_key, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Entry& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareEqualWithReverseMove{}(lhs.m_key, rhs);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareEqualWithReverseMove{}(lhs, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareEqualWithReverseMove{}(lhs, rhs);
                    }
                };

                // This behaves like the old operator<
                struct CompareLessFull
                {
                    [[nodiscard]] bool operator()(const Entry& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareLessFull{}(lhs.m_key, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Entry& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareLessFull{}(lhs.m_key, rhs);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareLessFull{}(lhs, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareLessFull{}(lhs, rhs);
                    }
                };

                struct CompareEqualFull
                {
                    [[nodiscard]] bool operator()(const Entry& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareEqualFull{}(lhs.m_key, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Entry& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareEqualFull{}(lhs.m_key, rhs);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Entry& rhs) const noexcept
                    {
                        return Key::CompareEqualFull{}(lhs, rhs.m_key);
                    }

                    [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs) const noexcept
                    {
                        return Key::CompareEqualFull{}(lhs, rhs);
                    }
                };

                [[nodiscard]] const Key& key() const;

                [[nodiscard]] std::uint64_t count() const;

                [[nodiscard]] std::uint64_t gameOffset() const;

                [[nodiscard]] GameLevel level() const;

                [[nodiscard]] GameResult result() const;

                [[nodiscard]] const CountAndGameOffsetType& countAndGameOffset() const;

                void combine(const Entry& rhs);

            private:
                Key m_key;
                CountAndGameOffsetType m_countAndGameOffset;
            };

            static_assert(sizeof(Entry) == 16 + sizeof(CountAndGameOffsetType));
            static_assert(std::is_trivially_copyable_v<Entry>);

            using PositionStats = EnumArray<query::Select, EnumArray2<GameLevel, GameResult, CountAndGameOffset>>;

            using Index = ext::RangeIndex<Key, Entry::CompareLessWithoutReverseMove>;

            struct File
            {
                static std::filesystem::path pathForId(const std::filesystem::path& path, std::uint32_t id);

                File(std::filesystem::path path);

                File(ext::ImmutableSpan<Entry>&& entries);

                File(std::filesystem::path path, Index&& index);

                File(ext::ImmutableSpan<Entry>&& entries, Index&& index);

                [[nodiscard]] friend bool operator<(const File& lhs, const File& rhs) noexcept;

                [[nodiscard]] std::uint32_t id() const;

                [[nodiscard]] const std::filesystem::path& path() const;

                [[nodiscard]] Entry at(std::size_t idx) const;

                [[nodiscard]] const ext::ImmutableSpan<Entry>& entries() const;

                void accumulateStatsFromEntries(
                    const std::vector<Entry>& entries,
                    const query::Request& query,
                    const Key& key,
                    query::PositionQueryOrigin origin,
                    PositionStats& stats);

                void executeQuery(
                    const query::Request& query,
                    const std::vector<Key>& keys,
                    const query::PositionQueries& queries,
                    std::vector<PositionStats>& stats);

            private:
                ext::ImmutableSpan<Entry> m_entries;
                Index m_index;
                std::uint32_t m_id;
            };

            struct FutureFile
            {
                FutureFile(std::future<Index>&& future, std::filesystem::path path);

                [[nodiscard]] friend bool operator<(const FutureFile& lhs, const FutureFile& rhs) noexcept;

                [[nodiscard]] std::uint32_t id() const;

                [[nodiscard]] File get();

            private:
                std::future<Index> m_future;
                std::filesystem::path m_path;
                std::uint32_t m_id;
            };


            struct AsyncStorePipeline
            {
            private:
                struct Job
                {
                    Job(std::filesystem::path path, std::vector<Entry>&& buffer, std::promise<Index>&& promise);

                    std::filesystem::path path;
                    std::vector<Entry> buffer;
                    std::promise<Index> promise;
                };

            public:
                AsyncStorePipeline(std::vector<std::vector<Entry>>&& buffers, std::size_t numSortingThreads = 1);

                AsyncStorePipeline(const AsyncStorePipeline&) = delete;

                ~AsyncStorePipeline();

                [[nodiscard]] std::future<Index> scheduleUnordered(const std::filesystem::path& path, std::vector<Entry>&& elements);

                [[nodiscard]] std::future<Index> scheduleOrdered(const std::filesystem::path& path, std::vector<Entry>&& elements);

                [[nodiscard]] std::vector<Entry> getEmptyBuffer();

                void waitForCompletion();

            private:
                std::queue<Job> m_sortQueue;
                std::queue<Job> m_writeQueue;
                std::queue<std::vector<Entry>> m_bufferQueue;

                std::condition_variable m_sortQueueNotEmpty;
                std::condition_variable m_writeQueueNotEmpty;
                std::condition_variable m_bufferQueueNotEmpty;

                std::mutex m_mutex;

                std::atomic_bool m_sortingThreadFinished;
                std::atomic_bool m_writingThreadFinished;

                std::vector<std::thread> m_sortingThreads;
                std::thread m_writingThread;

                void runSortingThread();

                void runWritingThread();

                void sort(std::vector<Entry>& buffer);

                // works analogously to std::unique but also combines equal values
                void combine(std::vector<Entry>& buffer);

                void prepareData(std::vector<Entry>& buffer);
            };

            struct Partition
            {
                static const std::size_t mergeMemory;

                Partition() = default;

                Partition(std::filesystem::path path);

                void setPath(std::filesystem::path path);
            
                void executeQuery(
                    const query::Request& query,
                    const std::vector<Key>& keys,
                    const query::PositionQueries& queries,
                    std::vector<PositionStats>& stats);

                void mergeAll(std::function<void(const ext::ProgressReport&)> progressCallback);

                // outPath is a path of the file to output to
                void replicateMergeAll(const std::filesystem::path& outPath, std::function<void(const ext::ProgressReport&)> progressCallback);

                // data has to be sorted in ascending order
                void storeOrdered(const Entry* data, std::size_t count);

                // entries have to be sorted in ascending order
                void storeOrdered(const std::vector<Entry>& entries);

                // Uses the passed id.
                // It is required that the file with this id doesn't exist already.
                void storeUnordered(AsyncStorePipeline& pipeline, std::vector<Entry>&& entries, std::uint32_t id);

                void storeUnordered(AsyncStorePipeline& pipeline, std::vector<Entry>&& entries);

                void collectFutureFiles();

                [[nodiscard]] std::uint32_t nextId() const;

                [[nodiscard]] const std::filesystem::path path() const;

                void clear();

                [[nodiscard]] bool empty() const;

            private:
                std::filesystem::path m_path;
                std::vector<File> m_files;

                // We store it in a set because then we can change insertion
                // order through forcing ids. It's easier to keep it
                // ordered like that. And we need it ordered all the time
                // because of queries to nextId()
                std::set<FutureFile> m_futureFiles;

                std::mutex m_mutex;

                [[nodiscard]] std::filesystem::path pathForId(std::uint32_t id) const;

                [[nodiscard]] std::filesystem::path nextPath() const;

                [[nodiscard]] Index mergeAllIntoFile(const std::filesystem::path& outFilePath, std::function<void(const ext::ProgressReport&)> progressCallback) const;

                void discoverFiles();
            };
        }

        struct Database final : persistence::Database
        {
        private:
            using BaseType = persistence::Database;

            static inline const std::filesystem::path partitionDirectory = "data";

            static inline const DatabaseManifest m_manifest = { "db_beta", true };

            static constexpr std::size_t m_totalNumDirectories = 1;

            static inline const EnumArray<GameLevel, std::string> m_headerNames = {
                "_human",
                "_engine",
                "_server"
            };

            static const std::size_t m_pgnParserMemory;

        public:
            Database(std::filesystem::path path);

            Database(std::filesystem::path path, std::size_t headerBufferMemory);

            [[nodiscard]] static const std::string& key();

            [[nodiscard]] const DatabaseManifest& manifest() const override;

            void clear() override;

            const std::filesystem::path& path() const override;

            [[nodiscard]] query::Response executeQuery(query::Request query) override;

            void mergeAll(MergeProgressCallback progressCallback = {}) override;

            void replicateMergeAll(const std::filesystem::path& path, MergeProgressCallback progressCallback = {}) override;

            ImportStats import(
                std::execution::parallel_unsequenced_policy,
                const ImportablePgnFiles& pgns,
                std::size_t memory,
                std::size_t numThreads = std::thread::hardware_concurrency(),
                ImportProgressCallback progressCallback = {}
            ) override;

            ImportStats import(
                std::execution::sequenced_policy,
                const ImportablePgnFiles& pgns,
                std::size_t memory,
                ImportProgressCallback progressCallback = {}
            ) override;

            ImportStats import(
                const ImportablePgnFiles& pgns,
                std::size_t memory,
                ImportProgressCallback progressCallback = {}
            ) override;

            void flush() override;

        private:
            std::filesystem::path m_path;

            EnumArray<GameLevel, Header> m_headers;
            std::atomic<std::uint32_t> m_nextGameIdx;

            // We only have one partition for this format
            detail::Partition m_partition;

            [[nodiscard]] EnumArray<GameLevel, Header> makeHeaders(const std::filesystem::path& path);

            [[nodiscard]] EnumArray<GameLevel, Header> makeHeaders(const std::filesystem::path& path, std::size_t headerBufferMemory);

            [[nodiscard]] std::uint32_t numGamesInHeaders() const;

            void collectFutureFiles();

            [[nodiscard]] std::vector<PackedGameHeader> queryHeadersByOffsets(const std::vector<std::uint64_t>& offsets, GameLevel level);

            [[nodiscard]] std::vector<GameHeader> queryHeadersByOffsets(const std::vector<std::uint64_t>& offsets, const std::vector<query::GameHeaderDestination>& destinations);

            void disableUnsupportedQueryFeatures(query::Request& query) const;

            [[nodiscard]] query::PositionQueryResults commitStatsAsResults(
                const query::Request& query,
                const query::PositionQueries& posQueries,
                std::vector<detail::PositionStats>& stats);

            [[nodiscard]] std::vector<detail::Key> getKeys(const query::PositionQueries& queries);

            ImportStats importPgnsImpl(
                std::execution::sequenced_policy,
                detail::AsyncStorePipeline& pipeline,
                const ImportablePgnFiles& pgns,
                std::function<void(const std::filesystem::path& file)> completionCallback
            );

            struct Block
            {
                typename ImportablePgnFiles::const_iterator begin;
                typename ImportablePgnFiles::const_iterator end;
                std::uint32_t nextId;
            };

            [[nodiscard]] std::vector<Block> divideIntoBlocks(
                const ImportablePgnFiles& pgns,
                std::size_t bufferSize,
                std::size_t numBlocks
            );

            ImportStats importPgnsImpl(
                std::execution::parallel_unsequenced_policy,
                detail::AsyncStorePipeline& pipeline,
                const ImportablePgnFiles& paths,
                std::size_t bufferSize,
                std::size_t numThreads
            );

            void store(
                detail::AsyncStorePipeline& pipeline,
                std::vector<detail::Entry>& entries
            );

            void store(
                detail::AsyncStorePipeline& pipeline,
                std::vector<detail::Entry>&& entries
            );

            void store(
                detail::AsyncStorePipeline& pipeline,
                std::vector<detail::Entry>& entries,
                std::uint32_t id
            );

            void store(
                detail::AsyncStorePipeline& pipeline,
                std::vector<detail::Entry>&& entries,
                std::uint32_t id
            );
        };
    }
}
