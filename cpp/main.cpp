#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <io.h>
#pragma comment(lib, "Ws2_32.lib")
using socklen_t = int;
#define CLOSESOCKET closesocket
#else
// POSIX networking headers required for socket-based server
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCKET close
#endif

#include <chrono>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
constexpr int kDefaultPort = 8080;
constexpr int kBacklog = 32;
constexpr size_t kBufferSize = 8192;

std::string now_iso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
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

struct SessionData {
    std::string summary;
    std::vector<std::string> timeline;
};

class SessionStore {
   public:
    std::string add_turn(const std::string &session_id, const std::string &user_input) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &session = sessions_[session_id];
        session.timeline.push_back(user_input);
        if (!session.summary.empty()) session.summary += " | ";
        session.summary += user_input.substr(0, 160);
        return session.summary;
    }

    SessionData get(const std::string &session_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return {};
        return it->second;
    }

    std::vector<std::string> recent(const std::string &session_id, size_t limit = 6) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return {};
        const auto &timeline = it->second.timeline;
        if (timeline.size() <= limit) return timeline;
        return std::vector<std::string>(timeline.end() - limit, timeline.end());
    }

    std::vector<std::string> session_ids() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> ids;
        ids.reserve(sessions_.size());
        for (const auto &kv : sessions_) ids.push_back(kv.first);
        return ids;
    }

   private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionData> sessions_;
};

struct Plan {
    std::string id;
    std::string name;
    std::string price;
    std::vector<std::string> features;
    bool popular = false;
};

struct Account {
    int id = 0;
    std::string name;
    std::string email;
    std::string plan_id;
    std::string created_at;
};

class AccountStore {
   public:
    Account create(const std::string &name, const std::string &email, const std::string &plan_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        Account acc;
        acc.id = ++counter_;
        acc.name = name;
        acc.email = email;
        acc.plan_id = plan_id.empty() ? "basic" : plan_id;
        acc.created_at = now_iso8601();
        accounts_[acc.id] = acc;
        return acc;
    }

    std::optional<Account> get(int id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = accounts_.find(id);
        if (it == accounts_.end()) return std::nullopt;
        return it->second;
    }

    bool update_plan(int id, const std::string &plan_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = accounts_.find(id);
        if (it == accounts_.end()) return false;
        it->second.plan_id = plan_id;
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return accounts_.size();
    }

   private:
    mutable std::mutex mutex_;
    int counter_ = 0;
    std::unordered_map<int, Account> accounts_;
};

class InferenceEngine {
   public:
    std::string detect_intent(const std::string &text) const {
        if (text.find("ses") != std::string::npos || text.find("audio") != std::string::npos) return "speech";
        if (text.find("yardım") != std::string::npos) return "support";
        if (text.find("araştır") != std::string::npos || text.find("ara") != std::string::npos ||
            text.find("web") != std::string::npos)
            return "search";
        if (is_math_expression(text)) return "math";
        if (text.find("hava") != std::string::npos) return "weather";
        if (text.find("kod") != std::string::npos || text.find("api") != std::string::npos) return "development";
        return "general";
    }

    std::string respond(const std::string &text, const std::string &mode) const {
        if (is_math_expression(text)) {
            const auto result = evaluate_expression(text);
            if (result.has_value()) {
                std::ostringstream math_oss;
                math_oss << "[math] " << text << " = " << *result;
                return math_oss.str();
            }
        }
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

    std::vector<std::string> suggest_tools(const std::string &intent) const {
        if (intent == "weather") return {"get_weather", "forecast_5day"};
        if (intent == "speech") return {"transcribe", "classify_audio"};
        if (intent == "development") return {"run_tests", "deploy_preview", "lint_source"};
        if (intent == "search") return {"web_search", "crawl"};
        if (intent == "math") return {"calculator"};
        return {"search_docs", "semantic_answer"};
    }

    std::string summarize_intent(const std::string &intent) const {
        if (intent == "weather") return "Anlık hava durumu ve tahmin";
        if (intent == "speech") return "Ses analizi ve komut çıkarımı";
        if (intent == "development") return "Kod, API ve hata ayıklama";
        if (intent == "support") return "Destek ve yönlendirme";
        if (intent == "math") return "Hızlı hesaplama";
        if (intent == "search") return "Web araştırma";
        return "Genel sohbet";
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

    struct Disease {
        std::string name;
        std::vector<std::string> symptoms;
        std::string risk;
        std::string guidance;
    };

    std::vector<Disease> catalog() const {
        return {
            {"Grip", {"ateş", "öksürük", "boğaz", "halsizlik"}, "orta", "Bol sıvı, istirahat, gerekirse hekim"},
            {"Migren", {"baş ağrısı", "ışık", "ses", "bulantı"}, "orta", "Karanlık ortam, tetikleyici kaçınma"},
            {"Tip-2 Diyabet", {"susuzluk", "sık idrara çıkma", "bulanık", "yorgunluk"}, "yüksek", "Kan şekeri ölçümü, doktor"},
            {"Hipertansiyon", {"baş dönmesi", "ense ağrısı", "nefes darlığı"}, "yüksek", "Kan basıncı takibi, kardiyoloji"},
            {"COVID-19", {"koku kaybı", "tat kaybı", "öksürük", "ateş"}, "yüksek", "Test, izolasyon, hekim"},
            {"Demir Eksikliği", {"yorgunluk", "solukluk", "nefes", "çarpıntı"}, "orta", "Kan tahlili, takviye için doktor"},
            {"Alerji", {"kaşıntı", "hapşırma", "göz sulanması", "döküntü"}, "düşük", "Antihistaminik danışın"},
            {"Astım", {"nefes", "hırıltı", "göğüs sıkışması", "öksürük"}, "yüksek", "İnhaler kullanımı, hekim takibi"},
            {"Gıda Zehirlenmesi", {"bulantı", "kusma", "ishal", "karın ağrısı"}, "orta", "Sıvı alımı, hekim"},
            {"Anksiyete", {"çarpıntı", "kaygı", "uyku", "terleme"}, "orta", "Nefes egzersizi, psikolojik destek"},
        };
    }

    std::vector<std::string> match_symptoms(const std::string &input) const {
        std::vector<std::string> matches;
        auto diseases = catalog();
        for (const auto &d : diseases) {
            int score = 0;
            for (const auto &sym : d.symptoms) {
                if (input.find(sym) != std::string::npos) score++;
            }
            if (score > 0) {
                std::ostringstream oss;
                oss << d.name << " (skor:" << score << ", risk:" << d.risk << ")";
                matches.push_back(oss.str());
            }
        }
        if (matches.empty()) matches.push_back("Bulgu eşleşmesi yok, klinik değerlendirme gerek");
        return matches;
    }

    bool is_math_expression(const std::string &text) const {
        bool has_digit = false;
        for (char c : text) {
            if (std::isdigit(static_cast<unsigned char>(c))) has_digit = true;
            if (c == '+' || c == '-' || c == '*' || c == '/') return has_digit;
        }
        return false;
    }

    std::optional<double> evaluate_expression(const std::string &text) const {
        double a = 0, b = 0;
        char op = 0;
        std::istringstream iss(text);
        if (!(iss >> a)) return std::nullopt;
        iss >> op;
        if (!(iss >> b)) return std::nullopt;
        switch (op) {
            case '+':
                return a + b;
            case '-':
                return a - b;
            case '*':
                return a * b;
            case '/':
                if (b == 0) return std::nullopt;
                return a / b;
            default:
                return std::nullopt;
        }
    }
};

void handle_health(int client_fd) {
    std::string body = "{\"status\":\"ok\",\"timestamp\":\"" + now_iso8601() + "\"}";
    write_response(client_fd, 200, body);
}

void handle_search(int client_fd, const HttpRequest &req) {
    const std::string query = find_json_value(req.body, "query");
    if (query.empty()) {
        write_response(client_fd, 400, "{\"error\":\"query zorunlu\"}");
        return;
    }
    std::vector<std::pair<std::string, std::string>> sources = {
        {"Docs", "https://example.com/docs"},
        {"Blog", "https://example.com/blog"},
        {"Research", "https://example.com/research"},
    };
    std::ostringstream oss;
    oss << "{\"query\":\"" << json_escape(query) << "\",\"results\":[";
    for (size_t i = 0; i < sources.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"title\":\"" << json_escape(sources[i].first + " sonuçları") << "\",";
        oss << "\"url\":\"" << json_escape(sources[i].second) << "\",";
        oss << "\"snippet\":\"" << json_escape("\"" + query + "\" terimi için yapay sonuç") << "\"}";
    }
    oss << "],\"freshness\":\"" << now_iso8601() << "\"}";
    write_response(client_fd, 200, oss.str());
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

    std::string summary = store.add_turn(session_id, text);
    const std::string intent = engine.detect_intent(text);
    const auto tools = engine.suggest_tools(intent);
    if (stream) {
        auto chunks = engine.streaming_chunks(engine.respond(text, mode.empty() ? "text" : mode));
        std::ostringstream oss;
        oss << "{\"session_id\":\"" << json_escape(session_id) << "\",";
        oss << "\"intent\":\"" << json_escape(intent) << "\",";
        oss << "\"intent_label\":\"" << json_escape(engine.summarize_intent(intent)) << "\",";
        oss << "\"streaming\":[";
        for (size_t i = 0; i < chunks.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << json_escape(chunks[i]) << "\"";
        }
        oss << "],\"summary\":\"" << json_escape(summary) << "\",";
        oss << "\"tools\":[";
        for (size_t i = 0; i < tools.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << json_escape(tools[i]) << "\"";
        }
        oss << "],\"turns\":[";
        auto timeline = store.recent(session_id);
        for (size_t i = 0; i < timeline.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << json_escape(timeline[i]) << "\"";
        }
        oss << "]}";
        write_response(client_fd, 200, oss.str());
    } else {
        std::ostringstream oss;
        oss << "{\"session_id\":\"" << json_escape(session_id) << "\",";
        oss << "\"intent\":\"" << json_escape(intent) << "\",";
        oss << "\"intent_label\":\"" << json_escape(engine.summarize_intent(intent)) << "\",";
        oss << "\"reply\":\"" << json_escape(engine.respond(text, mode.empty() ? "text" : mode)) << "\",";
        oss << "\"summary\":\"" << json_escape(summary) << "\",";
        oss << "\"tools\":[";
        for (size_t i = 0; i < tools.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << json_escape(tools[i]) << "\"";
        }
        oss << "],\"turns\":[";
        auto timeline = store.recent(session_id);
        for (size_t i = 0; i < timeline.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << json_escape(timeline[i]) << "\"";
        }
        oss << "]}";
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
    oss << "\"analysis\":\"" << json_escape(summary) << "\",";
    oss << "\"confidence\":0.62}";
    write_response(client_fd, 200, oss.str());
}

void handle_health_diagnose(int client_fd, const HttpRequest &req, InferenceEngine &engine) {
    const std::string symptoms = find_json_value(req.body, "symptoms");
    if (symptoms.empty()) {
        write_response(client_fd, 400, "{\"error\":\"symptoms zorunlu\"}");
        return;
    }
    const std::string duration = find_json_value(req.body, "duration_days");
    auto matches = engine.match_symptoms(symptoms);
    std::ostringstream oss;
    oss << "{\"layer\":\"health1.0\",\"symptoms\":\"" << json_escape(symptoms) << "\",";
    oss << "\"duration_days\":\"" << json_escape(duration) << "\",";
    oss << "\"predictions\":[";
    for (size_t i = 0; i < matches.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << json_escape(matches[i]) << "\"";
    }
    oss << "],\"note\":\"Klinik değerlendirme gereklidir; bu çıktı triyaj amaçlıdır.\"}";
    write_response(client_fd, 200, oss.str());
}

void handle_session_summary(int client_fd, const std::string &session_id, SessionStore &store) {
    if (session_id.empty()) {
        write_response(client_fd, 400, "{\"error\":\"session_id eksik\"}");
        return;
    }
    SessionData data = store.get(session_id);
    std::ostringstream oss;
    oss << "{\"session_id\":\"" << json_escape(session_id) << "\",";
    oss << "\"summary\":\"" << json_escape(data.summary) << "\",";
    oss << "\"turns\":[";
    for (size_t i = 0; i < data.timeline.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << json_escape(data.timeline[i]) << "\"";
    }
    oss << "],\"available_sessions\":[";
    auto ids = store.session_ids();
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << json_escape(ids[i]) << "\"";
    }
    oss << "]}";
    write_response(client_fd, 200, oss.str());
}

void handle_tools(int client_fd) {
    const std::string payload =
        "{\"tools\":[{\"name\":\"transcribe\",\"description\":\"Ses dönüştürme\"},{\"name\":\"classify_audio\",\"description\":\"Hızlı etiketleme\"},{\"name\":\"search_docs\",\"description\":\"Belgelerden yanıt bulur\"},{\"name\":\"run_tests\",\"description\":\"Kod sağlığını ölçer\"}],\"version\":\"2025.1\"}";
    write_response(client_fd, 200, payload);
}

std::vector<Plan> subscription_plans() {
    return {
        {"basic", "Basic", "$0", {"Chat modelleri", "Sınırlı context", "Ses erişimi"}, false},
        {"super", "SuperGrok", "$30.00", {"Grok 4.1 erişimi", "Uzun bağlam", "Öncelikli ses", "Tüm Basic"}, true},
        {"heavy", "SuperGrok Heavy", "$300.00", {"Grok 4 Heavy önizleme", "En uzun bağlam", "Tüm SuperGrok"}, false},
    };
}

void handle_subscriptions(int client_fd) {
    auto plans = subscription_plans();
    std::ostringstream oss;
    oss << "{\"plans\":[";
    for (size_t i = 0; i < plans.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"id\":\"" << plans[i].id << "\",";
        oss << "\"name\":\"" << json_escape(plans[i].name) << "\",";
        oss << "\"price\":\"" << plans[i].price << " USD/month\",";
        oss << "\"popular\":" << (plans[i].popular ? "true" : "false") << ",";
        oss << "\"features\":[";
        for (size_t j = 0; j < plans[i].features.size(); ++j) {
            if (j > 0) oss << ",";
            oss << "\"" << json_escape(plans[i].features[j]) << "\"";
        }
        oss << "]}";
    }
    oss << "]}";
    write_response(client_fd, 200, oss.str());
}

void handle_account_create(int client_fd, const HttpRequest &req, AccountStore &accounts) {
    const std::string name = find_json_value(req.body, "name");
    const std::string email = find_json_value(req.body, "email");
    const std::string plan = find_json_value(req.body, "plan_id");
    if (name.empty() || email.empty()) {
        write_response(client_fd, 400, "{\"error\":\"name ve email zorunlu\"}");
        return;
    }
    Account acc = accounts.create(name, email, plan);
    std::ostringstream oss;
    oss << "{\"account_id\":" << acc.id << ",";
    oss << "\"plan_id\":\"" << acc.plan_id << "\",";
    oss << "\"email\":\"" << json_escape(acc.email) << "\",";
    oss << "\"name\":\"" << json_escape(acc.name) << "\",";
    oss << "\"created_at\":\"" << acc.created_at << "\"}";
    write_response(client_fd, 200, oss.str());
}

void handle_subscription_select(int client_fd, const HttpRequest &req, AccountStore &accounts) {
    const std::string plan_id = find_json_value(req.body, "plan_id");
    const std::string account_id_val = find_json_value(req.body, "account_id");
    if (plan_id.empty() || account_id_val.empty()) {
        write_response(client_fd, 400, "{\"error\":\"plan_id ve account_id zorunlu\"}");
        return;
    }
    int account_id = std::stoi(account_id_val);
    if (!accounts.update_plan(account_id, plan_id)) {
        write_response(client_fd, 404, "{\"error\":\"hesap bulunamadı\"}");
        return;
    }
    auto acc = accounts.get(account_id);
    std::ostringstream oss;
    oss << "{\"account_id\":" << account_id << ",";
    oss << "\"plan_id\":\"" << plan_id << "\",";
    oss << "\"updated_at\":\"" << now_iso8601() << "\"}";
    write_response(client_fd, 200, oss.str());
}

void handle_diagnostics(int client_fd, SessionStore &store, AccountStore &accounts) {
    std::ostringstream oss;
    oss << "{\"status\":\"ok\",\"uptime_hint\":\"hafif\",";
    oss << "\"sessions\":" << store.session_ids().size() << ",";
    oss << "\"endpoints\":[\"/api/chat\",\"/api/audio/analyze\",\"/api/session/{id}\",\"/api/tools\",\"/api/subscriptions\"],";
    oss << "\"accounts\":" << accounts.size() << ",";
    oss << "\"timestamp\":\"" << now_iso8601() << "\"}";
    write_response(client_fd, 200, oss.str());
}

void handle_client(int client_fd, InferenceEngine &engine, SessionStore &store, AccountStore &accounts) {
    HttpRequest req;
    if (!read_http_request(client_fd, req)) {
        CLOSESOCKET(client_fd);
        return;
    }

    if (req.method == "GET" && req.path == "/health") {
        handle_health(client_fd);
    } else if (req.method == "GET" && req.path == "/api/tools") {
        handle_tools(client_fd);
    } else if (req.method == "GET" && req.path.rfind("/api/session/", 0) == 0) {
        handle_session_summary(client_fd, req.path.substr(std::string("/api/session/").size()), store);
    } else if (req.method == "GET" && req.path == "/api/diagnostics") {
        handle_diagnostics(client_fd, store, accounts);
    } else if (req.method == "GET" && req.path == "/api/subscriptions") {
        handle_subscriptions(client_fd);
    } else if (req.method == "POST" && req.path == "/api/account/create") {
        handle_account_create(client_fd, req, accounts);
    } else if (req.method == "POST" && req.path == "/api/subscriptions/select") {
        handle_subscription_select(client_fd, req, accounts);
    } else if (req.method == "POST" && req.path == "/api/chat") {
        handle_chat(client_fd, req, engine, store);
    } else if (req.method == "POST" && req.path == "/api/search") {
        handle_search(client_fd, req);
    } else if (req.method == "POST" && req.path == "/api/audio/analyze") {
        handle_audio(client_fd, req, engine, store);
    } else if (req.method == "POST" && req.path == "/api/health/diagnose") {
        handle_health_diagnose(client_fd, req, engine);
    } else {
        write_response(client_fd, 404, "{\"error\":\"bulunamadı\"}");
    }
    CLOSESOCKET(client_fd);
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
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            std::perror("bind");
            CLOSESOCKET(server_fd);
            return;
        }
        if (listen(server_fd, kBacklog) < 0) {
            std::perror("listen");
            CLOSESOCKET(server_fd);
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
        CLOSESOCKET(server_fd);
    }

    void stop() { running_ = false; }

   private:
    void dispatch(int client_fd) {
        handle_client(client_fd, engine_, store_, accounts_);
    }

    int port_;
    bool running_ = true;
    InferenceEngine engine_;
    SessionStore store_;
    AccountStore accounts_;
};

}  // namespace

Server *g_server = nullptr;

void signal_handler(int) {
    if (g_server) g_server->stop();
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup başarısız" << std::endl;
        return 1;
    }
#endif
    Server server(kDefaultPort);
    g_server = &server;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    server.run();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

