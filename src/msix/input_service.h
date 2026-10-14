#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "engine.h"

namespace numpad9::msix {

// featureId/token/attestation come from Microsoft's grant for this package.
// inputProfile is a separate Windows input-profile binding; it must match the
// actual platform configuration, rather than an invented profile identifier.
struct Config {
    std::wstring featureId;
    std::wstring token;
    std::wstring attestation;
    std::wstring inputProfile;
};

enum class State { Stopped, UnsupportedWindows, NotConfigured, Denied,
    WaitingForActivation, Ready, Failed };

struct Snapshot {
    State state = State::Stopped;
    std::wstring message;
    HRESULT error = S_OK;
    bool active = false;
    bool hasFocus = false;
    Mode mode = Mode::Pinyin;
    std::string digits;
    std::vector<Candidate> candidates;
    std::size_t selected = 0;
    RECT anchor{}; // Screen coordinates, supplied by the system input service.
    bool anchorValid = false; // If false, host chooses a monitor-work-area fallback.
    unsigned long long revision = 0; // Reject stale popup invocations.
};

// Construct/start/stop/select on the host's initialized WinRT STA thread.
// Keep that thread's message loop running. The host displays candidates in a
// nonactivating window and revokes its callback before destroying that window.
class InputService {
public:
    using Changed = std::function<void(const Snapshot&)>;
    explicit InputService(Changed changed = {});
    ~InputService();
    InputService(const InputService&) = delete;
    InputService& operator=(const InputService&) = delete;
    bool Start(const Config& config, const std::string& dictionaryPathUtf8);
    void Stop() noexcept;
    Snapshot GetSnapshot() const;
    bool SelectCandidate(std::size_t index, unsigned long long revision, bool commit = true);
    bool ToggleMode();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
