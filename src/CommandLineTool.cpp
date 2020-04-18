#define _SILENCE_CXX17_C_HEADER_DEPRECATION_WARNING

#include "CommandLineTool.h"


#include "chess/GameClassification.h"

// IMPORTANT: If brynet is included AFTER nlohmann::json then linker requires
//            __imp_MapViewOfFileNuma2 to be present which breaks the build
//            (at least for windows <10)
//            Very peculiar behaviour. No cause nor solution found.
//            Cannot be reproduced in a clean project.
#define NOMINMAX
#include <brynet/net/SocketLibFunction.h>
#include <brynet/net/EventLoop.h>
#include <brynet/net/TCPService.h>
#include <brynet/net/ListenThread.h>
#include <brynet/net/Connector.h>
#include <brynet/net/Socket.h>

#include "chess/Bcgn.h"
#include "chess/GameClassification.h"
#include "chess/Pgn.h"
#include "chess/San.h"

#include "enum/EnumArray.h"

#include "persistence/pos_db/alpha/DatabaseFormatAlpha.h"
#include "persistence/pos_db/beta/DatabaseFormatBeta.h"
#include "persistence/pos_db/delta/DatabaseFormatDelta.h"
#include "persistence/pos_db/Database.h"
#include "persistence/pos_db/DatabaseFactory.h"
#include "persistence/pos_db/Query.h"

#include "util/MemoryAmount.h"

#include "Configuration.h"
#include "Logger.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <queue>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "json/json.hpp"

namespace command_line_app
{
    using namespace brynet::net;

    using Args = std::vector<std::string>;
    using CommandHandler = void(*)(const Args&);
    using TcpCommandHandler = void(*)(
        std::unique_ptr<persistence::Database>&,
        const TcpConnection::Ptr&,
        const nlohmann::json&);

    const std::size_t pgnImportMemory = cfg::g_config["console_app"]["pgn_import_memory"].get<MemoryAmount>();
    const std::size_t pgnParserMemory = cfg::g_config["console_app"]["pgn_parser_memory"].get<MemoryAmount>();
    const std::size_t bcgnParserMemory = cfg::g_config["console_app"]["bcgn_parser_memory"].get<MemoryAmount>();

    static void assertDirectoryNotEmpty(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path) || std::filesystem::is_empty(path))
        {
            throw Exception("Directory " + path.string() + " doesn't exist or is empty");
        }
    }

    static void assertDirectoryEmpty(const std::filesystem::path& path)
    {
        if (std::filesystem::exists(path) && !std::filesystem::is_empty(path))
        {
            throw Exception("Directory " + path.string() + " is not empty");
        }
    }

    static void assertFileExists(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path))
        {
            throw Exception("File " + path.string() + " does not exist.");
        }
    }

    static void assertDatabaseOpen(const std::unique_ptr<persistence::Database>& db)
    {
        if (db == nullptr)
        {
            throw Exception("No database open.");
        }
    }

    static void assertNoDatabaseOpen(const std::unique_ptr<persistence::Database>& db)
    {
        if (db != nullptr)
        {
            throw Exception("Database already open.");
        }
    }

    static void throwInvalidCommand(const std::string& command)
    {
        throw Exception("Invalid command: " + command);
    }

    static void throwInvalidArguments()
    {
        throw Exception("Invalid arguments. See help.");
    }

    static const persistence::DatabaseFactory g_factory = []() {
        persistence::DatabaseFactory g_factory;

        g_factory.registerDatabaseType<persistence::db_alpha::Database>();
        g_factory.registerDatabaseType<persistence::db_beta::Database>();
        g_factory.registerDatabaseType<persistence::db_delta::Database>();

        return g_factory;
    }();

    static auto instantiateDatabase(const std::string& key, const std::filesystem::path& destination)
    {
        auto ptr = g_factory.tryInstantiateByKey(key, destination);
        if (ptr == nullptr)
        {
            throw Exception("Invalid database type.");
        }
        return ptr;
    }

    static auto readKeyOfDatabase(const std::filesystem::path& path)
    {
        auto key = persistence::Database::tryReadKey(path);
        if (!key.has_value())
        {
            throw Exception("Directory " + path.string() + " does not contain a valid database.");
        }
        return *key;
    }

    static auto loadDatabase(const std::filesystem::path& path)
    {
        const auto key = readKeyOfDatabase(path);
        return instantiateDatabase(key, path);
    }

    [[nodiscard]] static Args convertCommandLineArguments(int argc, char* argv[])
    {
        std::vector<std::string> args;
        args.reserve(argc);
        for (int i = 0; i < argc; ++i)
        {
            args.emplace_back(argv[i]);
        }

        return args;
    }

    [[nodiscard]] static persistence::ImportablePgnFiles parsePgnListFile(const std::filesystem::path& path)
    {
        persistence::ImportablePgnFiles pgns;

        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream ss(line);
            std::string levelStr, pgnPath;
            std::getline(ss, levelStr, ';');
            if (levelStr.empty()) continue;
            const auto levelOpt = fromString<GameLevel>(levelStr);
            if (!levelOpt.has_value())
            {
                throw Exception("Invalid level: " + levelStr);
            }

            std::getline(ss, pgnPath, ';');
            pgns.emplace_back(pgnPath, *levelOpt);
        }

        return pgns;
    }

    static void help(const Args&)
    {
        std::cout << "create <type> <destination> <pgn_files> [<temp>]\n";
        std::cout << "merge <path> [<destination>]\n";
    }

    static void createImpl(
        const std::string& key,
        const std::filesystem::path& destination,
        const persistence::ImportablePgnFiles& pgns
    )
    {
        assertDirectoryEmpty(destination);

        auto db = instantiateDatabase(key, destination);
        db->import(pgns, pgnImportMemory);
    }

    static void createImpl(
        const std::string& key,
        const std::filesystem::path& destination,
        const persistence::ImportablePgnFiles& pgns,
        const std::filesystem::path& temp
    )
    {
        assertDirectoryEmpty(destination);
        assertDirectoryEmpty(temp);

        {
            auto db = instantiateDatabase(key, temp);
            db->import(pgns, pgnImportMemory);
            db->replicateMergeAll(destination);
        }

        std::filesystem::remove_all(temp);
    }

    static void create(const Args& args)
    {
        if (args.size() == 4)
        {
            createImpl(args[1], args[2], parsePgnListFile(args[3]));
        }
        else if (args.size() == 5)
        {
            createImpl(args[1], args[2], parsePgnListFile(args[3]), args[4]);
        }
        else
        {
            throwInvalidArguments();
        }
    }

    static void mergeImpl(const std::filesystem::path& path)
    {
        auto db = loadDatabase(path);
        db->mergeAll();
    }

    static void mergeImpl(const std::filesystem::path& fromPath, const std::filesystem::path& toPath)
    {
        assertDirectoryEmpty(toPath);

        auto db = loadDatabase(fromPath);
        db->replicateMergeAll(toPath);
    }

    static void merge(const Args& args)
    {
        if (args.size() == 2)
        {
            mergeImpl(args[1]);
        }
        else if (args.size() == 3)
        {
            mergeImpl(args[1], args[2]);
        }
    }

    static std::uint32_t receiveLength(const char* str)
    {
        constexpr std::uint32_t xorValue = 3173045653u;

        std::uint32_t size = 0;
        std::uint32_t xoredSize = 0;

        size += static_cast<std::uint32_t>(static_cast<unsigned char>(str[3])); size *= 256;
        size += static_cast<std::uint32_t>(static_cast<unsigned char>(str[2])); size *= 256;
        size += static_cast<std::uint32_t>(static_cast<unsigned char>(str[1])); size *= 256;
        size += static_cast<std::uint32_t>(static_cast<unsigned char>(str[0]));

        xoredSize += static_cast<std::uint32_t>(static_cast<unsigned char>(str[7])); xoredSize *= 256;
        xoredSize += static_cast<std::uint32_t>(static_cast<unsigned char>(str[6])); xoredSize *= 256;
        xoredSize += static_cast<std::uint32_t>(static_cast<unsigned char>(str[5])); xoredSize *= 256;
        xoredSize += static_cast<std::uint32_t>(static_cast<unsigned char>(str[4]));
        xoredSize ^= xorValue;

        if (size != xoredSize)
        {
            return 0;
        }
        return size;
    }

    // 4 bytes of size S in little endian
    // 4 bytes of size S xored with 3173045653u (for verification)
    // then S bytes
    static void sendMessage(
        const TcpConnection::Ptr& session,
        std::string message
    )
    {
        constexpr std::uint32_t xorValue = 3173045653u;

        std::uint32_t size = static_cast<std::uint32_t>(message.size());
        std::uint32_t xoredSize = size ^ xorValue;

        std::string sizeStr;
        sizeStr += static_cast<char>(size % 256); size /= 256;
        sizeStr += static_cast<char>(size % 256); size /= 256;
        sizeStr += static_cast<char>(size % 256); size /= 256;
        sizeStr += static_cast<char>(size);

        sizeStr += static_cast<char>(xoredSize % 256); xoredSize /= 256;
        sizeStr += static_cast<char>(xoredSize % 256); xoredSize /= 256;
        sizeStr += static_cast<char>(xoredSize % 256); xoredSize /= 256;
        sizeStr += static_cast<char>(xoredSize);
        session->send(sizeStr.c_str(), sizeStr.size());
        session->send(message.c_str(), message.size());
    }

    struct MessageReceiver
    {
        std::vector<std::string> onDataReceived(const char* buffer, std::size_t len)
        {
            constexpr std::uint32_t maxLength = 4 * 1024 * 1024;

            std::vector<std::string> messages;

            while (len != 0)
            {
                if (m_length == 0)
                {
                    if (len < 8)
                    {
                        throw Exception("Length did not arrive in one packet");
                    }

                    m_length = receiveLength(buffer);
                    if (m_length > maxLength)
                    {
                        throw Exception("Message too long");
                    }

                    m_message = "";

                    len -= 8;
                    buffer += 8;
                }
                else
                {
                    const std::size_t toRead = std::min(len, m_length);
                    m_message.append(buffer, toRead);
                    m_length -= toRead;

                    if (m_length == 0)
                    {
                        messages.emplace_back(std::move(m_message));
                        m_message.clear();
                    }

                    len -= toRead;
                    buffer += len;
                }
            }

            return messages;
        }

    private:
        std::string m_message;
        std::size_t m_length;
    };

    static void handleTcpRequest(
        persistence::Database& db,
        const TcpConnection::Ptr& session,
        const char* data,
        std::size_t len
    )
    {
        auto datastr = std::string(data, len);
        Logger::instance().logInfo("Received data: ", datastr);

        try
        {
            auto json = nlohmann::json::parse(datastr);
            query::Request request = json;
            if (request.isValid())
            {
                auto response = nlohmann::json(db.executeQuery(request)).dump();
                sendMessage(session, response);
                Logger::instance().logInfo("Handled valid request. Response size: ", response.size());
                return;
            }
        }
        catch (...)
        {
            Logger::instance().logInfo("Error parsing request");
        }

        Logger::instance().logInfo("Invalid request");

        auto errorJson = nlohmann::json::object({ {"error", "InvalidRequest" } }).dump();
        sendMessage(session, std::move(errorJson));
    }

    static void tcpImpl(const std::filesystem::path& path, std::uint16_t port)
    {
        struct Operation
        {
            TcpConnection::Ptr session;
            std::string data;
        };

        // TODO: Make it so only one connection is allowed.
        //       Or better, have one db per session

        std::queue<Operation> operations;
        std::condition_variable anyOperations;
        std::mutex mutex;

        auto db = loadDatabase(path);

        auto server = TcpService::Create();
        auto listenThread = ListenThread::Create(false, "127.0.0.1", port, [&](TcpSocket::Ptr socket) {
            socket->setNodelay();

            auto enterCallback = [&mutex, &anyOperations, &operations, &db](const TcpConnection::Ptr& session) {
                Logger::instance().logInfo("TCP connection from ", session->getIP());

                session->setDataCallback(
                    [&mutex, &anyOperations, &operations, &db, session, messageReceiver = MessageReceiver()]
                (const char* buffer, size_t len) mutable {
                    try
                    {
                        auto messages = messageReceiver.onDataReceived(buffer, len);
                        for (auto&& message : messages)
                        {
                            std::unique_lock lock(mutex);
                            operations.emplace(Operation{ session, std::move(message) });
                        }
                        if (!messages.empty())
                        {
                            anyOperations.notify_one();
                        }
                    }
                    catch (Exception& ex)
                    {
                        sendMessage(session, std::string("{\"error\":\"") + ex.what() + "\"");
                    }

                    return len;
                });

                session->setDisConnectCallback([](const TcpConnection::Ptr& session) {
                });
            };

            server->addTcpConnection(std::move(socket),
                brynet::net::TcpService::AddSocketOption::AddEnterCallback(enterCallback),
                brynet::net::TcpService::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024));
        });

        listenThread->startListen();
        server->startWorkerThread(1);

        EventLoop mainloop;

        std::thread workerThread([&]() {
            for (;;)
            {
                std::unique_lock lock(mutex);
                anyOperations.wait(lock, [&operations]() {return !operations.empty(); });

                auto operation = std::move(operations.front());
                operations.pop();

                lock.unlock();

                handleTcpRequest(*db, operation.session, operation.data.c_str(), operation.data.size());
            }
        });

        for (;;)
        {
            std::string line;
            std::getline(std::cin, line);
            if (line == "exit"sv)
            {
                return;
            }
        }
    }

    static void sendProgressFinished(const TcpConnection::Ptr& session, std::string operation, nlohmann::json additionalData = nlohmann::json::object())
    {
        nlohmann::json finishedResponse = nlohmann::json{
            { "overall_progress", 1.0f },
            { "finished", true },
            { "operation", operation }
        };
        finishedResponse.merge_patch(additionalData);
        std::string finisedResponseStr = finishedResponse.dump();

        sendMessage(session, std::move(finisedResponseStr));
    }

    static nlohmann::json statsToJson(persistence::ImportStats stats)
    {
        return nlohmann::json{
            { "num_games", stats.totalNumGames() },
            { "num_positions", stats.totalNumPositions() },
            { "num_skipped_games", stats.totalNumSkippedGames() }
        };
    }

    static auto makeImportProgressReportHandler(const TcpConnection::Ptr& session, bool doReportProgress = true)
    {
        return [session, doReportProgress](const persistence::Database::ImportProgressReport& report) {
            if (!doReportProgress) return;

            auto reportJson = nlohmann::json{
                { "operation", "import" },
                { "overall_progress", report.ratio() },
                { "finished", false }
            };
            if (report.importedPgnPath.has_value())
            {
                reportJson["imported_file_path"] = report.importedPgnPath->string();
            }

            auto reportStr = reportJson.dump();

            sendMessage(session, std::move(reportStr));
        };
    }

    static auto makeMergeProgressReportHandler(const TcpConnection::Ptr& session, bool doReportProgress = true)
    {
        return [session, doReportProgress](const persistence::Database::MergeProgressReport& report) {
            if (!doReportProgress) return;

            auto reportJson = nlohmann::json{
                { "operation", "merge" },
                { "overall_progress", report.ratio() },
                { "finished", false }
            };

            auto reportStr = reportJson.dump();
            sendMessage(session, reportStr);
        };
    }

    static void handleTcpCommandCreateImpl(
        std::unique_ptr<persistence::Database>&,
        const TcpConnection::Ptr& session,
        const std::string& key,
        const std::filesystem::path& destination,
        const persistence::ImportablePgnFiles& pgns,
        const std::filesystem::path& temp,
        bool doMerge,
        bool doReportProgress
    )
    {
        assertDirectoryEmpty(destination);
        assertDirectoryEmpty(temp);

        if (doMerge)
        {
            {
                auto db = instantiateDatabase(key, temp);

                {
                    auto callback = makeImportProgressReportHandler(session, doReportProgress);
                    auto stats = db->import(pgns, pgnImportMemory, callback);
                    sendProgressFinished(session, "import", statsToJson(stats));
                }

                {
                    auto callback = makeMergeProgressReportHandler(session, doReportProgress);
                    db->replicateMergeAll(destination, callback);
                }
            }

            std::filesystem::remove_all(temp);
            std::filesystem::create_directory(temp);
        }
        else
        {
            auto db = instantiateDatabase(key, destination);

            auto callback = makeImportProgressReportHandler(session, doReportProgress);
            auto stats = db->import(pgns, pgnImportMemory, callback);
            sendProgressFinished(session, "import", statsToJson(stats));
        }

        // We have to always sent some info that we finished
        sendProgressFinished(session, "create");
    }

    static void handleTcpCommandCreateImpl(
        std::unique_ptr<persistence::Database>&,
        const TcpConnection::Ptr& session,
        const std::string& key,
        const std::filesystem::path& destination,
        const persistence::ImportablePgnFiles& pgns,
        bool doMerge,
        bool doReportProgress
    )
    {
        assertDirectoryEmpty(destination);

        {
            auto db = instantiateDatabase(key, destination);

            auto callback = makeImportProgressReportHandler(session, doReportProgress);
            auto stats = db->import(pgns, pgnImportMemory, callback);
            sendProgressFinished(session, "import", statsToJson(stats));

            if (doMerge)
            {
                auto callback = makeMergeProgressReportHandler(session, doReportProgress);
                db->mergeAll(callback);
            }
        }

        // We have to always sent some info that we finished
        sendProgressFinished(session, "create");
    }

    static void handleTcpCommandCreate(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        const nlohmann::json& json
    )
    {
        const std::string destination = json["destination_path"].get<std::string>();
        const bool doMerge = json["merge"].get<bool>();
        const bool doReportProgress = json["report_progress"].get<bool>();
        
        persistence::ImportablePgnFiles pgns;
        for (auto& v : json["human_pgns"]) pgns.emplace_back(v.get<std::string>(), GameLevel::Human);
        for (auto& v : json["engine_pgns"]) pgns.emplace_back(v.get<std::string>(), GameLevel::Engine);
        for (auto& v : json["server_pgns"]) pgns.emplace_back(v.get<std::string>(), GameLevel::Server);

        const std::string databaseFormat = json["database_format"].get<std::string>();

        if (json.contains("temporary_path"))
        {
            const std::string temp = json["temporary_path"].get<std::string>();
            handleTcpCommandCreateImpl(db, session, databaseFormat, destination, pgns, temp, doMerge, doReportProgress);
        }
        else
        {
            handleTcpCommandCreateImpl(db, session, databaseFormat, destination, pgns, doMerge, doReportProgress);
        }
    }

    static void handleTcpCommandMergeImpl(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        const std::filesystem::path& destination,
        bool doReportProgress
    )
    {
        assertDirectoryEmpty(destination);
        assertDatabaseOpen(db);

        auto callback = makeMergeProgressReportHandler(session, doReportProgress);
        db->replicateMergeAll(destination, callback);

        // We have to always sent some info that we finished
        sendProgressFinished(session, "merge");
    }

    static void handleTcpCommandMergeImpl(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        bool doReportProgress
    )
    {
        assertDatabaseOpen(db);

        auto callback = makeMergeProgressReportHandler(session, doReportProgress);
        db->mergeAll(callback);

        // We have to always sent some info that we finished
        sendProgressFinished(session, "merge");
    }

    static void handleTcpCommandMerge(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        const nlohmann::json& json
    )
    {
        const bool doReportProgress = json["report_progress"].get<bool>();
        if (json.contains("destination_path"))
        {
            const std::string destination = json["destination_path"].get<std::string>();
            handleTcpCommandMergeImpl(db, session, destination, doReportProgress);
        }
        else
        {
            handleTcpCommandMergeImpl(db, session, doReportProgress);
        }
    }

    static void handleTcpCommandOpen(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        const nlohmann::json& json
    )
    {
        assertNoDatabaseOpen(db);

        const std::string dbPath = json["database_path"].get<std::string>();

        db = loadDatabase(dbPath);

        sendProgressFinished(session, "open");
    }

    static void handleTcpCommandClose(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        const nlohmann::json& json
    )
    {
        db.reset();

        sendProgressFinished(session, "close");
    }

    static void handleTcpCommandQuery(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        const nlohmann::json& json
    )
    {
        assertDatabaseOpen(db);

        query::Request request = json["query"];
        auto response = db->executeQuery(request);
        auto responseStr = nlohmann::json(response).dump();

        sendMessage(session, responseStr);
    }

    static void handleTcpCommandStats(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        const nlohmann::json& json
    )
    {
        assertDatabaseOpen(db);

        auto stats = db->stats();

        auto response = nlohmann::json{
            { "human", nlohmann::json{
                { "num_games", stats.statsByLevel[GameLevel::Human].numGames },
                { "num_positions", stats.statsByLevel[GameLevel::Human].numPositions }
            }},
            { "engine", nlohmann::json{
                { "num_games", stats.statsByLevel[GameLevel::Engine].numGames },
                { "num_positions", stats.statsByLevel[GameLevel::Engine].numPositions }
            }},
            { "server", nlohmann::json{
                { "num_games", stats.statsByLevel[GameLevel::Server].numGames },
                { "num_positions", stats.statsByLevel[GameLevel::Server].numPositions }
            }},
        };

        auto responseStr = nlohmann::json(response).dump();
        sendMessage(session, responseStr);
    }

    static void handleTcpCommandDumpImpl(
        const TcpConnection::Ptr& session,
        const std::vector<std::filesystem::path>& pgns,
        const std::filesystem::path& output,
        std::size_t minN,
        bool doReportProgress
    )
    {
        std::vector<CompressedPosition> positions;

        {
            auto callback = makeImportProgressReportHandler(session, doReportProgress);
            std::size_t i = 0;
            for (auto&& pgn : pgns)
            {
                pgn::LazyPgnFileReader reader(pgn);
                for (auto&& game : reader)
                {
                    for (auto&& position : game.positions())
                    {
                        positions.emplace_back(position.compress());
                    }
                }

                ++i;
                callback({
                    i,
                    pgns.size(),
                    pgn
                    });
            }

            sendProgressFinished(session, "import");
        }

        std::sort(positions.begin(), positions.end());

        auto forEachWithCount = [&positions](auto&& func) {
            const std::size_t size = positions.size();
            std::size_t lastUnique = 0;
            for (std::size_t i = 1; i < size; ++i)
            {
                if (!(positions[i - 1] == positions[i]))
                {
                    func(positions[i - 1], i - lastUnique);
                    lastUnique = i;
                }
            }
        };

        {
            constexpr std::size_t reportEvery = 10'000'000;

            std::ofstream outEpdFile(output, std::ios_base::out | std::ios_base::app);
            std::size_t nextReport = 0;
            std::size_t totalCount = 0;
            std::size_t passed = 0;
            forEachWithCount([&](const CompressedPosition& pos, std::size_t count)
                {
                    if (count >= minN)
                    {
                        outEpdFile << pos.decompress().fen() << ";\n";
                        ++passed;
                    }
                    totalCount += count;

                    if (totalCount >= nextReport)
                    {
                        if (doReportProgress)
                        {
                            auto reportJson = nlohmann::json{
                                { "operation", "dump" },
                                { "overall_progress", static_cast<double>(totalCount) / positions.size() },
                                { "finished", false }
                            };

                            auto reportStr = reportJson.dump();
                            sendMessage(session, reportStr);
                        }

                        nextReport += reportEvery;
                    }
                }
            );

            sendProgressFinished(session, "dump");
        }
    }

    namespace detail
    {
        struct AsyncStorePipeline
        {
        private:
            using EntryType = CompressedPosition;
            using BufferType = std::vector<EntryType>;
            using PromiseType = std::promise<std::filesystem::path>;
            using FutureType = std::future<std::filesystem::path>;

            struct Job
            {
                Job(std::filesystem::path path, BufferType&& buffer, PromiseType&& promise) :
                    path(std::move(path)),
                    buffer(std::move(buffer)),
                    promise(std::move(promise))
                {
                }

                std::filesystem::path path;
                BufferType buffer;
                PromiseType promise;
            };

        public:
            AsyncStorePipeline(std::vector<BufferType>&& buffers, std::size_t numSortingThreads = 1) :
                m_sortingThreadFinished(false),
                m_writingThreadFinished(false),
                m_writingThread([this]() { runWritingThread(); })
            {
                ASSERT(numSortingThreads >= 1);
                ASSERT(!buffers.empty());

                m_sortingThreads.reserve(numSortingThreads);
                for (std::size_t i = 0; i < numSortingThreads; ++i)
                {
                    m_sortingThreads.emplace_back([this]() { runSortingThread(); });
                }

                for (auto&& buffer : buffers)
                {
                    m_bufferQueue.emplace(std::move(buffer));
                }
            }

            AsyncStorePipeline(const AsyncStorePipeline&) = delete;

            ~AsyncStorePipeline()
            {
                waitForCompletion();
            }

            [[nodiscard]] FutureType scheduleUnordered(const std::filesystem::path& path, BufferType&& elements)
            {
                std::unique_lock<std::mutex> lock(m_mutex);

                PromiseType promise;
                FutureType future = promise.get_future();
                m_sortQueue.emplace(path, std::move(elements), std::move(promise));

                lock.unlock();
                m_sortQueueNotEmpty.notify_one();

                return future;
            }

            [[nodiscard]] BufferType getEmptyBuffer()
            {
                std::unique_lock<std::mutex> lock(m_mutex);

                m_bufferQueueNotEmpty.wait(lock, [this]() {return !m_bufferQueue.empty(); });

                auto buffer = std::move(m_bufferQueue.front());
                m_bufferQueue.pop();

                buffer.clear();

                return buffer;
            }

            void waitForCompletion()
            {
                if (!m_sortingThreadFinished.load())
                {
                    m_sortingThreadFinished.store(true);
                    m_sortQueueNotEmpty.notify_one();
                    for (auto& th : m_sortingThreads)
                    {
                        th.join();
                    }

                    m_writingThreadFinished.store(true);
                    m_writeQueueNotEmpty.notify_one();
                    m_writingThread.join();
                }
            }

        private:
            std::queue<Job> m_sortQueue;
            std::queue<Job> m_writeQueue;
            std::queue<BufferType> m_bufferQueue;

            std::condition_variable m_sortQueueNotEmpty;
            std::condition_variable m_writeQueueNotEmpty;
            std::condition_variable m_bufferQueueNotEmpty;

            std::mutex m_mutex;

            std::atomic_bool m_sortingThreadFinished;
            std::atomic_bool m_writingThreadFinished;

            std::vector<std::thread> m_sortingThreads;
            std::thread m_writingThread;

            void runSortingThread()
            {
                for (;;)
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_sortQueueNotEmpty.wait(lock, [this]() {return !m_sortQueue.empty() || m_sortingThreadFinished.load(); });

                    if (m_sortQueue.empty())
                    {
                        lock.unlock();
                        m_sortQueueNotEmpty.notify_one();
                        return;
                    }

                    Job job = std::move(m_sortQueue.front());
                    m_sortQueue.pop();

                    lock.unlock();

                    sort(job.buffer);

                    lock.lock();
                    m_writeQueue.emplace(std::move(job));
                    lock.unlock();

                    m_writeQueueNotEmpty.notify_one();
                }
            }

            void runWritingThread()
            {
                for (;;)
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_writeQueueNotEmpty.wait(lock, [this]() {return !m_writeQueue.empty() || m_writingThreadFinished.load(); });

                    if (m_writeQueue.empty())
                    {
                        lock.unlock();
                        m_writeQueueNotEmpty.notify_one();
                        return;
                    }

                    Job job = std::move(m_writeQueue.front());
                    m_writeQueue.pop();

                    lock.unlock();

                    (void)ext::writeFile(job.path, job.buffer.data(), job.buffer.size());
                    job.promise.set_value(job.path);

                    job.buffer.clear();

                    lock.lock();
                    m_bufferQueue.emplace(std::move(job.buffer));
                    lock.unlock();

                    m_bufferQueueNotEmpty.notify_one();
                }
            }

            void sort(BufferType& buffer)
            {
                std::sort(buffer.begin(), buffer.end());
            }
        };
    }

    static void handleTcpCommandDumpImpl(
        const TcpConnection::Ptr& session,
        const std::vector<std::filesystem::path>& pgns,
        const std::filesystem::path& output,
        const std::filesystem::path& temp,
        std::size_t minN,
        bool doReportProgress
    )
    {
        static const std::size_t pgnParserMemory = cfg::g_config["command_line_app"]["dump"]["pgn_parser_memory"].get<MemoryAmount>();
        static const std::size_t importMemory = cfg::g_config["command_line_app"]["dump"]["pgn_import_memory"].get<MemoryAmount>();
        static const std::size_t mergeMemory = cfg::g_config["command_line_app"]["dump"]["max_merge_buffer_size"].get<MemoryAmount>();

        assertDirectoryEmpty(temp);

        std::size_t numPosOut = 0;
        std::size_t numPosIn = 0;
        std::size_t numGamesIn = 0;

        // this has to be destroyed last
        ext::TemporaryPaths tempPaths(temp);

        auto makeBuffers = [](std::size_t numBuffers)
        {
            ASSERT(numBuffers > 0);

            const std::size_t size = ext::numObjectsPerBufferUnit<CompressedPosition>(importMemory, numBuffers);

            std::vector<std::vector<CompressedPosition>> buffers;
            buffers.resize(numBuffers);
            for (auto& buffer : buffers)
            {
                buffer.reserve(size);
            }
            return buffers;
        };

        {
            std::vector<std::future<std::filesystem::path>> futureParts;

            {
                detail::AsyncStorePipeline pipeline(makeBuffers(4), 2);

                auto callback = makeImportProgressReportHandler(session, doReportProgress);

                std::size_t i = 0;
                auto positions = pipeline.getEmptyBuffer();

                for (auto&& pgn : pgns)
                {
                    pgn::LazyPgnFileReader reader(pgn, pgnParserMemory);
                    for (auto&& game : reader)
                    {
                        ++numGamesIn;

                        for (auto&& position : game.positions())
                        {
                            ++numPosIn;

                            positions.emplace_back(position.compress());

                            if (positions.size() == positions.capacity())
                            {
                                auto path = tempPaths.next();
                                futureParts.emplace_back(pipeline.scheduleUnordered(path, std::move(positions)));
                                positions = pipeline.getEmptyBuffer();
                                Logger::instance().logInfo("Created temp file ", path);
                            }
                        }
                    }

                    ++i;
                    callback({
                        i,
                        pgns.size(),
                        pgn
                        });

                    Logger::instance().logInfo("Finished file ", pgn);
                }

                if (!positions.empty())
                {
                    auto path = tempPaths.next();
                    futureParts.emplace_back(pipeline.scheduleUnordered(path, std::move(positions)));
                    Logger::instance().logInfo("Created temp file ", path);
                }

                sendProgressFinished(session, "import");
            }

            {
                std::vector<ext::ImmutableSpan<CompressedPosition>> files;
                for (auto&& f : futureParts)
                {
                    auto path = f.get();
                    files.emplace_back(ext::ImmutableBinaryFile(ext::Pooled{}, path));
                    Logger::instance().logInfo("Commited file ", path);
                }

                auto progressCallback = [session, doReportProgress](const ext::ProgressReport& report) {
                    if (!doReportProgress) return;

                    auto reportJson = nlohmann::json{
                        { "operation", "dump" },
                        { "overall_progress", report.ratio() },
                        { "finished", false }
                    };

                    auto reportStr = reportJson.dump();
                    sendMessage(session, reportStr);
                }; 
                
                auto append = [
                        &numPosOut,
                        minN,
                        outEpdFile = std::ofstream(output, std::ios_base::out | std::ios_base::app), 
                        first = true, 
                        pos = CompressedPosition{}, 
                        count = 0
                    ](const CompressedPosition& position) mutable {
                    if (first)
                    {
                        first = false;
                        pos = position;
                        count = 1;
                    }
                    else if (pos == position)
                    {
                        ++count;
                    }
                    else
                    {
                        if (count >= minN)
                        {
                            ++numPosOut;
                            outEpdFile << pos.decompress().fen() << ";\n";
                        }

                        pos = position;
                        count = 1;
                    }
                };
                
                ext::merge_for_each(progressCallback, { mergeMemory }, files, append, std::less<>{});
            }

            auto stats = nlohmann::json{
                { "num_games", numGamesIn },
                { "num_in_positions", numPosIn },
                { "num_out_positions", numPosOut }
            };
            sendProgressFinished(session, "dump", stats);
        }
    }

    static void handleTcpCommandDump(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        const nlohmann::json& json
    )
    {
        std::vector<std::filesystem::path> pgns;
        for (auto& v : json["pgns"]) pgns.emplace_back(v.get<std::string>());

        const std::filesystem::path epdOut = json["output_path"].get<std::string>();
        const bool reportProgress = json["report_progress"];
        const std::size_t minN = json["min_count"].get<std::size_t>();

        if (minN == 0) throw Exception("Min count must be positive.");

        if (json.contains("temporary_path"))
        {
            const std::filesystem::path temp = json["temporary_path"].get<std::string>();
            handleTcpCommandDumpImpl(session, pgns, epdOut, temp, minN, reportProgress);
        }
        else
        {
            handleTcpCommandDumpImpl(session, pgns, epdOut, minN, reportProgress);
        }
    }

    static bool handleTcpCommand(
        std::unique_ptr<persistence::Database>& db,
        const TcpConnection::Ptr& session,
        const char* data,
        std::size_t len
    )
    {
        static std::map<std::string, TcpCommandHandler> s_handlers{
            { "create", handleTcpCommandCreate },
            { "merge", handleTcpCommandMerge },
            { "open", handleTcpCommandOpen },
            { "close", handleTcpCommandClose },
            { "query", handleTcpCommandQuery },
            { "stats", handleTcpCommandStats },
            { "dump", handleTcpCommandDump }
        };

        auto datastr = std::string(data, len);
        Logger::instance().logInfo("Received data: ", datastr);

        try
        {
            nlohmann::json json = nlohmann::json::parse(datastr);

            const std::string command = json["command"].get<std::string>();
            if (command == "exit") return true;

            s_handlers.at(command)(db, session, json);

            return false;
        }
        catch (std::runtime_error& ex)
        {
            Logger::instance().logError("Error while trying to perform request");

            auto errorJson = nlohmann::json::object({ {"error", ex.what() } }).dump();
            sendMessage(session, errorJson);
        }
        catch (...)
        {
            Logger::instance().logError("Unknown error");

            auto errorJson = nlohmann::json::object({ {"error", "Unknown error" } }).dump();
            sendMessage(session, errorJson);
        }

        return false;
    }

    static void tcpImpl(std::uint16_t port)
    {
        struct Operation
        {
            TcpConnection::Ptr session;
            std::string data;
        };

        // TODO: Make it so only one connection is allowed.
        //       Or better, have one db per session

        std::unique_ptr<persistence::Database> db = nullptr;

        std::queue<Operation> operations;
        std::condition_variable anyOperations;
        std::mutex mutex;

        auto server = TcpService::Create();
        auto listenThread = ListenThread::Create(false, "127.0.0.1", port, [&](TcpSocket::Ptr socket) {
            socket->setNodelay();

            auto enterCallback = [&mutex, &anyOperations, &operations, &db](const TcpConnection::Ptr& session) {
                Logger::instance().logInfo("TCP connection from ", session->getIP());

                session->setDataCallback(
                    [&mutex, &anyOperations, &operations, &db, session, messageReceiver = MessageReceiver()]
                    (const char* buffer, size_t len) mutable {
                        try
                        {
                            auto messages = messageReceiver.onDataReceived(buffer, len);
                            for (auto&& message : messages)
                            {
                                std::unique_lock lock(mutex);
                                operations.emplace(Operation{ session, std::move(message) });
                            }
                            if (!messages.empty())
                            {
                                anyOperations.notify_one();
                            }
                        }
                        catch (Exception& ex)
                        {
                            sendMessage(session, std::string("{\"error\":\"") + ex.what() + "\"");
                        }

                        return len;
                    });

                session->setDisConnectCallback([](const TcpConnection::Ptr& session) {
                    });
            };

            server->addTcpConnection(std::move(socket),
                brynet::net::TcpService::AddSocketOption::AddEnterCallback(enterCallback),
                brynet::net::TcpService::AddSocketOption::WithMaxRecvBufferSize(1024 * 1024));
            });

        listenThread->startListen();
        server->startWorkerThread(3);

        EventLoop mainloop;

        for (;;)
        {
            std::unique_lock lock(mutex);
            anyOperations.wait(lock, [&operations]() {return !operations.empty(); });

            auto operation = std::move(operations.front());
            operations.pop();

            lock.unlock();

            if (handleTcpCommand(db, operation.session, operation.data.c_str(), operation.data.size()))
            {
                break;
            }
        }
    }

    static void tcp(const Args& args)
    {
        if (args.size() == 3)
        {
            const int port = std::stoi(args[2]);
            if (port <= 0 || port > std::numeric_limits<std::uint64_t>::max())
            {
                throwInvalidArguments();
            }

            tcpImpl(args[1], static_cast<std::uint16_t>(port));
        }
        else if (args.size() == 2)
        {
            const int port = std::stoi(args[1]);
            if (port <= 0 || port > std::numeric_limits<std::uint64_t>::max())
            {
                throwInvalidArguments();
            }

            tcpImpl(static_cast<std::uint16_t>(port));
        }
        else
        {
            throwInvalidArguments();
        }
    }

    static void convertPgnToBcgnImpl(
        const std::filesystem::path& pgn, 
        const std::filesystem::path& bcgn,
        const bcgn::BcgnFileHeader& header,
        bcgn::BcgnFileWriter::FileOpenMode mode)
    {
        pgn::LazyPgnFileReader pgnReader(pgn, pgnParserMemory);
        bcgn::BcgnFileWriter bcgnWriter(bcgn, header, mode, bcgnParserMemory);

        constexpr std::size_t reportEvery = 100'000;

        std::size_t nextReport = 0;
        std::size_t totalCount = 0;
        for (auto&& game : pgnReader)
        {
            Position pos = Position::startPosition();

            bcgnWriter.beginGame();

            std::optional<GameResult> result;
            Date date;
            Eco eco;
            std::string_view event;
            std::string_view white;
            std::string_view black;
            game.getResultDateEcoEventWhiteBlack(
                result,
                date,
                eco,
                event,
                white,
                black
                );

            bcgnWriter.setWhiteElo(game.whiteElo());
            bcgnWriter.setBlackElo(game.blackElo());
            bcgnWriter.setDate(date);
            bcgnWriter.setEco(eco);
            bcgnWriter.setRound(game.round());
            bcgnWriter.setWhitePlayer(white);
            bcgnWriter.setBlackPlayer(black);
            bcgnWriter.setEvent(event);
            bcgnWriter.setSite(game.tag("Site"sv));
            if (result.has_value())
            {
                bcgnWriter.setResult(*result);
            }

            for (auto&& san : game.moves())
            {
                const auto move = san::sanToMove(pos, san);

                bcgnWriter.addMove(pos, move);

                pos.doMove(move);
            }

            bcgnWriter.endGame();

            ++totalCount;
            if (totalCount >= nextReport)
            {
                std::cout << "Converted " << totalCount << " games...\n";
                nextReport += reportEvery;
            }
        }
        std::cout << "Converted " << totalCount << " games...\n";
    }

    static void convert(const Args& args)
    {
        if (args.size() < 3)
        {
            throwInvalidArguments();
        }

        const std::filesystem::path from = args[1];
        const std::filesystem::path to = args[2];

        if (from.extension() == ".pgn" && to.extension() == ".bcgn")
        {
            bcgn::BcgnFileHeader header{};
            auto mode = bcgn::BcgnFileWriter::FileOpenMode::Truncate;

            if (args.size() >= 4)
            {
                switch (std::stoi(args[3]))
                {
                case 0:
                    header.compressionLevel = bcgn::BcgnCompressionLevel::Level_0;
                    break;

                case 1:
                    header.compressionLevel = bcgn::BcgnCompressionLevel::Level_1;
                    break;
                }
            }

            if (args.size() >= 5)
            {
                if (args[4] == "a")
                {
                    mode = bcgn::BcgnFileWriter::FileOpenMode::Append;
                }
            }

            convertPgnToBcgnImpl(from, to, header, mode);
        }
        else
        {
            throwInvalidArguments();
        }
    }

    static void countPgnGames(const std::filesystem::path& path)
    {
        pgn::LazyPgnFileReader reader(path, pgnParserMemory);

        constexpr std::size_t reportEvery = 100'000;

        std::size_t nextReport = 0;
        std::size_t totalCount = 0;
        for (auto&& game : reader)
        {
            ++totalCount;
            if (totalCount >= nextReport)
            {
                std::cout << "Found " << totalCount << " games...\n";
                nextReport += reportEvery;
            }
        }
        std::cout << "Found " << totalCount << " games...\n";
    }

    static void countBcgnGames(const std::filesystem::path& path)
    {
        bcgn::BcgnFileReader reader(path, bcgnParserMemory);

        constexpr std::size_t reportEvery = 100'000;

        std::size_t nextReport = 0;
        std::size_t totalCount = 0;
        for (auto&& game : reader)
        {
            ++totalCount;
            if (totalCount >= nextReport)
            {
                std::cout << "Found " << totalCount << " games...\n";
                nextReport += reportEvery;
            }
        }
        std::cout << "Found " << totalCount << " games...\n";
    }

    static void countGames(const Args& args)
    {
        if (args.size() < 2)
        {
            throwInvalidArguments();
        }

        const std::filesystem::path path = args[1];
        if (path.extension() == ".pgn")
        {
            countPgnGames(path);
        }
        else if (path.extension() == ".bcgn")
        {
            countBcgnGames(path);
        }
        else
        {
            throwInvalidArguments();
        }
    }

    template <typename ReaderT>
    static void benchReader(const std::filesystem::path& path, std::size_t memory)
    {
        const auto size = std::filesystem::file_size(path);
        std::cout << "File size: " << size << '\n';

        for (int i = 0; i < 2; ++i)
        {
            // warmup
            ReaderT reader(path, memory);
            for (auto&& game : reader);
            std::cout << "warmup " << i << " finished\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds{ 1 });

        const auto t0 = std::chrono::high_resolution_clock::now();
        ReaderT reader(path, memory);
        std::size_t numGames = 0;
        std::size_t numPositions = 0;
        for (auto&& game : reader)
        {
            numGames += 1;
            for (auto&& position : game.positions())
            {
                numPositions += 1;
            }
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double time = (t1 - t0).count() / 1e9;

        std::cout << numGames << " games in " << time << "s\n";
        std::cout << (std::uint64_t)(numGames / time) << " games/s\n";
        std::cout << numPositions << " positions in " << time << "s\n";
        std::cout << (std::uint64_t)(numPositions / time) << " positions/s\n";
        std::cout << "Throughput of " << size / time / 1e6 << " MB/s\n";
    }

    static void benchPgn(const std::filesystem::path& path)
    {
        benchReader<pgn::LazyPgnFileReader>(path, pgnParserMemory);
    }

    static void benchBcgn(const std::filesystem::path& path)
    {
        benchReader<bcgn::BcgnFileReader>(path, bcgnParserMemory);
    }

    static void bench(const Args& args)
    {
        if (args.size() < 2)
        {
            throwInvalidArguments();
        }

        const std::filesystem::path path = args[1];
        if (path.extension() == ".pgn")
        {
            benchPgn(path);
        }
        else if (path.extension() == ".bcgn")
        {
            benchBcgn(path);
        }
        else
        {
            throwInvalidArguments();
        }
    }

    void runCommand(int argc, char* argv[])
    {
        static const std::map<std::string, CommandHandler> s_commandHandlers = {
            { "help", help },
            { "create", create },
            { "merge", merge },
            { "tcp", tcp },
            { "convert", convert },
            { "count_games", countGames },
            { "bench", bench }
        };

        if (argc <= 0) return;

        auto args = convertCommandLineArguments(argc, argv);

        auto handlerIt = s_commandHandlers.find(args[0]);
        if (handlerIt == s_commandHandlers.end())
        {
            throwInvalidCommand(args[0]);
        }

        handlerIt->second(args);
    }
}
