// Entry points for the SoftVoice SAPI 5 in-process server.
//
// Registration writes exactly two things: the CLSIDs of the two COM objects
// below, and one TokenEnums key telling SAPI to ask this DLL for its voice
// list. No voice is ever written to the registry, and the SoftVoice engine
// itself reads none.

#include <new>
#include <sapi.h>
#include <windows.h>

#include "sv_com.hpp"
#include "sv_engine.hpp"
#include "sv_enum_tokens.hpp"
#include "sv_log.hpp"
#include "sv_paths.hpp"
#include "sv_registry.hpp"

namespace {

HINSTANCE g_dll = nullptr;
SoftVoice::com::class_object_factory g_factory;

// SAPI reads this key from HKEY_LOCAL_MACHINE only. An enumerator written
// under HKEY_CURRENT_USER registers without complaint and is then never
// consulted, which looks exactly like a broken engine.
const std::wstring kTokenEnumsPath =
    L"Software\\Microsoft\\Speech\\Voices\\TokenEnums";

[[nodiscard]] std::wstring clsid_to_string(const GUID& clsid)
{
    wchar_t buffer[64] = {};
    StringFromGUID2(clsid, buffer, 64);
    return std::wstring(buffer);
}

void register_token_enumerator()
{
    using namespace SoftVoice;

    registry::key enums(HKEY_LOCAL_MACHINE, kTokenEnumsPath,
                        KEY_CREATE_SUB_KEY | KEY_SET_VALUE, true);
    registry::key ours(enums, L"SoftVoice", KEY_SET_VALUE, true);
    ours.set(L"SoftVoice Voices");
    ours.set(L"CLSID",
             clsid_to_string(__uuidof(sapi::IEnumSpObjectTokensImpl)));
}

void unregister_token_enumerator() noexcept
{
    using namespace SoftVoice;
    try {
        registry::key enums(HKEY_LOCAL_MACHINE, kTokenEnumsPath,
                            KEY_ALL_ACCESS);
        enums.delete_subkey(L"SoftVoice");
    }
    catch (...) {
    }
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_dll = instance;
        DisableThreadLibraryCalls(instance);

        // Only opening the log file here. Winsock, the settings file and the
        // host are all left until they are first needed: DllMain runs under
        // the loader lock, and WSAStartup loads service providers.
        SoftVoice::log::init(L"sapi");
        SV_LOG_INFO("dll: attached from %S",
                    SoftVoice::paths::module_dir(instance).c_str());

        try {
            g_factory.register_class<
                SoftVoice::sapi::IEnumSpObjectTokensImpl>();
            g_factory.register_class<SoftVoice::sapi::ISpTTSEngineImpl>();
        }
        catch (...) {
            SV_LOG_ERROR("dll: could not register the class objects");
            return FALSE;
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        SoftVoice::log::shutdown();
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    return g_factory.create(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return SoftVoice::com::object_counter::is_zero() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    try {
        SoftVoice::com::class_registrar registrar(g_dll);
        registrar.register_class<SoftVoice::sapi::IEnumSpObjectTokensImpl>();
        registrar.register_class<SoftVoice::sapi::ISpTTSEngineImpl>();
        register_token_enumerator();
        SV_LOG_INFO("dll: registered (%d-bit)",
                    static_cast<int>(sizeof(void*) * 8));
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        SV_LOG_ERROR("dll: registration failed");
        return E_UNEXPECTED;
    }
}

STDAPI DllUnregisterServer()
{
    try {
        unregister_token_enumerator();
        SoftVoice::com::class_registrar registrar(g_dll);
        registrar.unregister_class<SoftVoice::sapi::IEnumSpObjectTokensImpl>();
        registrar.unregister_class<SoftVoice::sapi::ISpTTSEngineImpl>();
        SV_LOG_INFO("dll: unregistered (%d-bit)",
                    static_cast<int>(sizeof(void*) * 8));
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        // Unregistering must never fail loudly: an uninstall that stops
        // half way leaves the machine in a worse state than one that
        // tolerates a key that has already gone.
        return S_OK;
    }
}
