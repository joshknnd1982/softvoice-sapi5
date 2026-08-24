#pragma once

// The COM plumbing the SAPI 5 interface is built on: reference counting, a
// class factory, and self-registration. Adapted from the bestspeech SAPI 5
// wrapper, whose shape this package deliberately follows.

#include <atomic>
#include <comdef.h>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

#include "sv_registry.hpp"
#include "sv_utils.hpp"

namespace SoftVoice {
namespace com {

[[nodiscard]] wchar_t* strdup(const std::wstring& s);

template <class T>
[[nodiscard]] std::wstring clsid_as_string()
{
    utils::out_ptr<wchar_t> p(CoTaskMemFree);
    const HRESULT hr = StringFromCLSID(__uuidof(T), p.address());
    if (FAILED(hr)) {
        throw _com_error(hr);
    }
    return std::wstring(p.get());
}

class object_counter {
public:
    static void increment() noexcept
    {
        count_.fetch_add(1, std::memory_order_relaxed);
    }

    static void decrement() noexcept
    {
        count_.fetch_sub(1, std::memory_order_relaxed);
    }

    [[nodiscard]] static bool is_zero() noexcept
    {
        return count_.load(std::memory_order_acquire) == 0;
    }

private:
    static std::atomic<long> count_;
};

template <class I, class O>
[[nodiscard]] inline void* try_interface(O* ptr, REFIID riid) noexcept
{
    return IsEqualIID(riid, __uuidof(I)) ? static_cast<I*>(ptr) : nullptr;
}

template <class I, class O>
[[nodiscard]] inline void* try_primary_interface(O* ptr, REFIID riid) noexcept
{
    if (IsEqualIID(riid, __uuidof(IUnknown))) {
        return static_cast<IUnknown*>(static_cast<I*>(ptr));
    }
    if (IsEqualIID(riid, __uuidof(I))) {
        return static_cast<I*>(ptr);
    }
    return nullptr;
}

template <class T>
class object;

template <class T>
class IUnknownImpl : public T {
    friend class object<T>;

private:
    IUnknownImpl() : ref_count_(1) {}

    template <typename A>
    explicit IUnknownImpl(const A& arg) : T(arg), ref_count_(1)
    {
    }

    ~IUnknownImpl() { object_counter::decrement(); }

    std::atomic<long> ref_count_;

public:
    IUnknownImpl(const IUnknownImpl&) = delete;
    IUnknownImpl& operator=(const IUnknownImpl&) = delete;

    STDMETHOD_(ULONG, AddRef)() noexcept override
    {
        return static_cast<ULONG>(
            ref_count_.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    STDMETHOD_(ULONG, Release)() noexcept override
    {
        const long count =
            ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (count == 0) {
            delete this;
        }
        return static_cast<ULONG>(count);
    }

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) noexcept override
    {
        if (!ppv) {
            return E_POINTER;
        }
        void* pv = this->get_interface(riid);
        *ppv = pv;
        if (!pv) {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
};

template <class T>
class object {
public:
    object() : impl_(new IUnknownImpl<T>) { object_counter::increment(); }

    template <typename A>
    explicit object(const A& arg) : impl_(new IUnknownImpl<T>(arg))
    {
        object_counter::increment();
    }

    ~object() { impl_->Release(); }

    object(const object&) = delete;
    object& operator=(const object&) = delete;

    [[nodiscard]] IUnknownImpl<T>* operator->() const noexcept
    {
        return impl_;
    }

private:
    IUnknownImpl<T>* impl_;
};

template <class I>
class interface_ptr {
public:
    interface_ptr() noexcept : ptr_(nullptr) {}

    template <class T>
    explicit interface_ptr(object<T>& obj)
    {
        if (FAILED(obj->QueryInterface(__uuidof(I),
                                       reinterpret_cast<void**>(&ptr_)))) {
            throw std::invalid_argument("Unsupported interface");
        }
    }

    interface_ptr(const interface_ptr& other) noexcept : ptr_(other.ptr_)
    {
        if (ptr_) {
            ptr_->AddRef();
        }
    }

    interface_ptr& operator=(const interface_ptr& other) noexcept
    {
        if (this != &other) {
            if (other.ptr_) {
                other.ptr_->AddRef();
            }
            release();
            ptr_ = other.ptr_;
        }
        return *this;
    }

    interface_ptr(interface_ptr&& other) noexcept : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }

    ~interface_ptr() { release(); }

    [[nodiscard]] I* get(bool add_ref = true) noexcept
    {
        if (ptr_ && add_ref) {
            ptr_->AddRef();
        }
        return ptr_;
    }

private:
    void release() noexcept
    {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    I* ptr_;
};

template <class T>
class IClassFactoryImpl : public IClassFactory {
protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        return try_primary_interface<IClassFactory>(this, riid);
    }

public:
    STDMETHOD(CreateInstance)
    (IUnknown* outer, REFIID riid, void** ppv) noexcept override
    {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        if (!ppv) {
            return E_POINTER;
        }
        *ppv = nullptr;
        try {
            object<T> obj;
            return obj->QueryInterface(riid, ppv);
        }
        catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
        catch (...) {
            return E_UNEXPECTED;
        }
    }

    STDMETHOD(LockServer)(BOOL lock) noexcept override
    {
        if (lock) {
            object_counter::increment();
        } else {
            object_counter::decrement();
        }
        return S_OK;
    }
};

class class_object_factory {
public:
    class_object_factory() = default;

    class_object_factory(const class_object_factory&) = delete;
    class_object_factory& operator=(const class_object_factory&) = delete;

    template <class T>
    void register_class()
    {
        creators_.push_back(std::make_shared<concrete_creator<T>>());
    }

    [[nodiscard]] HRESULT create(REFCLSID rclsid, REFIID riid,
                                 void** ppv) const noexcept;

private:
    class creator {
    public:
        virtual ~creator() = default;
        [[nodiscard]] virtual bool matches(REFCLSID rclsid) const noexcept = 0;
        [[nodiscard]] virtual HRESULT create(REFIID riid, void** ppv) const = 0;
    };

    template <class T>
    class concrete_creator : public creator {
    public:
        [[nodiscard]] bool matches(REFCLSID rclsid) const noexcept override
        {
            return IsEqualCLSID(rclsid, __uuidof(T));
        }

        [[nodiscard]] HRESULT create(REFIID riid, void** ppv) const override
        {
            object<IClassFactoryImpl<T>> obj;
            return obj->QueryInterface(riid, ppv);
        }
    };

    std::vector<std::shared_ptr<creator>> creators_;
};

class class_registrar {
public:
    explicit class_registrar(HINSTANCE dll_handle);

    class_registrar(const class_registrar&) = delete;
    class_registrar& operator=(const class_registrar&) = delete;

    template <class T>
    void register_class()
    {
        registry::key classes(HKEY_LOCAL_MACHINE, clsid_key_path,
                              KEY_CREATE_SUB_KEY);
        registry::key clsid(classes, clsid_as_string<T>(), KEY_CREATE_SUB_KEY,
                            true);
        registry::key server(clsid, L"InProcServer32", KEY_SET_VALUE, true);
        server.set(dll_path_);
        server.set(L"ThreadingModel", L"Both");
    }

    template <class T>
    void unregister_class()
    {
        const std::wstring clsid_string = clsid_as_string<T>();
        registry::key classes(HKEY_LOCAL_MACHINE, clsid_key_path);
        registry::key clsid(classes, clsid_string);
        clsid.delete_subkey(L"InProcServer32");
        classes.delete_subkey(clsid_string);
    }

private:
    std::wstring dll_path_;
    static const std::wstring clsid_key_path;
};

}  // namespace com
}  // namespace SoftVoice
