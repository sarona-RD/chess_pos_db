#pragma once

#include "Query.h"

#include "chess/GameClassification.h"

#include "enum/EnumArray.h"

#include <execution>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace persistence
{
    struct SingleGameLevelImportStats
    {
        std::size_t numGames = 0;
        std::size_t numSkippedGames = 0; // We skip games with an unknown result.
        std::size_t numPositions = 0;

        SingleGameLevelImportStats& operator+=(const SingleGameLevelImportStats& rhs);
    };

    struct ImportStats
    {
        EnumArray<GameLevel, SingleGameLevelImportStats> statsByLevel;

        ImportStats() = default;
        ImportStats(SingleGameLevelImportStats stats, GameLevel level);

        [[nodiscard]] std::size_t totalNumGames() const;
        [[nodiscard]] std::size_t totalNumSkippedGames() const;
        [[nodiscard]] std::size_t totalNumPositions() const;

        void add(SingleGameLevelImportStats stats, GameLevel level);

        ImportStats& operator+=(const ImportStats& rhs);
    };

    struct SingleGameLevelDatabaseStats
    {
        std::size_t numGames = 0;
        std::size_t numPositions = 0;

        SingleGameLevelDatabaseStats& operator+=(const SingleGameLevelImportStats& rhs);
    };

    struct DatabaseStats
    {
        EnumArray<GameLevel, SingleGameLevelDatabaseStats> statsByLevel;

        DatabaseStats& operator+=(const ImportStats& rhs);
    };

    enum struct ImportableFileType
    {
        Pgn,
        Bcgn,
        Unknown
    };

    [[nodiscard]] const std::string& importableFileTypeExtension(ImportableFileType type);
    [[nodiscard]] ImportableFileType importableFileTypeFromPath(const std::filesystem::path path);

    using ImportableFilePath = std::filesystem::path;
    using ImportableFilePaths = std::vector<std::filesystem::path>;

    struct ImportableFile
    {
        ImportableFile(std::filesystem::path path, GameLevel level);

        [[nodiscard]] const std::filesystem::path& path() const &;

        [[nodiscard]] ImportableFilePath path() &&;

        [[nodiscard]] GameLevel level() const;

        [[nodiscard]] ImportableFileType type() const;

    private:
        ImportableFilePath m_path;
        GameLevel m_level;
        ImportableFileType m_type;
    };

    using ImportableFiles = std::vector<ImportableFile>;

    struct DatabaseSupportManifest
    {
        std::vector<ImportableFileType> importableFileTypes;
    };

    struct DatabaseManifest
    {
        std::string key;
        bool requiresMatchingEndianness;
    };

    enum struct ManifestValidationResult
    {
        Ok,
        KeyMismatch,
        EndiannessMismatch,
        InvalidManifest
    };

    struct Database
    {
        struct ImportProgressReport
        {
            std::size_t workDone;
            std::size_t workTotal;
            std::optional<std::filesystem::path> importedPgnPath;

            [[nodiscard]] double ratio() const
            {
                return static_cast<double>(workDone) / static_cast<double>(workTotal);
            }
        };

        struct MergeProgressReport
        {
            std::size_t workDone;
            std::size_t workTotal;

            [[nodiscard]] double ratio() const
            {
                return static_cast<double>(workDone) / static_cast<double>(workTotal);
            }
        };

        using ImportProgressCallback = std::function<void(const ImportProgressReport&)>;
        using MergeProgressCallback = std::function<void(const MergeProgressReport&)>;

        Database(const std::filesystem::path& dirPath, const DatabaseManifest& manifestModel);

        [[nodiscard]] static std::filesystem::path manifestPath(const std::filesystem::path& dirPath);

        [[nodiscard]] static std::optional<std::string> tryReadKey(const std::filesystem::path& dirPath);

        [[nodiscard]] virtual const DatabaseManifest& manifest() const = 0;

        virtual const std::filesystem::path & path() const = 0;

        const DatabaseStats& stats() const;

        [[nodiscard]] virtual query::Response executeQuery(query::Request query) = 0;

        virtual void mergeAll(
            const std::vector<std::filesystem::path>& temporaryDirs,
            std::optional<MemoryAmount> temporarySpace,
            MergeProgressCallback progressCallback = {}
        ) = 0;

        virtual ImportStats import(
            const ImportableFiles& pgns, 
            std::size_t memory,
            ImportProgressCallback progressCallback = {}
        ) = 0;

        [[nodiscard]] virtual std::map<std::string, std::vector<std::string>> mergableFiles() const = 0;

        virtual void flush() = 0;

        virtual void clear() = 0;

        virtual ~Database();

    protected:
        void addStats(ImportStats stats);

    private:
        static const inline std::filesystem::path m_manifestFilename = "manifest";
        static const inline std::filesystem::path m_statsFilename = "stats";

        [[nodiscard]] static std::filesystem::path statsPath(const std::filesystem::path& dirPath);

        [[nodiscard]] std::filesystem::path statsPath() const;

        std::filesystem::path m_baseDirPath;
        DatabaseStats m_stats;
        DatabaseManifest m_manifestModel;

        void loadStats();
        void saveStats();

        [[nodiscard]] ManifestValidationResult createOrValidateManifest() const;

        void initializeManifest() const;

        void createManifest() const;

        [[nodiscard]] ManifestValidationResult validateManifest() const;

        [[nodiscard]] std::filesystem::path manifestPath() const;

        void writeManifest(const std::vector<std::byte>& data) const;

        std::vector<std::byte> readManifest() const;
    };
}
