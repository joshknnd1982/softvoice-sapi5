#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <windows.h>

namespace SoftVoice {
namespace utils {

[[nodiscard]] inline std::wstring widen(const char* s)
{
    if (!s || !*s) {
        return {};
    }
    const int len = static_cast<int>(std::strlen(s));
    const int needed = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, len, result.data(), needed);
    return result;
}

[[nodiscard]] inline std::string narrow(const wchar_t* s, std::size_t n)
{
    if (!s || n == 0) {
        return {};
    }
    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(n), nullptr, 0,
                            nullptr, nullptr);
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(n), result.data(),
                        needed, nullptr, nullptr);
    return result;
}

[[nodiscard]] inline std::string narrow(const std::wstring& s)
{
    return narrow(s.c_str(), s.size());
}

// A raw pointer an API fills in, released with the function it came with.
template <typename T>
class out_ptr {
public:
    template <typename F>
    explicit out_ptr(F f)
        : ptr_(nullptr), deleter_(std::make_unique<deleter_impl<F>>(f))
    {
    }

    ~out_ptr() { release(); }

    out_ptr(const out_ptr&) = delete;
    out_ptr& operator=(const out_ptr&) = delete;

    [[nodiscard]] T* get() const noexcept { return ptr_; }

    T** address() noexcept
    {
        release();
        return &ptr_;
    }

private:
    class deleter_base {
    public:
        virtual ~deleter_base() = default;
        virtual void destroy(T* p) const noexcept = 0;
    };

    template <typename F>
    class deleter_impl : public deleter_base {
    public:
        explicit deleter_impl(F f) : func_(f) {}
        void destroy(T* p) const noexcept override { func_(p); }

    private:
        F func_;
    };

    void release() noexcept
    {
        if (ptr_) {
            deleter_->destroy(ptr_);
            ptr_ = nullptr;
        }
    }

    T* ptr_;
    std::unique_ptr<deleter_base> deleter_;
};

}  // namespace utils
}  // namespace SoftVoice
