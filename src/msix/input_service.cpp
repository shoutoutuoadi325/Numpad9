#include "input_service.h"
#include <algorithm>
#include <appmodel.h>
#include <cmath>
#include <mutex>

#if __has_include(<winrt/Windows.UI.Input.Preview.Text.h>)
#define NUMPAD9_HAS_PREVIEW_TEXT 1
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.Preview.Text.h>
#include <winrt/Windows.UI.Text.Core.h>
#endif

namespace numpad9::msix {
#ifdef NUMPAD9_HAS_PREVIEW_TEXT
namespace text = winrt::Windows::UI::Input::Preview::Text;
using winrt::Windows::System::VirtualKey;
using winrt::Windows::UI::Text::Core::CoreTextRange;
#endif

struct InputService::Impl : std::enable_shared_from_this<Impl> {
    explicit Impl(Changed handler) : changed(std::move(handler)), thread(GetCurrentThreadId()) {}
    Changed changed;
    DWORD thread;
    Engine engine;
    Snapshot snapshot;
    mutable std::recursive_mutex mutex;
    std::wstring original;
    std::uint32_t textBox = 0;
    bool busy = false;
    bool running = false;
#ifdef NUMPAD9_HAS_PREVIEW_TEXT
    text::TextInputService service{nullptr};
    text::KeyboardInputProcessor processor{nullptr};
    text::KeyboardInputProcessor::Activated_revoker activated;
    text::KeyboardInputProcessor::Deactivated_revoker deactivated;
    text::KeyboardInputProcessor::FocusEntered_revoker entered;
    text::KeyboardInputProcessor::FocusRemoved_revoker removed;
    text::KeyboardInputProcessor::CompositionTerminated_revoker terminated;
    text::KeyboardInputProcessor::TextBoxInfoChanged_revoker infoChanged;
    text::KeyboardInputProcessor::TextBoxContentChanged_revoker contentChanged;
    text::KeyboardInputProcessor::ConversionModeChanged_revoker modeChanged;
    text::KeyboardInputProcessor::KeyEventReceived_revoker keyReceived;
#endif
    bool OnThread() const noexcept { return GetCurrentThreadId() == thread; }
    void Clear() {
        engine.Clear(); original.clear(); textBox = 0;
        snapshot.digits.clear(); snapshot.candidates.clear(); snapshot.selected = 0;
    }
    void Publish() noexcept {
        // UI callbacks cannot propagate exceptions into the system input server.
        try {
            snapshot.mode = engine.GetMode(); snapshot.digits = engine.Digits();
            ++snapshot.revision;
            if (changed) changed(snapshot);
        } catch (...) {}
    }
    void Fail(State state, const wchar_t* message, HRESULT error = S_OK) {
        running = false;
        Clear(); snapshot.state = state; snapshot.message = message;
        snapshot.error = error; snapshot.active = snapshot.hasFocus = false; Publish();
    }
    void RestoreDigits(const std::string& digits) {
        engine.Clear(); for (char digit : digits) engine.AppendDigit(digit);
        snapshot.candidates = engine.Candidates();
        if (snapshot.selected >= snapshot.candidates.size()) snapshot.selected = 0;
    }
#ifdef NUMPAD9_HAS_PREVIEW_TEXT
    bool Allowed() noexcept {
        try {
            if (!processor || !processor.IsActive() || !processor.HasFocusedTextBox()) return false;
            const auto info = processor.FocusedTextBoxInfo();
            if (!info) return false;
            if ((static_cast<std::uint32_t>(info.Settings()) &
                static_cast<std::uint32_t>(text::TextBoxSettings::Private)) != 0) return false;
            const auto denied = static_cast<std::uint32_t>(info.DisabledFeatures());
            if ((denied & static_cast<std::uint32_t>(text::TextBoxFeatures::ReadText)) != 0 ||
                (denied & static_cast<std::uint32_t>(text::TextBoxFeatures::WriteText)) != 0) return false;
            using Scope = winrt::Windows::UI::Text::Core::CoreTextInputScope;
            const auto scope = info.InputScope();
            return scope != Scope::Password && scope != Scope::Private &&
                scope != Scope::PasswordNumeric && scope != Scope::PinNumeric &&
                scope != Scope::PinAlphanumeric && static_cast<int>(scope) != 66; // Win32 PIN creation scope.
        } catch (...) { return false; }
    }
    void Refresh() noexcept {
        try {
            snapshot.active = processor && processor.IsActive();
            snapshot.hasFocus = Allowed();
            snapshot.state = snapshot.active ? State::Ready : State::WaitingForActivation;
            snapshot.message = snapshot.active ? L"Windows 输入服务已激活。" :
                L"等待 Windows 激活已批准的输入配置。";
            if (!snapshot.hasFocus) Clear();
            if (snapshot.hasFocus) {
                const auto bounds = processor.SelectionBounds();
                if (std::isfinite(bounds.X) && std::isfinite(bounds.Y) &&
                    std::isfinite(bounds.Width) && std::isfinite(bounds.Height) &&
                    std::abs(bounds.X) < 1000000 && std::abs(bounds.Y) < 1000000 &&
                    bounds.Width >= 0 && bounds.Width < 1000000 && bounds.Height >= 0 && bounds.Height < 1000000) {
                    snapshot.anchor = {static_cast<LONG>(bounds.X), static_cast<LONG>(bounds.Y),
                        static_cast<LONG>(bounds.X + bounds.Width), static_cast<LONG>(bounds.Y + bounds.Height)};
                }
            }
            Publish();
        } catch (const winrt::hresult_error& error) {
            Fail(State::Failed, L"Windows 输入服务状态不可用。", error.code());
        } catch (...) { Fail(State::Failed, L"无法读取输入服务状态。", E_FAIL); }
    }
    bool OwnComposition(const text::TextEditSession& session, text::TextComposition& composition,
                        text::TextCompositionSegment& segment) {
        if (engine.Digits().empty() || session.TextBoxId().Value != textBox) return false;
        composition = session.Composition();
        if (!composition || std::wstring(composition.Text()) !=
            std::wstring(engine.Digits().begin(), engine.Digits().end())) return false;
        segment = composition.FirstSegment();
        return static_cast<bool>(segment) && !segment.Next();
    }
    bool BeginOrUpdate(const text::TextEditSession& session, const std::string& previousDigits) {
        text::TextComposition composition{nullptr};
        text::TextCompositionSegment segment{nullptr};
        const std::wstring digits(engine.Digits().begin(), engine.Digits().end());
        if (previousDigits.empty()) {
            if (session.Composition()) return false; // Do not adopt another input method's composition.
            const auto range = session.GetSelectedRange();
            if (range.StartCaretPosition < 0 || range.EndCaretPosition < range.StartCaretPosition ||
                range.EndCaretPosition - range.StartCaretPosition > 65536) return false;
            std::wstring selected;
            if (range.StartCaretPosition != range.EndCaretPosition) {
                const auto populated = session.PopulatedRange();
                if (populated.StartCaretPosition > range.StartCaretPosition ||
                    populated.EndCaretPosition < range.EndCaretPosition) return false;
                selected = session.GetText(range);
            }
            const auto collapsed = session.ReplaceText(range, L"");
            session.SetSelectedRange({collapsed.StartCaretPosition, collapsed.StartCaretPosition});
            composition = session.StartComposition();
            if (!composition) return false;
            segment = composition.InsertText(digits);
            original = std::move(selected); textBox = session.TextBoxId().Value;
        } else {
            // Validate against the previously delivered preedit, not the engine
            // digits just changed by this transaction.
            if (session.TextBoxId().Value != textBox) return false;
            composition = session.Composition();
            if (!composition || std::wstring(composition.Text()) !=
                std::wstring(previousDigits.begin(), previousDigits.end())) return false;
            segment = composition.FirstSegment();
            if (!segment || segment.Next()) return false;
            segment.Text(digits);
        }
        composition.CaretPosition(static_cast<std::uint32_t>(digits.size()));
        return session.SubmitPayload();
    }
    bool Finish(const text::TextEditSession& session, bool cancel, const std::wstring& suffix = L"") {
        if (engine.Digits().empty()) {
            if (suffix.empty()) return true;
            const auto selection = session.GetSelectedRange();
            const auto replaced = session.ReplaceText(selection, suffix);
            session.SetSelectedRange({replaced.EndCaretPosition, replaced.EndCaretPosition});
            return session.SubmitPayload();
        }
        text::TextComposition composition{nullptr};
        text::TextCompositionSegment segment{nullptr};
        if (!OwnComposition(session, composition, segment)) { Clear(); return false; }
        std::wstring output;
        if (cancel) output = original;
        else if (snapshot.selected < snapshot.candidates.size()) output = snapshot.candidates[snapshot.selected].text;
        else output.assign(engine.Digits().begin(), engine.Digits().end());
        output += suffix;
        segment.Text(output);
        const auto range = segment.Range();
        composition.Complete();
        if (cancel) session.SetSelectedRange(range);
        else session.SetSelectedRange({range.EndCaretPosition, range.EndCaretPosition});
        if (!session.SubmitPayload()) return false;
        Clear(); return true;
    }
    bool Toggle(const text::TextEditSession& session = nullptr) {
        if (!engine.Digits().empty() && (!session || !Finish(session, false))) return false;
        const Mode next = engine.GetMode() == Mode::Pinyin ? Mode::English : Mode::Pinyin;
        // Updating the platform's conversion mode also exposes idle 中/A state.
        processor.ConversionMode(next == Mode::English ? text::TextConversionMode::AlphanumericHalfWidth :
            text::TextConversionMode::NativeHalfWidth);
        engine.SetMode(next); Publish(); return true;
    }
    void Key(const text::KeyEventReceivedEventArgs& args) noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (!OnThread() || !running || busy || !Allowed()) return;
        struct Busy { bool& value; explicit Busy(bool& v) : value(v) { value = true; } ~Busy() { value = false; } } guard(busy);
        try {
            const auto status = args.KeyStatus();
            if (status.IsKeyReleased) return;
            const auto key = args.VirtualKey();
            const auto vk = static_cast<unsigned int>(key);
            const bool ctrl = args.IsKeyPressed(VirtualKey::Control);
            const bool alt = args.IsKeyPressed(VirtualKey::Menu) || args.IsKeyPressed(VirtualKey::LeftWindows) ||
                args.IsKeyPressed(VirtualKey::RightWindows);
            const bool shift = args.IsKeyPressed(VirtualKey::Shift);
            if (alt) return;
            const auto session = args.EditSession();
            if (!session) return;
            if ((ctrl && shift && vk == 'E') || (!ctrl && !shift && vk == VK_DIVIDE)) {
                if (Toggle(session)) args.Handled(true);
                return;
            }
            if (ctrl || shift) return;
            const bool composing = !engine.Digits().empty();
            const char digit = Engine::PhysicalToLogical(vk, status.ScanCode | (status.IsExtendedKey ? 0x100U : 0U));
            bool handled = false;
            if (digit >= '2' && digit <= '9') {
                const auto before = engine.Digits();
                if (engine.AppendDigit(digit)) {
                    if (BeginOrUpdate(session, before)) {
                        snapshot.selected = 0; snapshot.candidates = engine.Candidates(); handled = true;
                    } else { RestoreDigits(before); if (before.empty()) { original.clear(); textBox = 0; } }
                }
            } else if (composing && vk == VK_BACK) {
                const auto before = engine.Digits();
                if (before.size() == 1) handled = Finish(session, true);
                else {
                    engine.Backspace();
                    if (BeginOrUpdate(session, before)) { snapshot.selected = 0; snapshot.candidates = engine.Candidates(); handled = true; }
                    else RestoreDigits(before);
                }
            } else if (composing && vk == VK_ESCAPE) handled = Finish(session, true);
            else if (composing && (vk == VK_RETURN || vk == VK_SPACE || digit == '0')) handled = Finish(session, false);
            else if (digit == '0') handled = Finish(session, false, L" ");
            else if (digit == '1') handled = Finish(session, false, engine.GetMode() == Mode::Pinyin ? L"，" : L",");
            else if (vk == VK_DECIMAL || (vk == VK_DELETE && !status.IsExtendedKey && status.ScanCode == 0x53))
                handled = Finish(session, false, engine.GetMode() == Mode::Pinyin ? L"。" : L".");
            else if (composing && (vk == VK_MULTIPLY || vk == VK_RIGHT || vk == VK_DOWN ||
                vk == VK_LEFT || vk == VK_UP || vk == VK_ADD || vk == VK_SUBTRACT || vk == VK_NEXT || vk == VK_PRIOR)) {
                const auto size = snapshot.candidates.size();
                if (size) {
                    const bool backward = vk == VK_LEFT || vk == VK_UP || vk == VK_SUBTRACT || vk == VK_PRIOR;
                    const std::size_t step = (vk == VK_ADD || vk == VK_SUBTRACT || vk == VK_NEXT || vk == VK_PRIOR) ? 9 : 1;
                    snapshot.selected = backward ? (snapshot.selected + size - step % size) % size : (snapshot.selected + step) % size;
                }
                handled = true;
            } else if (composing && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU &&
                vk != VK_CAPITAL && vk != VK_NUMLOCK) {
                Finish(session, false); // Commit owned preedit, then pass the ordinary key through.
            }
            if (handled) args.Handled(true);
            Refresh();
        } catch (const winrt::hresult_error& error) {
            Fail(State::Failed, L"输入服务拒绝了编辑操作；按键继续交给当前应用。", error.code());
        } catch (...) { Fail(State::Failed, L"输入服务编辑失败。", E_FAIL); }
    }
    template<class F> auto Handler(F callback) {
        std::weak_ptr<Impl> weak = shared_from_this();
        return [weak, callback](const auto&, const auto& args) noexcept {
            if (const auto self = weak.lock()) {
                std::lock_guard<std::recursive_mutex> lock(self->mutex);
                if (!self->OnThread() || !self->running || self->busy) return;
                try { callback(*self, args); }
                catch (...) { self->busy = false; self->Fail(State::Failed, L"输入服务事件处理失败。", E_FAIL); }
            }
        };
    }
#endif
    void Stop() noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        running = false;
#ifdef NUMPAD9_HAS_PREVIEW_TEXT
        try {
            if (OnThread() && processor && Allowed() && !engine.Digits().empty()) {
                busy = true; const auto session = processor.CreateEditSession();
                if (session) Finish(session, true); busy = false;
            }
        } catch (...) { busy = false; }
        busy = false;
        keyReceived.revoke(); activated.revoke(); deactivated.revoke(); entered.revoke();
        removed.revoke(); terminated.revoke(); infoChanged.revoke(); contentChanged.revoke();
        modeChanged.revoke();
        processor = nullptr; service = nullptr;
#endif
        Clear(); snapshot.state = State::Stopped; snapshot.message = L"输入服务已停止。";
        snapshot.error = S_OK; snapshot.active = snapshot.hasFocus = false; Publish();
    }
    bool Start(const Config& config, const std::string& path) {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        Stop();
        if (!OnThread()) { Fail(State::Failed, L"输入服务必须由界面线程启动。", RPC_E_WRONG_THREAD); return false; }
        if (config.featureId.empty() || config.token.empty() || config.attestation.empty() || config.inputProfile.empty()) {
            Fail(State::NotConfigured, L"Microsoft 输入法权限申请尚未完成：缺少授权令牌或已批准的输入配置。"); return false;
        }
#ifndef NUMPAD9_HAS_PREVIEW_TEXT
        (void)path;
        Fail(State::UnsupportedWindows, L"此构建的 Windows SDK 未包含新的输入服务 API。", E_NOTIMPL); return false;
#else
        try {
            if (!winrt::Windows::Foundation::Metadata::ApiInformation::IsTypePresent(
                L"Windows.UI.Input.Preview.Text.TextInputService")) {
                Fail(State::UnsupportedWindows, L"此输入服务需要 Windows 11 24H2 或更高版本。", E_NOTIMPL); return false;
            }
            UINT32 packageLength = 0;
            if (GetCurrentPackageFullName(&packageLength, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
                Fail(State::Denied, L"此输入服务必须在已批准的 MSIX 包身份中运行。", E_ACCESSDENIED); return false;
            }
            const auto approval = winrt::Windows::ApplicationModel::LimitedAccessFeatures::TryUnlockFeature(
                config.featureId, config.token, config.attestation);
            using Access = winrt::Windows::ApplicationModel::LimitedAccessFeatureStatus;
            if (approval.Status() != Access::Available && approval.Status() != Access::AvailableWithoutToken) {
                Fail(State::Denied, L"Microsoft 尚未授权此包使用系统输入服务。", E_ACCESSDENIED); return false;
            }
            if (!engine.LoadDictionary(path)) {
                Fail(State::Failed, L"无法加载内置离线词库。", HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)); return false;
            }
            service = text::TextInputService::GetForCurrentThread();
            processor = service.CreateKeyboardInputProcessor(config.inputProfile);
            if (!processor) { Fail(State::Failed, L"Windows 未接受已配置的输入配置。", E_FAIL); return false; }
            running = true;
            activated = processor.Activated(winrt::auto_revoke, Handler([](Impl& self, const auto&) { self.Refresh(); }));
            deactivated = processor.Deactivated(winrt::auto_revoke, Handler([](Impl& self, const auto&) { self.Clear(); self.Refresh(); }));
            entered = processor.FocusEntered(winrt::auto_revoke, Handler([](Impl& self, const auto&) { self.Clear(); self.Refresh(); }));
            removed = processor.FocusRemoved(winrt::auto_revoke, Handler([](Impl& self, const auto&) { self.Clear(); self.Refresh(); }));
            terminated = processor.CompositionTerminated(winrt::auto_revoke, Handler([](Impl& self, const auto&) { self.Clear(); self.Refresh(); }));
            infoChanged = processor.TextBoxInfoChanged(winrt::auto_revoke, Handler([](Impl& self, const auto&) { self.Refresh(); }));
            contentChanged = processor.TextBoxContentChanged(winrt::auto_revoke, Handler([](Impl& self, const auto& args) {
                if (args.Source() == text::TextChangeSource::External) self.Clear(); self.Refresh();
            }));
            modeChanged = processor.ConversionModeChanged(winrt::auto_revoke, Handler([](Impl& self, const auto& args) {
                const auto mode = args.NewConversionMode();
                const Mode next = mode == text::TextConversionMode::AlphanumericHalfWidth ||
                    mode == text::TextConversionMode::AlphanumericFullWidth ||
                    mode == text::TextConversionMode::NoConversion ? Mode::English : Mode::Pinyin;
                if (next != self.engine.GetMode()) {
                    if (!self.engine.Digits().empty() && self.Allowed()) {
                        self.busy = true;
                        const auto session = self.processor.CreateEditSession();
                        if (session) self.Finish(session, false);
                        self.busy = false;
                    }
                    self.engine.SetMode(next); self.Clear();
                }
                self.Refresh();
            }));
            keyReceived = processor.KeyEventReceived(winrt::auto_revoke, Handler([](Impl& self, const auto& args) { self.Key(args); }));
            busy = true;
            processor.ConversionMode(text::TextConversionMode::NativeHalfWidth);
            engine.SetMode(Mode::Pinyin); busy = false;
            Refresh(); return running;
        } catch (const winrt::hresult_error& error) {
            Stop(); Fail(State::Failed, L"Windows 拒绝创建输入服务。请核对 Microsoft 审批配置。", error.code()); return false;
        } catch (...) { Stop(); Fail(State::Failed, L"无法启动输入服务。", E_FAIL); return false; }
#endif
    }
};

InputService::InputService(Changed changed) : impl_(std::make_shared<Impl>(std::move(changed))) {}
InputService::~InputService() { if (impl_) { impl_->changed = {}; impl_->Stop(); } }
bool InputService::Start(const Config& config, const std::string& path) { return impl_->Start(config, path); }
void InputService::Stop() noexcept { impl_->Stop(); }
Snapshot InputService::GetSnapshot() const { std::lock_guard<std::recursive_mutex> lock(impl_->mutex); return impl_->snapshot; }
bool InputService::SelectCandidate(std::size_t index, unsigned long long revision, bool commit) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!impl_->OnThread() || !impl_->running || impl_->busy || revision != impl_->snapshot.revision || index >= impl_->snapshot.candidates.size()) return false;
#ifdef NUMPAD9_HAS_PREVIEW_TEXT
    try {
        if (!impl_->Allowed()) return false;
        impl_->snapshot.selected = index;
        if (!commit) { impl_->Publish(); return true; }
        impl_->busy = true;
        const auto session = impl_->processor.CreateEditSession();
        const bool accepted = session && impl_->Finish(session, false);
        impl_->busy = false; impl_->Refresh(); return accepted;
    } catch (...) { impl_->busy = false; impl_->Fail(State::Failed, L"候选确认失败。", E_FAIL); return false; }
#else
    (void)commit; return false;
#endif
}
bool InputService::ToggleMode() {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!impl_->OnThread() || !impl_->running || impl_->busy) return false;
#ifdef NUMPAD9_HAS_PREVIEW_TEXT
    try {
        if (!impl_->processor || !impl_->processor.IsActive()) return false;
        impl_->busy = true;
        const auto session = impl_->engine.Digits().empty() ? text::TextEditSession{nullptr} : impl_->processor.CreateEditSession();
        const bool toggled = impl_->Toggle(session); impl_->busy = false; return toggled;
    } catch (...) { impl_->busy = false; impl_->Fail(State::Failed, L"模式切换失败。", E_FAIL); return false; }
#else
    return false;
#endif
}
}
