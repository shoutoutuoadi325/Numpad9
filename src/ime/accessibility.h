#pragma once
#include <windows.h>
#include <memory>
#include <vector>
#include "../core/engine.h"

namespace numpad9::ime {
inline constexpr UINT kCandidateActionMessage = WM_APP + 91;
class CandidateAccessibility {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    CandidateAccessibility();
    ~CandidateAccessibility();
    void Attach(HWND hwnd);
    void Update(const std::vector<Candidate>& candidates, size_t selected, UINT dpi, bool visible);
    void Hide();
    void Detach();
    LRESULT GetObject(WPARAM, LPARAM);
    bool IsCurrentGeneration(LPARAM generation) const;
};
}
