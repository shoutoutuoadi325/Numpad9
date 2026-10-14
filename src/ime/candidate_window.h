#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "accessibility.h"
#include "../core/engine.h"

namespace numpad9::ime {
class CandidateWindow {
    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    std::wstring title_;
    std::vector<Candidate> candidates_;
    size_t selected_ = 0;
    bool dictionaryReady_ = false;
    bool visible_ = false;
    CandidateAccessibility accessibility_;
    std::function<void(size_t, bool)> action_;
    int scale(int px) const { return MulDiv(px, static_cast<int>(dpi_), 96); }
    void Paint();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
public:
    void SetAction(std::function<void(size_t, bool)> action) { action_ = std::move(action); }
    ~CandidateWindow();
    void Update(const RECT& caret, HWND owner, Mode mode, const std::string& digits,
                const std::vector<Candidate>& candidates, size_t selected, bool dictionaryReady);
    void Hide();
    void Destroy();
};
}
