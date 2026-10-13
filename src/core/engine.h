#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace numpad9 {

enum class Mode { Pinyin, English };

struct Candidate {
    std::wstring text;
    std::string spelling;
};

// Offline predictive lookup. No input history, filesystem writes or networking.
class Engine {
public:
    Engine();
    // Atomically replaces the dictionary. On failure the previous one is retained.
    // UTF-8 TSV: normalized spelling<TAB>output text<TAB>rank (higher is better).
    bool LoadDictionary(const std::string& path);
    void SetMode(Mode mode);
    Mode GetMode() const;
    bool AppendDigit(char logicalDigit);
    void Backspace();
    void Clear();
    const std::string& Digits() const;
    std::vector<Candidate> Candidates(std::size_t limit = 60) const;
    // Pass scanCode | 0x100 for an extended key. This distinguishes a dedicated
    // navigation key from the NumLock-off physical keypad key.
    static char PhysicalToLogical(unsigned int vk, unsigned int scanCode = 0);
    // ASCII spelling, case insensitive; spaces, apostrophes and hyphens ignored.
    // Pinyin ü is written v. Unsupported characters return an empty encoding.
    static std::string Encode(const std::string& spelling);

private:
    struct Entry {
        Candidate candidate;
        std::string digits;
        std::uint32_t rank;
    };
    Mode mode_ = Mode::Pinyin;
    std::string digits_;
    std::vector<Entry> pinyin_;
    std::vector<Entry> english_;
};

} // namespace numpad9
