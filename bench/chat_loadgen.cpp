#include <boost/asio.hpp>
#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using boost::asio::ip::tcp;

struct Session {
    std::string host;
    std::string token;
    int port = 0;
    int uid = 0;
    int peer_uid = 0;
};

struct Options {
    std::string sessions;
    int connections = 1;
    int rate = 100;
    int batch = 1;
    int duration = 10;
    int offset = 0;
    int timeout_ms = 5000;
    std::uint64_t seed = 1;
};

Options parse(int argc, char** argv) {
    if (argc % 2 == 0)
        throw std::runtime_error("every option requires a value");
    Options result;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        const std::string value = argv[i + 1];
        if (key == "--sessions")
            result.sessions = value;
        else if (key == "--connections")
            result.connections = std::stoi(value);
        else if (key == "--rate")
            result.rate = std::stoi(value);
        else if (key == "--batch")
            result.batch = std::stoi(value);
        else if (key == "--duration")
            result.duration = std::stoi(value);
        else if (key == "--timeout-ms")
            result.timeout_ms = std::stoi(value);
        else if (key == "--offset")
            result.offset = std::stoi(value);
        else if (key == "--seed")
            result.seed = std::stoull(value);
        else
            throw std::runtime_error("unknown option: " + key);
    }
    if (result.sessions.empty() || result.connections <= 0 || result.rate <= 0 ||
        result.batch <= 0 || result.batch > 10 || result.duration <= 0 || result.offset < 0 ||
        result.timeout_ms < 1 || result.connections > 10000) {
        throw std::runtime_error("usage: chat_loadgen --sessions sessions.json --connections N "
                                 "--rate N --batch N --duration S --seed N [--offset N]");
    }
    return result;
}

std::vector<Session> loadSessions(const std::string& path) {
    std::ifstream input(path);
    Json::Value root;
    input >> root;
    if (!root.isArray())
        throw std::runtime_error("sessions file must be a JSON array");
    std::vector<Session> sessions;
    for (const auto& item : root) {
        sessions.push_back({item["host"].asString(), item["token"].asString(), item["port"].asInt(),
                            item["uid"].asInt(), item["peer_uid"].asInt()});
    }
    return sessions;
}

thread_local int io_timeout_ms = 5000;
void transfer(tcp::socket& socket, void* data, std::size_t size, bool write,
              Clock::time_point deadline) {
    std::size_t offset = 0;
    while (offset < size) {
        if (Clock::now() >= deadline)
            throw std::runtime_error("network timeout");
        boost::system::error_code error;
        auto buffer = boost::asio::buffer(static_cast<char*>(data) + offset, size - offset);
        auto count = write ? socket.write_some(buffer, error) : socket.read_some(buffer, error);
        if (error == boost::asio::error::would_block || error == boost::asio::error::try_again) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (error)
            throw boost::system::system_error(error);
        if (!count)
            throw std::runtime_error("connection closed");
        offset += count;
    }
}
void connectWithDeadline(boost::asio::io_context& context, tcp::socket& socket,
                         const Session& session) {
    tcp::resolver resolver(context);
    boost::system::error_code result = boost::asio::error::timed_out;
    resolver.async_resolve(
        session.host, std::to_string(session.port), [&](auto error, auto endpoints) {
            if (error) {
                result = error;
                return;
            }
            boost::asio::async_connect(socket, endpoints, [&](auto ec, auto) { result = ec; });
        });
    context.run_for(std::chrono::milliseconds(io_timeout_ms));
    if (!context.stopped()) {
        resolver.cancel();
        socket.cancel();
        context.restart();
        context.run();
        throw std::runtime_error("connection timeout");
    }
    if (result)
        throw boost::system::system_error(result);
    socket.non_blocking(true);
}

void writeFrame(tcp::socket& socket, std::uint16_t id, const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, value);
    if (body.size() > 65535)
        throw std::runtime_error("frame exceeds 16-bit length");
    std::array<unsigned char, 4> header{
        {static_cast<unsigned char>(id >> 8), static_cast<unsigned char>(id),
         static_cast<unsigned char>(body.size() >> 8), static_cast<unsigned char>(body.size())}};
    const auto deadline = Clock::now() + std::chrono::milliseconds(io_timeout_ms);
    transfer(socket, header.data(), header.size(), true, deadline);
    transfer(socket, body.data(), body.size(), true, deadline);
}

std::pair<std::uint16_t, Json::Value> readFrame(tcp::socket& socket, Clock::time_point deadline) {
    std::array<unsigned char, 4> header{};
    transfer(socket, header.data(), header.size(), false, deadline);
    const auto id = static_cast<std::uint16_t>((header[0] << 8) | header[1]);
    const auto size = static_cast<std::uint16_t>((header[2] << 8) | header[3]);
    std::string body(size, '\0');
    transfer(socket, body.data(), body.size(), false, deadline);
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream input(body);
    if (!Json::parseFromStream(builder, input, &parsed, &errors))
        throw std::runtime_error(errors);
    return {id, parsed};
}

double percentile(std::vector<double> values, double p) {
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::ceil((values.size() - 1) * p));
    return values[index];
}
} // namespace

int main(int argc, char** argv) try {
    const auto options = parse(argc, argv);
    auto sessions = loadSessions(options.sessions);
    if (sessions.size() < static_cast<std::size_t>(options.offset + options.connections)) {
        throw std::runtime_error("one unconsumed login ticket is required per connection");
    }

    std::atomic<std::uint64_t> sent{0}, errors{0}, attempted{0}, setup_errors{0}, received{0};
    std::mutex samples_mutex;
    std::vector<double> samples;
    std::mutex start_mutex;
    std::condition_variable start_condition;
    int initialized = 0;
    bool start = false;
    Clock::time_point benchmark_start, deadline;
    std::vector<std::thread> workers;
    for (int index = 0; index < options.connections; ++index) {
        workers.emplace_back([&, index] {
            bool announced = false, inflight = false;
            try {
                io_timeout_ms = options.timeout_ms;
                const auto& session = sessions[static_cast<std::size_t>(options.offset + index)];
                boost::asio::io_context context;
                tcp::socket socket(context);
                connectWithDeadline(context, socket, session);
                Json::Value login;
                login["uid"] = session.uid;
                login["token"] = session.token;
                writeFrame(socket, 1005, login);
                auto login_response =
                    readFrame(socket, Clock::now() + std::chrono::milliseconds(io_timeout_ms));
                if (login_response.first != 1006 || !login_response.second["error"].isInt() ||
                    login_response.second["error"].asInt() != 0)
                    throw std::runtime_error("ticket rejected");
                {
                    std::unique_lock<std::mutex> lock(start_mutex);
                    ++initialized;
                    announced = true;
                    start_condition.notify_all();
                    start_condition.wait(lock, [&] { return start; });
                }
                std::unordered_set<std::string> seen;

                std::mt19937_64 random(options.seed + static_cast<std::uint64_t>(index));
                const auto interval = std::chrono::duration<double>(
                    static_cast<double>(options.connections * options.batch) / options.rate);
                auto next = Clock::now();
                while (Clock::now() < deadline) {
                    Json::Value request;
                    request["touid"] = session.peer_uid;
                    for (int n = 0; n < options.batch; ++n) {
                        Json::Value message;
                        message["msgid"] =
                            std::to_string(session.uid) + "-" + std::to_string(random());
                        message["content"] = "loadgen payload";
                        request["text_array"].append(message);
                    }
                    const auto started = Clock::now();
                    const auto response_deadline =
                        started + std::chrono::milliseconds(io_timeout_ms);
                    attempted += options.batch;
                    inflight = true;
                    writeFrame(socket, 1017, request);
                    for (;;) {
                        auto response = readFrame(socket, response_deadline);
                        if (response.first == 1019) {
                            Json::Value ack;
                            ack["fromuid"] = response.second["fromuid"];
                            for (const auto& message : response.second["text_array"]) {
                                const auto id = message["msgid"].asString();
                                ack["msgids"].append(id);
                                if (seen.insert(std::to_string(ack["fromuid"].asInt()) + ":" + id)
                                        .second)
                                    ++received;
                            }
                            writeFrame(socket, 1020, ack);
                            continue;
                        }
                        if (response.first != 1018)
                            continue;
                        if (!response.second["error"].isInt() ||
                            response.second["error"].asInt() != 0 ||
                            response.second["delivery"].asString() != "accepted")
                            throw std::runtime_error("message rejected");
                        if (response.second["text_array"] != request["text_array"])
                            throw std::runtime_error("response IDs/payload do not match request");
                        break;
                    }
                    const auto latency =
                        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
                    {
                        std::lock_guard<std::mutex> lock(samples_mutex);
                        samples.push_back(latency);
                    }
                    sent += static_cast<std::uint64_t>(options.batch);
                    inflight = false;
                    next = std::max(next + std::chrono::duration_cast<Clock::duration>(interval),
                                    Clock::now());
                    std::this_thread::sleep_until(std::min(next, deadline));
                }
            } catch (const std::exception&) {
                if (inflight)
                    errors += options.batch;
                if (!announced) {
                    ++setup_errors;
                    std::lock_guard<std::mutex> lock(start_mutex);
                    ++initialized;
                    start_condition.notify_all();
                }
            }
        });
    }
    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_condition.wait(lock, [&] { return initialized == options.connections; });
        benchmark_start = Clock::now();
        deadline = benchmark_start + std::chrono::seconds(options.duration);
        start = true;
    }
    start_condition.notify_all();
    for (auto& worker : workers)
        worker.join();
    const auto elapsed = std::chrono::duration<double>(Clock::now() - benchmark_start).count();
    Json::Value output;
    output["schema_version"] = 2;
    output["mode"] = "closed_loop_acceptance";
    output["connections"] = options.connections;
    output["configured_rate"] = options.rate;
    output["batch"] = options.batch;
    output["duration_seconds"] = elapsed;
    output["accepted_messages"] = Json::UInt64(sent.load());
    output["attempted_messages"] = Json::UInt64(attempted.load());
    output["setup_errors"] = Json::UInt64(setup_errors.load());
    output["received_unique_messages"] = Json::UInt64(received.load());
    output["throughput"] = sent.load() / elapsed;
    output["failed_messages"] = Json::UInt64(errors.load());
    output["error_rate"] = attempted ? static_cast<double>(errors.load()) / attempted.load() : 1.0;
    output["accept_p50_ms"] = percentile(samples, .50);
    output["accept_p95_ms"] = percentile(samples, .95);
    output["accept_p99_ms"] = percentile(samples, .99);
    output["seed"] = Json::UInt64(options.seed);
    std::cout << output << '\n';
    return errors || setup_errors || !sent ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << "chat_loadgen: " << error.what() << '\n';
    return 2;
}
