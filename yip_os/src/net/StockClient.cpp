#include "StockClient.hpp"
#include "core/Logger.hpp"
#define NOMINMAX
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace YipOS {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

StockClient::StockClient() {
    curl_ = curl_easy_init();
}

StockClient::~StockClient() {
    if (curl_) curl_easy_cleanup(curl_);
}

bool StockClient::IsCrypto(const std::string& symbol) {
    // Common crypto tickers
    static const char* crypto[] = {
        "BTC", "ETH", "DOGE", "SOL", "ADA", "XRP", "DOT", "AVAX",
        "SHIB", "MATIC", "LINK", "UNI", "LTC", "BCH", "ATOM",
        "NEAR", "FIL", "APE", "MANA", "SAND", "PEPE", "BONK",
    };
    for (const auto* c : crypto) {
        if (symbol == c) return true;
    }
    return false;
}

std::string StockClient::ToYahooSymbol(const std::string& symbol) const {
    if (IsCrypto(symbol)) return symbol + "-USD";
    return symbol;
}

std::string StockClient::RangeForWindow(const std::string& window) const {
    if (window == "1DY") return "1d";
    if (window == "1WK") return "5d";
    if (window == "1MO") return "1mo";
    if (window == "6MO") return "6mo";
    if (window == "1YR") return "1y";
    if (window == "5YR") return "5y";
    return "1mo";
}

std::string StockClient::IntervalForWindow(const std::string& window) const {
    if (window == "1DY") return "5m";
    if (window == "1WK") return "1h";
    if (window == "5YR") return "1wk";
    return "1d";
}

std::string StockClient::BuildURL(const std::string& symbol, const std::string& window) const {
    std::string yahoo_sym = ToYahooSymbol(symbol);
    std::string range = RangeForWindow(window);
    std::string interval = IntervalForWindow(window);
    return "https://query1.finance.yahoo.com/v8/finance/chart/" + yahoo_sym +
           "?range=" + range + "&interval=" + interval;
}

bool StockClient::FetchQuote(const std::string& symbol, const std::string& window) {
    if (!curl_) return false;

    std::string url = BuildURL(symbol, window);
    std::string response;

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl_, CURLOPT_USERAGENT, "YipOS/1.0");

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        Logger::Warning("StockClient fetch failed for " + symbol + ": " + curl_easy_strerror(res));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        Logger::Warning("StockClient HTTP " + std::to_string(http_code) + " for " + symbol);
        return false;
    }

    StockQuote quote;
    quote.symbol = symbol;
    quote.time_label = window;

    if (!ParseResponse(response, quote)) {
        Logger::Warning("StockClient JSON parse failed for " + symbol);
        return false;
    }

    quote.last_fetch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Store/update in quotes_
    auto it = symbol_index_.find(symbol);
    if (it != symbol_index_.end()) {
        quotes_[it->second] = std::move(quote);
    } else {
        symbol_index_[symbol] = quotes_.size();
        quotes_.push_back(std::move(quote));
    }

    Logger::Debug("StockClient fetched " + symbol + " (" + window + ")");
    return true;
}

bool StockClient::ParseResponse(const std::string& json_str, StockQuote& quote) {
    try {
        auto j = nlohmann::json::parse(json_str);

        auto& result = j["chart"]["result"][0];
        auto& meta = result["meta"];

        quote.current_price = meta["regularMarketPrice"].get<float>();
        if (meta.contains("chartPreviousClose")) {
            quote.prev_close = meta["chartPreviousClose"].get<float>();
        } else if (meta.contains("previousClose")) {
            quote.prev_close = meta["previousClose"].get<float>();
        } else {
            quote.prev_close = quote.current_price;
        }

        // Extract close prices
        auto& closes = result["indicators"]["quote"][0]["close"];
        quote.history.clear();
        for (auto& v : closes) {
            if (v.is_null()) {
                // Fill null with previous value or 0
                float prev = quote.history.empty() ? quote.current_price : quote.history.back();
                quote.history.push_back(prev);
            } else {
                quote.history.push_back(v.get<float>());
            }
        }

        // Downsample to GRAPH_WIDTH
        Downsample(quote.history, GRAPH_WIDTH);

        return true;
    } catch (const std::exception& e) {
        Logger::Warning("StockClient parse error: " + std::string(e.what()));
        return false;
    }
}

void StockClient::Downsample(std::vector<float>& data, int target_size) {
    if (static_cast<int>(data.size()) <= target_size) return;

    std::vector<float> result;
    result.reserve(target_size);
    float step = static_cast<float>(data.size() - 1) / (target_size - 1);
    for (int i = 0; i < target_size; i++) {
        int idx = static_cast<int>(std::round(i * step));
        idx = std::min(idx, static_cast<int>(data.size()) - 1);
        result.push_back(data[idx]);
    }
    data = std::move(result);
}

const StockQuote* StockClient::GetQuote(const std::string& symbol) const {
    auto it = symbol_index_.find(symbol);
    if (it != symbol_index_.end() && it->second < quotes_.size()) {
        return &quotes_[it->second];
    }
    return nullptr;
}

void StockClient::FetchAll(const std::string& window) {
    for (const auto& sym : symbols_) {
        FetchQuote(sym, window);
    }
}

} // namespace YipOS
