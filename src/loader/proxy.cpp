#include <windows.h>
#include <unknwn.h>
namespace {
HMODULE self, systemInput;
INIT_ONCE systemOnce = INIT_ONCE_STATIC_INIT, backendOnce = INIT_ONCE_STATIC_INIT;
BOOL CALLBACK loadSystem(PINIT_ONCE, PVOID, PVOID *) {
    wchar_t path[32768];
    UINT n = GetSystemDirectoryW(path, 32768);
    if (n && n < 32750) {
        wcscat_s(path, L"\\dinput8.dll");
        systemInput = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    return TRUE;
}
FARPROC resolve(const char *name) {
    InitOnceExecuteOnce(&systemOnce, loadSystem, nullptr, nullptr);
    return systemInput ? GetProcAddress(systemInput, name) : nullptr;
}
BOOL CALLBACK loadBackend(PINIT_ONCE, PVOID, PVOID *) {
    wchar_t executable[32768]{};
    DWORD count = GetModuleFileNameW(nullptr, executable, 32768);
    if (!count || count >= 32768)
        return TRUE;
    auto filename = wcsrchr(executable, L'\\');
    if (!filename || _wcsicmp(filename + 1, L"Dishonored2.exe"))
        return TRUE;
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(self, path, 32768);
    if (!n || n >= 32768)
        return TRUE;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash)
        return TRUE;
    constexpr wchar_t suffix[] = L"VoidPhotoMode\\VoidPhotoBackend.dll";
    const size_t remaining = 32768 - size_t(slash + 1 - path);
    if (remaining < sizeof(suffix) / sizeof(*suffix)) {
        OutputDebugStringW(L"Void Photo Mode: backend path too long\n");
        return TRUE;
    }
    wcscpy_s(slash + 1, remaining, suffix);
    HMODULE backend =
        LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (backend) {
        auto start = reinterpret_cast<void(WINAPI *)()>(GetProcAddress(backend, "VoidPhotoStart"));
        if (start)
            start();
    } else
        OutputDebugStringW(
            L"Void Photo Mode: backend missing or failed to load; forwarding DirectInput normally.\n");
    return TRUE;
}
} // namespace
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID *output,
                                             LPUNKNOWN outer) {
    auto real = reinterpret_cast<HRESULT(WINAPI *)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN)>(
        resolve("DirectInput8Create"));
    if (!real)
        return E_FAIL;
    // Called by the game after process initialization, never from our DllMain.
    InitOnceExecuteOnce(&backendOnce, loadBackend, nullptr, nullptr);
    return real(instance, version, iid, output, outer);
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
    auto f = reinterpret_cast<HRESULT(WINAPI *)()>(resolve("DllCanUnloadNow"));
    return f ? f() : S_FALSE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID i, LPVOID *o) {
    auto f = reinterpret_cast<HRESULT(WINAPI *)(REFCLSID, REFIID, LPVOID *)>(resolve("DllGetClassObject"));
    return f ? f(c, i, o) : CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    auto f = reinterpret_cast<HRESULT(WINAPI *)()>(resolve("DllRegisterServer"));
    return f ? f() : E_FAIL;
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    auto f = reinterpret_cast<HRESULT(WINAPI *)()>(resolve("DllUnregisterServer"));
    return f ? f() : E_FAIL;
}
BOOL WINAPI DllMain(HMODULE module, DWORD reason, void *) {
    if (reason == DLL_PROCESS_ATTACH)
        self = module;
    return TRUE;
}
