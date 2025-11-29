#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cctype>
#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
constexpr int kDefaultPort = 8080;
constexpr int kBacklog = 16;
constexpr size_t kBufferSize = 8192;

std::string now_iso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string json_escape(const std::string &input) {
    std::ostringstream oss;
    for (char c : input) {
        switch (c) {
            case '\\':
                oss << "\\\\";
                break;
            case '"':
                oss << "\\\"";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    oss << "\\u";
                    oss << std::hex << std::uppercase << static_cast<int>(c);
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

std::string trim(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::unordered_map<std::string, std::string> parse_headers(const std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::string> headers;
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string &line = lines[i];
        auto pos = line.find(":");
        if (pos == std::string::npos) continue;
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        for (char &c : key) {
            c = std::tolower(static_cast<unsigned char>(c));
        }
        headers[key] = value;
    }
    return headers;
}

bool read_http_request(int client_fd, HttpRequest &req) {
    std::string data;
    char buffer[kBufferSize];
    ssize_t received = 0;

    while (data.find("\r\n\r\n") == std::string::npos) {
        received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return false;
        }
        data.append(buffer, buffer + received);
        if (data.size() > 128 * 1024) {
            return false;  // too large
        }
    }

    size_t header_end = data.find("\r\n\r\n");
    std::string header_block = data.substr(0, header_end);
    std::string remaining = data.substr(header_end + 4);

    std::vector<std::string> lines;
    std::istringstream iss(header_block);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (lines.empty()) return false;

    std::istringstream start_line(lines[0]);
    if (!(start_line >> req.method >> req.path)) return false;
    req.headers = parse_headers(lines);

    size_t content_length = 0;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
        content_length = static_cast<size_t>(std::stoul(it->second));
    }

    while (remaining.size() < content_length) {
        received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        remaining.append(buffer, buffer + received);
    }
    if (remaining.size() > content_length) {
        remaining.resize(content_length);
    }
    req.body = std::move(remaining);
    return true;
}

void write_response(int client_fd, int status, const std::string &content, const std::string &content_type = "application/json") {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " \r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << content.size() << "\r\n";
    oss << "Connection: close\r\n\r\n";
    oss << content;
    const std::string payload = oss.str();
    send(client_fd, payload.data(), payload.size(), 0);
}

std::string find_json_value(const std::string &body, const std::string &key) {
    const std::string pattern = "\"" + key + "\"";
    auto pos = body.find(pattern);
    if (pos == std::string::npos) return "";
    pos = body.find(":", pos + pattern.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) pos++;
    if (pos >= body.size()) return "";
    if (body[pos] == '"') {
        size_t end = body.find('"', pos + 1);
        while (end != std::string::npos && body[end - 1] == '\\') {
            end = body.find('"', end + 1);
        }
        if (end != std::string::npos) return body.substr(pos + 1, end - pos - 1);
    } else {
        size_t end = pos;
        while (end < body.size() && !std::isspace(static_cast<unsigned char>(body[end])) && body[end] != ',' && body[end] != '}') {
            ++end;
        }
        return trim(body.substr(pos, end - pos));
    }
    return "";
}

bool find_json_bool(const std::string &body, const std::string &key, bool default_value) {
    std::string val = find_json_value(body, key);
    if (val == "true" || val == "1") return true;
    if (val == "false" || val == "0") return false;
    return default_value;
}

class SessionStore {
   public:
    std::string get_or_create_summary(const std::string &session_id, const std::string &user_input) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &summary = sessions_[session_id];
        if (!summary.empty()) summary += " | ";
        summary += user_input.substr(0, 120);
        return summary;
    }

   private:
    std::unordered_map<std::string, std::string> sessions_;
    std::mutex mutex_;
};

class InferenceEngine {
   public:
    std::string detect_intent(const std::string &text) const {
        if (text.find("ses") != std::string::npos || text.find("audio") != std::string::npos) return "speech";
        if (text.find("yardım") != std::string::npos) return "support";
        if (text.find("hava") != std::string::npos) return "weather";
        return "general";
    }

    std::string respond(const std::string &text, const std::string &mode) const {
        std::ostringstream oss;
        oss << "[" << mode << "] " << text << " -> analitik değerlendirme: ";
        if (text.size() > 80) {
            oss << text.substr(0, 80) << "...";
        } else {
            oss << text;
        }
        oss << ".";
        return oss.str();
    }

    std::vector<std::string> streaming_chunks(const std::string &text) const {
        std::vector<std::string> chunks;
        for (size_t i = 0; i < text.size(); i += 48) {
            chunks.push_back(text.substr(i, 48));
        }
        if (chunks.empty()) chunks.push_back("boş istek");
        return chunks;
    }

    std::string describe_audio(const std::string &base64_audio, const std::string &transcript_hint) const {
        std::ostringstream oss;
        oss << "Ses paketi uzunluğu: " << base64_audio.size() << " karakter;";
        if (!transcript_hint.empty()) {
            oss << " tahmini metin: " << transcript_hint;
        } else {
            oss << " ham veri ön-işleme hazır.";
        }
        return oss.str();
    }
};

void handle_health(int client_fd) {
    std::string body = "{\"status\":\"ok\",\"timestamp\":\"" + now_iso8601() + "\"}";
    write_response(client_fd, 200, body);
}

void handle_chat(int client_fd, const HttpRequest &req, InferenceEngine &engine, SessionStore &store) {
    const std::string text = find_json_value(req.body, "text");
    if (text.empty()) {
        write_response(client_fd, 400, "{\"error\":\"text alanı zorunlu\"}");
        return;
    }
    const std::string mode = find_json_value(req.body, "mode");
    const std::string session_id = find_json_value(req.body, "session_id").empty() ? "default" : find_json_value(req.body, "session_id");
    const bool stream = find_json_bool(req.body, "stream", false);

    std::string summary = store.get_or_create_summary(session_id, text);
    const std::string intent = engine.detect_intent(text);
    if (stream) {
        auto chunks = engine.streaming_chunks(engine.respond(text, mode.empty() ? "text" : mode));
        std::ostringstream oss;
        oss << "{\"session_id\":\"" << json_escape(session_id) << "\",";
        oss << "\"intent\":\"" << json_escape(intent) << "\",";
        oss << "\"streaming\":[";
        for (size_t i = 0; i < chunks.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << json_escape(chunks[i]) << "\"";
        }
        oss << "],\"summary\":\"" << json_escape(summary) << "\"}";
        write_response(client_fd, 200, oss.str());
    } else {
        std::ostringstream oss;
        oss << "{\"session_id\":\"" << json_escape(session_id) << "\",";
        oss << "\"intent\":\"" << json_escape(intent) << "\",";
        oss << "\"reply\":\"" << json_escape(engine.respond(text, mode.empty() ? "text" : mode)) << "\",";
        oss << "\"summary\":\"" << json_escape(summary) << "\"}";
        write_response(client_fd, 200, oss.str());
    }
}

void handle_audio(int client_fd, const HttpRequest &req, InferenceEngine &engine, SessionStore &) {
    const std::string audio = find_json_value(req.body, "audio_b64");
    const std::string transcript = find_json_value(req.body, "transcript_hint");
    if (audio.empty()) {
        write_response(client_fd, 400, "{\"error\":\"audio_b64 zorunlu\"}");
        return;
    }
    const std::string summary = engine.describe_audio(audio, transcript);
    std::ostringstream oss;
    oss << "{\"audio_preview\":\"" << json_escape(audio.substr(0, 24)) << "...\",";
    oss << "\"analysis\":\"" << json_escape(summary) << "\"}";
    write_response(client_fd, 200, oss.str());
}

void handle_client(int client_fd, InferenceEngine &engine, SessionStore &store) {
    HttpRequest req;
    if (!read_http_request(client_fd, req)) {
        close(client_fd);
        return;
    }

    if (req.method == "GET" && req.path == "/health") {
        handle_health(client_fd);
    } else if (req.method == "POST" && req.path == "/api/chat") {
        handle_chat(client_fd, req, engine, store);
    } else if (req.method == "POST" && req.path == "/api/audio/analyze") {
        handle_audio(client_fd, req, engine, store);
    } else {
        write_response(client_fd, 404, "{\"error\":\"bulunamadı\"}");
    }
    close(client_fd);
}

class Server {
   public:
    explicit Server(int port) : port_(port) {}

    void run() {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::perror("socket");
            return;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            std::perror("bind");
            close(server_fd);
            return;
        }
        if (listen(server_fd, kBacklog) < 0) {
            std::perror("listen");
            close(server_fd);
            return;
        }

        std::cout << "[edac-api] http://0.0.0.0:" << port_ << " adresinde çalışıyor" << std::endl;

        while (running_) {
            sockaddr_in client_addr{};
            socklen_t len = sizeof(client_addr);
            int client_fd = accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &len);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                std::perror("accept");
                break;
            }
            std::thread(&Server::dispatch, this, client_fd).detach();
        }
        close(server_fd);
    }

    void stop() { running_ = false; }

   private:
    void dispatch(int client_fd) {
        handle_client(client_fd, engine_, store_);
    }

    int port_;
    bool running_ = true;
    InferenceEngine engine_;
    SessionStore store_;
};

}  // namespace

Server *g_server = nullptr;

void signal_handler(int) {
    if (g_server) g_server->stop();
}

int main() {
    Server server(kDefaultPort);
    g_server = &server;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    server.run();
    return 0;
}

