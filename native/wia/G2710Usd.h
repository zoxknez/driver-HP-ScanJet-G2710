// IStiUSD - ulazna tacka kojom STI servis otvara uredjaj.
//
// Ovo je JEDINO mesto u projektu koje pravi pravi Win32 HANDLE za skener, i
// zato jedino koje se ne moze testirati bez uredjaja. Sve odluke su izvucene u
// WiaCapabilities; ovde ostaje prevod.
//
// PRODUKCIONA PUTANJA OTVARANJA (docs, MASTER plan):
//
//   IStiUSD::Initialize
//     -> IStiDeviceControl::GetMyDeviceOpenMode
//     -> IStiDeviceControl::GetMyDevicePortName
//     -> CreateFile(portName, ..., FILE_FLAG_OVERLAPPED, ...)
//     -> UsbScanTransport::FromPortName
//
// GetMyDeviceHandle NIJE na ovoj putanji. Nije pogresan, ali se ne zna
// pouzdano kako je taj handle otvoren, sa kojim pravima, da li podnosi nas
// overlapped/cancel model i ko mu je vlasnik. Promovise se tek ako H11 to
// dokaze - do tada stoji neiskoriscen, sa razlogom zapisanim ovde.

#pragma once

#include <windows.h>

#include <objbase.h>
#include <sti.h>
#include <stiusd.h>

#include "device/G2710Device.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace g2710::wia {

class G2710MiniDrv;

class G2710Usd final : public IStiUSD {
public:
    G2710Usd();
    ~G2710Usd();

    G2710Usd(const G2710Usd&) = delete;
    G2710Usd& operator=(const G2710Usd&) = delete;

    // --- IUnknown --------------------------------------------------------
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // --- IStiUSD ---------------------------------------------------------
    STDMETHODIMP Initialize(PSTIDEVICECONTROL control, DWORD stiVersion,
                            HKEY parametersKey) override;
    STDMETHODIMP GetCapabilities(PSTI_USD_CAPS capabilities) override;
    STDMETHODIMP GetStatus(PSTI_DEVICE_STATUS status) override;
    STDMETHODIMP DeviceReset() override;
    STDMETHODIMP Diagnostic(LPSTI_DIAG diagnostic) override;
    STDMETHODIMP Escape(STI_RAW_CONTROL_CODE function, LPVOID inData, DWORD inSize,
                        LPVOID outData, DWORD outSize, LPDWORD actual) override;
    STDMETHODIMP GetLastError(LPDWORD lastError) override;
    STDMETHODIMP LockDevice() override;
    STDMETHODIMP UnLockDevice() override;
    STDMETHODIMP RawReadData(LPVOID buffer, LPDWORD size, LPOVERLAPPED overlapped) override;
    STDMETHODIMP RawWriteData(LPVOID buffer, DWORD size, LPOVERLAPPED overlapped) override;
    STDMETHODIMP RawReadCommand(LPVOID buffer, LPDWORD size, LPOVERLAPPED overlapped) override;
    STDMETHODIMP RawWriteCommand(LPVOID buffer, DWORD size, LPOVERLAPPED overlapped) override;
    STDMETHODIMP SetNotificationHandle(HANDLE event) override;
    STDMETHODIMP GetNotificationData(LPSTINOTIFY notify) override;
    STDMETHODIMP GetLastErrorInfo(STI_ERROR_INFO* info) override;

    // Uredjaj, za IWiaMiniDrv koji zivi u istom objektu.
    G2710Device* device() noexcept { return device_.get(); }
    const std::wstring& portName() const noexcept { return portName_; }

private:
    // Prevodi nas ErrorCode u HRESULT i pamti ga za GetLastError.
    HRESULT recordError(const Error& error);
    HRESULT recordWin32(DWORD code, HRESULT result);

    std::atomic<ULONG> references_{1};

    std::wstring portName_;
    DWORD openMode_ = 0;
    std::unique_ptr<G2710Device> device_;

    // STI servis moze zvati iz vise niti; stanje uredjaja se cuva ovim.
    std::recursive_mutex lock_;
    int lockDepth_ = 0;

    HANDLE notificationEvent_ = nullptr;
    DWORD pendingButtonMask_ = 0;

    DWORD lastWin32_ = 0;
    ErrorCode lastError_ = ErrorCode::Ok;
};

}  // namespace g2710::wia
