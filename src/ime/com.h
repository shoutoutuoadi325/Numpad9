#pragma once
#include <windows.h>
#include <atomic>
#include <utility>

namespace numpad9::ime {
extern HINSTANCE gModule;
extern std::atomic<long> gObjects;
extern std::atomic<long> gServerLocks;

template<class T> class ComPtr {
    T* p_ = nullptr;
public:
    ComPtr() = default;
    explicit ComPtr(T* p) : p_(p) { if (p_) p_->AddRef(); }
    ComPtr(const ComPtr& other) : ComPtr(other.p_) {}
    ComPtr(ComPtr&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }
    ~ComPtr() { reset(); }
    ComPtr& operator=(const ComPtr& other) { if (this != &other) { ComPtr copy(other); swap(copy); } return *this; }
    ComPtr& operator=(ComPtr&& other) noexcept { if (this != &other) { reset(); p_ = other.p_; other.p_ = nullptr; } return *this; }
    void reset(T* p = nullptr) { if (p_) p_->Release(); p_ = p; }
    void swap(ComPtr& other) { std::swap(p_, other.p_); }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** put() { reset(); return &p_; }
    T* detach() { T* result = p_; p_ = nullptr; return result; }
    template<class U> HRESULT as(REFIID iid, ComPtr<U>& result) const {
        return p_ ? p_->QueryInterface(iid, reinterpret_cast<void**>(result.put())) : E_NOINTERFACE;
    }
};

class RefCount {
    std::atomic<ULONG> refs_{1};
protected:
    RefCount() { ++gObjects; }
    virtual ~RefCount() { --gObjects; }
public:
    ULONG Retain() { return ++refs_; }
    ULONG Drop() { const auto refs = --refs_; if (!refs) delete this; return refs; }
};
}
