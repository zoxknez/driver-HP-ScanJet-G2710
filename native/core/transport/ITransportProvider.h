// Jedini mehanizam zamene transporta u testovima.
//
// SimTransport je C++ objekat. HANDLE koji UsbScanTransport prosledjuje u
// ReadFile / WriteFile / DeviceIoControl mora biti pravi kernel handle, pa se
// simulator NE MOZE predstaviti laznim Win32 handle-om. Zamena se zato radi
// IZNAD transporta, ne ispod njega:
//
//   produkcija   TransportProvider::create(ref)  -> UsbScanTransport
//   test         ScopedTestProvider guard{...}   -> SimTransport
//
// Tako wiaharness i twainharness voze isti pravi code path i menjaju samo
// najnizu instancu.

#pragma once

#include "ITransport.h"

#include <memory>
#include <string>

namespace g2710 {

// Sta se zatvara kada transport nestane. Bitno kada handle dolazi spolja
// (IStiDeviceControl::GetMyDeviceHandle) - tada ga MI ne smemo zatvoriti.
enum class HandleOwnership {
    Owned,     // transport ga je otvorio i zatvara ga
    Borrowed,  // vlasnik je neko drugi (STI); transport ga samo koristi
};

// Kako doci do uredjaja.
class DeviceRef {
public:
    enum class Kind {
        // \\.\Usbscan0 - CLI, aplikacija, TWAIN.
        DevicePath,

        // Ime porta iz IStiDeviceControl::GetMyDevicePortName. DOKUMENTOVANI
        // produkcioni put za WIA; transport radi sopstveni CreateFile sa
        // FILE_FLAG_OVERLAPPED.
        PortName,

        // Handle iz IStiDeviceControl::GetMyDeviceHandle. OPTIMIZATION
        // CANDIDATE, ne produkcioni put - ne znamo pouzdano access prava ni
        // overlapped/cancel model tog handle-a. Kvalifikuje se u H11 pre nego
        // sto bi zamenio PortName. Vidi docs/G2710-PROFILE.md.
        ExistingHandle,
    };

    static DeviceRef devicePath(std::wstring path);
    static DeviceRef portName(std::wstring name);
    static DeviceRef existingHandle(void* handle, HandleOwnership ownership);

    // Podrazumevani put za CLI/App/TWAIN: \\.\Usbscan0.
    static DeviceRef defaultUsbScan();

    Kind kind() const noexcept { return kind_; }
    const std::wstring& path() const noexcept { return path_; }
    void* handle() const noexcept { return handle_; }
    HandleOwnership ownership() const noexcept { return ownership_; }

private:
    DeviceRef() = default;

    Kind kind_ = Kind::DevicePath;
    std::wstring path_;
    void* handle_ = nullptr;
    HandleOwnership ownership_ = HandleOwnership::Owned;
};

class ITransportProvider {
public:
    virtual ~ITransportProvider() = default;
    virtual Result<std::unique_ptr<ITransport>> create(const DeviceRef& ref) = 0;
    virtual const char* name() const noexcept = 0;
};

namespace TransportProvider {

// Pravi transport za dati uredjaj. Vraca produkcioni UsbScanTransport, osim
// ako je test provider aktivan.
Result<std::unique_ptr<ITransport>> create(const DeviceRef& ref);

// Ime aktivnog provider-a; dijagnostika mora moci da kaze da li je test
// provider slucajno ostao aktivan.
const char* activeProviderName() noexcept;

// RAII zamena za testove. Namerno nema golog setForTesting() - override koji
// procuri iz jednog testa u drugi je tacno ona vrsta greske koju je tesko naci.
class ScopedTestProvider {
public:
    explicit ScopedTestProvider(std::unique_ptr<ITransportProvider> provider);
    ~ScopedTestProvider();

    ScopedTestProvider(const ScopedTestProvider&) = delete;
    ScopedTestProvider& operator=(const ScopedTestProvider&) = delete;

private:
    ITransportProvider* previous_;
    std::unique_ptr<ITransportProvider> owned_;
};

}  // namespace TransportProvider

}  // namespace g2710
