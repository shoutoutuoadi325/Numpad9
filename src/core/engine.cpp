#include "engine.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

namespace numpad9 {
namespace {
constexpr std::size_t kMaxDigits = 64;
constexpr std::size_t kMaxCandidates = 100;
constexpr std::size_t kMaxEntries = 500000;
constexpr std::uintmax_t kMaxDictionaryBytes = 64 * 1024 * 1024;

bool DecodeUtf8(const std::string& input, std::wstring& output, bool& ascii) {
    ascii = true;
    std::size_t characters = 0;
    for (std::size_t i = 0; i < input.size();) {
        const auto first = static_cast<unsigned char>(input[i++]);
        std::uint32_t codepoint = first;
        unsigned int following = 0;
        if (first >= 0xC2 && first <= 0xDF) { codepoint = first & 0x1F; following = 1; }
        else if (first >= 0xE0 && first <= 0xEF) { codepoint = first & 0x0F; following = 2; }
        else if (first >= 0xF0 && first <= 0xF4) { codepoint = first & 0x07; following = 3; }
        else if (first >= 0x80) { return false; }
        if (i + following > input.size()) return false;
        for (unsigned int n = 0; n < following; ++n) {
            const auto next = static_cast<unsigned char>(input[i++]);
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if ((following == 1 && codepoint < 0x80) ||
            (following == 2 && codepoint < 0x800) ||
            (following == 3 && codepoint < 0x10000) ||
            codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F) ||
            codepoint == 0x2028 || codepoint == 0x2029 || ++characters > kMaxDigits) return false;
        ascii = ascii && codepoint < 0x80;
        if (sizeof(wchar_t) == 2 && codepoint > 0xFFFF) {
            codepoint -= 0x10000;
            output.push_back(static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
            output.push_back(static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
        } else {
            output.push_back(static_cast<wchar_t>(codepoint));
        }
    }
    return !output.empty();
}
} // namespace

Engine::Engine() = default;

bool Engine::LoadDictionary(const std::string& path) {
    const auto filePath = std::filesystem::u8path(path);
    std::error_code error;
    const auto bytes = std::filesystem::file_size(filePath, error);
    if (error || bytes == 0 || bytes > kMaxDictionaryBytes) return false;
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream) return false;
    std::vector<Entry> pinyin, english;
    std::string line;
    std::size_t lines = 0;
    while (std::getline(stream, line)) {
        if (++lines > kMaxEntries + 1000 || line.size() > 4096) return false;
        if (lines == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? first : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
            line.find('\t', second + 1) != std::string::npos) return false;
        Entry entry;
        entry.candidate.spelling = line.substr(0, first);
        if (entry.candidate.spelling.empty() || entry.candidate.spelling.size() > kMaxDigits ||
            !std::all_of(entry.candidate.spelling.begin(), entry.candidate.spelling.end(),
                         [](char c) { return c >= 'a' && c <= 'z'; })) return false;
        entry.digits = Encode(entry.candidate.spelling);
        bool ascii = false;
        if (!DecodeUtf8(line.substr(first + 1, second - first - 1), entry.candidate.text, ascii)) return false;
        const char* rankStart = line.data() + second + 1;
        const char* end = line.data() + line.size();
        const auto result = std::from_chars(rankStart, end, entry.rank);
        if (result.ec != std::errc{} || result.ptr != end) return false;
        if (pinyin.size() + english.size() >= kMaxEntries) return false;
        (ascii ? english : pinyin).push_back(std::move(entry));
    }
    if (stream.bad() || (pinyin.empty() && english.empty())) return false;
    const auto byEncoding = [](const Entry& a, const Entry& b) {
        if (a.digits != b.digits) return a.digits < b.digits;
        if (a.rank != b.rank) return a.rank > b.rank;
        if (a.candidate.spelling != b.candidate.spelling) return a.candidate.spelling < b.candidate.spelling;
        return a.candidate.text < b.candidate.text;
    };
    std::sort(pinyin.begin(), pinyin.end(), byEncoding);
    std::sort(english.begin(), english.end(), byEncoding);
    pinyin_.swap(pinyin);
    english_.swap(english);
    Clear();
    return true;
}

void Engine::SetMode(Mode mode) { if (mode_ != mode) { mode_ = mode; Clear(); } }
Mode Engine::GetMode() const { return mode_; }
bool Engine::AppendDigit(char digit) {
    if (digit < '2' || digit > '9' || digits_.size() >= kMaxDigits) return false;
    digits_.push_back(digit);
    return true;
}
void Engine::Backspace() { if (!digits_.empty()) digits_.pop_back(); }
void Engine::Clear() { digits_.clear(); }
const std::string& Engine::Digits() const { return digits_; }

std::vector<Candidate> Engine::Candidates(std::size_t limit) const {
    std::vector<Candidate> output;
    if (digits_.empty() || limit == 0) return output;
    limit = std::min(limit, kMaxCandidates);
    const auto& dictionary = mode_ == Mode::Pinyin ? pinyin_ : english_;
    const auto begin = std::lower_bound(dictionary.begin(), dictionary.end(), digits_,
        [](const Entry& entry, const std::string& value) { return entry.digits < value; });
    std::string upper = digits_;
    upper.back() += 1;
    const auto end = std::lower_bound(begin, dictionary.end(), upper,
        [](const Entry& entry, const std::string& value) { return entry.digits < value; });
    const auto better = [this](const Entry* a, const Entry* b) {
        const bool aExact = a->digits.size() == digits_.size();
        const bool bExact = b->digits.size() == digits_.size();
        if (aExact != bExact) return aExact;
        if (a->rank != b->rank) return a->rank > b->rank;
        if (a->candidate.spelling != b->candidate.spelling) return a->candidate.spelling < b->candidate.spelling;
        return a->candidate.text < b->candidate.text;
    };
    // Retain only the requested best records; duplicate pronunciations never
    // cause repeated output text. Query allocations are capped at 100 records.
    std::vector<const Entry*> best;
    best.reserve(limit);
    for (auto it = begin; it != end; ++it) {
        const auto* entry = &*it;
        auto duplicate = std::find_if(best.begin(), best.end(), [entry](const Entry* other) {
            return entry->candidate.text == other->candidate.text;
        });
        if (duplicate != best.end()) {
            if (!better(entry, *duplicate)) continue;
            best.erase(duplicate);
        }
        if (best.size() == limit && !better(entry, best.back())) continue;
        best.insert(std::lower_bound(best.begin(), best.end(), entry, better), entry);
        if (best.size() > limit) best.pop_back();
    }
    output.reserve(best.size());
    for (const auto* entry : best) output.push_back(entry->candidate);
    return output;
}

char Engine::PhysicalToLogical(unsigned int vk, unsigned int scanCode) {
    static constexpr char mapping[] = {'0', '7', '8', '9', '4', '5', '6', '1', '2', '3'};
    if (vk >= 0x60 && vk <= 0x69) return mapping[vk - 0x60];
    if (vk == 0x6E) return '.';
    if ((scanCode & ~0xFFu) != 0) return '\0';
    switch (vk) {
    case 0x24: return scanCode == 0x47 ? '1' : '\0'; // Home
    case 0x26: return scanCode == 0x48 ? '2' : '\0'; // Up
    case 0x21: return scanCode == 0x49 ? '3' : '\0'; // Page Up
    case 0x25: return scanCode == 0x4B ? '4' : '\0'; // Left
    case 0x0C: return scanCode == 0x4C ? '5' : '\0'; // Clear
    case 0x27: return scanCode == 0x4D ? '6' : '\0'; // Right
    case 0x23: return scanCode == 0x4F ? '7' : '\0'; // End
    case 0x28: return scanCode == 0x50 ? '8' : '\0'; // Down
    case 0x22: return scanCode == 0x51 ? '9' : '\0'; // Page Down
    case 0x2D: return scanCode == 0x52 ? '0' : '\0'; // Insert
    case 0x2E: return scanCode == 0x53 ? '.' : '\0'; // Delete
    default: return '\0';
    }
}

std::string Engine::Encode(const std::string& spelling) {
    static constexpr char keypad[] = "22233344455566677778889999";
    std::string encoded;
    for (char c : spelling) {
        if (c == ' ' || c == '\'' || c == '-') continue;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c < 'a' || c > 'z' || encoded.size() >= kMaxDigits) return {};
        encoded.push_back(keypad[c - 'a']);
    }
    return encoded;
}

} // namespace numpad9
