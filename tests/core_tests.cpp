#include "core/engine.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using numpad9::Engine;
using numpad9::Mode;

#define CHECK(value) do { if (!(value)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #value); } while (false)

namespace {
struct Fixture {
    std::filesystem::path path;
    Fixture() {
        path = std::filesystem::temp_directory_path() /
            ("numpad9-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tsv");
    }
    ~Fixture() { std::error_code ec; std::filesystem::remove(path, ec); }
    void Write(const std::string& content) const {
        std::ofstream file(path, std::ios::binary);
        file << content;
        CHECK(file.good());
    }
};

void Enter(Engine& engine, const std::string& digits) {
    engine.Clear();
    for (char digit : digits) CHECK(engine.AppendDigit(digit));
}

void TestMappingAndEncoding() {
    constexpr char expected[] = {'0','7','8','9','4','5','6','1','2','3'};
    for (unsigned i = 0; i < 10; ++i) CHECK(Engine::PhysicalToLogical(0x60 + i) == expected[i]);
    CHECK(Engine::PhysicalToLogical(0x6E) == '.');
    const unsigned nav[] = {0x24,0x26,0x21,0x25,0x0C,0x27,0x23,0x28,0x22};
    const unsigned scan[] = {0x47,0x48,0x49,0x4B,0x4C,0x4D,0x4F,0x50,0x51};
    for (unsigned i = 0; i < 9; ++i) {
        CHECK(Engine::PhysicalToLogical(nav[i], scan[i]) == static_cast<char>('1' + i));
        CHECK(Engine::PhysicalToLogical(nav[i], scan[i] | 0x100) == 0);
        CHECK(Engine::PhysicalToLogical(nav[i]) == 0);
    }
    CHECK(Engine::PhysicalToLogical(0x2D, 0x52) == '0');
    CHECK(Engine::PhysicalToLogical(0x2E, 0x53) == '.');
    CHECK(Engine::PhysicalToLogical(0x41, 0x47) == 0);
    CHECK(Engine::Encode("abcdefghijklmnopqrstuvwxyz") == "22233344455566677778889999");
    CHECK(Engine::Encode("Ni Hao") == "64426");
    CHECK(Engine::Encode("xi'an") == "9426");
    CHECK(Engine::Encode("nv") == "68");
    CHECK(Engine::Encode("hello-world") == "4355696753");
    CHECK(Engine::Encode("ni3").empty());
    CHECK(Engine::Encode(std::string(65, 'a')).empty());
}

void TestLookupAndState() {
    Fixture fixture;
    fixture.Write("\xEF\xBB\xBF# fixture\r\nni\t你\t10\r\nnihao\t你好\t999\n"
                  "mi\t米\t9\nni\t你\t50\nni\t泥\t40\nhello\thello\t10\n"
                  "gekko\tgekko\t5\nhello\thello\t1\nher\ther\t20\n"
                  "zhong\t\xF0\xA0\x80\x80\t20\n");
    Engine engine;
    CHECK(engine.LoadDictionary(fixture.path.u8string()));
    CHECK(engine.GetMode() == Mode::Pinyin);
    CHECK(engine.Candidates().empty());
    CHECK(!engine.AppendDigit('0'));
    CHECK(!engine.AppendDigit('1'));
    CHECK(!engine.AppendDigit('a'));
    Enter(engine, "64");
    auto candidates = engine.Candidates();
    CHECK(candidates.size() == 4);
    CHECK(candidates[0].text == L"你");
    CHECK(candidates[1].text == L"泥");
    CHECK(candidates[2].text == L"米");
    CHECK(candidates[3].text == L"你好"); // exact readings precede completions
    CHECK(engine.Candidates(1).size() == 1);
    CHECK(engine.Candidates(0).empty());
    engine.Backspace();
    CHECK(engine.Digits() == "6");
    CHECK(engine.AppendDigit('4'));
    engine.SetMode(Mode::Pinyin);
    CHECK(engine.Digits() == "64");
    engine.SetMode(Mode::English);
    CHECK(engine.Digits().empty());
    Enter(engine, "43556");
    candidates = engine.Candidates();
    CHECK(candidates.size() == 2);
    CHECK(candidates.front().text == L"hello");
    CHECK(candidates[1].text == L"gekko");
    Enter(engine, "437");
    CHECK(engine.Candidates().front().text == L"her");
    engine.SetMode(Mode::Pinyin);
    Enter(engine, "94664");
    candidates = engine.Candidates();
    CHECK(candidates.size() == 1);
    if (sizeof(wchar_t) == 2) {
        CHECK(candidates[0].text.size() == 2);
        CHECK(static_cast<unsigned>(candidates[0].text[0]) == 0xD840);
    } else {
        CHECK(candidates[0].text.size() == 1);
        CHECK(static_cast<unsigned>(candidates[0].text[0]) == 0x20000);
    }
    Enter(engine, std::string(64, '9'));
    CHECK(!engine.AppendDigit('9'));
    CHECK(engine.Digits().size() == 64);
    engine.Clear(); engine.Backspace();
    CHECK(engine.Digits().empty());
}

void TestInvalidDictionaryIsAtomic() {
    Fixture fixture;
    fixture.Write("ni\t你\t10\n");
    Engine engine;
    CHECK(engine.LoadDictionary(fixture.path.u8string()));
    Enter(engine, "64");
    const std::string invalid[] = {
        "", "# comments only\n", "ni\t你\n", "ni\t你\t-1\n", "ni\t你\t4294967296\n",
        "ni\t你\t10junk\n", "ni\t你\t1\textra\n", "n1\t你\t1\n", "NI\t你\t1\n",
        "ni\t\xC0\xAF\t1\n", "ni\t\xED\xA0\x80\t1\n", "ni\t\xF4\x90\x80\x80\t1\n",
        "ni\t\xE4\xBD\t1\n", "ni\t\x01\t1\n", "ni\t\t1\n",
        std::string(65, 'a') + "\t你\t1\n", "ni\t" + std::string(65, 'a') + "\t1\n",
        "ni\t你\t10\ninvalid\n"
    };
    for (const auto& content : invalid) {
        fixture.Write(content);
        CHECK(!engine.LoadDictionary(fixture.path.u8string()));
        CHECK(engine.Digits() == "64");
        CHECK(engine.Candidates().size() == 1);
        CHECK(engine.Candidates().front().text == L"你");
    }
    CHECK(!engine.LoadDictionary(fixture.path.u8string() + ".missing"));
    // Reject large files before reading or allocating their contents.
    {
        std::ofstream oversized(fixture.path, std::ios::binary);
        oversized.seekp(64 * 1024 * 1024);
        oversized.put('\n');
    }
    CHECK(!engine.LoadDictionary(fixture.path.u8string()));
    CHECK(engine.Candidates().front().text == L"你");
}

void TestProductionDictionary(const std::string& path) {
    Engine engine;
    const auto loading = std::chrono::steady_clock::now();
    CHECK(engine.LoadDictionary(path));
    const auto loaded = std::chrono::steady_clock::now();
    const std::pair<const char*, const wchar_t*> examples[] = {
        {"ni",L"你"}, {"nihao",L"你好"}, {"zhongguo",L"中国"}, {"xiexie",L"谢谢"},
        {"shurufa",L"输入法"}, {"xiaojianpan",L"小键盘"}, {"zaijian",L"再见"},
        {"nv",L"女"}, {"lv",L"绿"}, {"chongqing",L"重庆"}
    };
    for (const auto& example : examples) {
        Enter(engine, Engine::Encode(example.first));
        CHECK(engine.Candidates().front().text == example.second);
    }
    engine.SetMode(Mode::English);
    Enter(engine, Engine::Encode("hello"));
    CHECK(engine.Candidates().front().text == L"hello");
    Enter(engine, "2");
    CHECK(engine.Candidates(1000).size() == 100);
    // Benchmark broad prefixes without a timing assertion that could fail on
    // slow CI hosts. Repeated queries must return the same sorted candidates.
    engine.SetMode(Mode::Pinyin);
    Enter(engine, "7");
    const auto baseline = engine.Candidates();
    CHECK(baseline.size() == 60);
    const auto start = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < 100; ++repeat) {
        const auto candidates = engine.Candidates();
        CHECK(candidates.size() == baseline.size());
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            CHECK(candidates[i].text == baseline[i].text);
            CHECK(candidates[i].spelling == baseline[i].spelling);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    std::cout << "Production load: " << std::chrono::duration<double, std::milli>(loaded - loading).count()
              << " ms; broad-prefix query mean: "
              << std::chrono::duration<double, std::milli>(end - start).count() / 100 << " ms\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        TestMappingAndEncoding();
        TestLookupAndState();
        TestInvalidDictionaryIsAtomic();
        TestProductionDictionary(argc > 1 ? argv[1] : "data/lexicon.tsv");
        std::cout << "All core tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
