// Jedini produkcioni transport. Ide preko Microsoftovog in-box usbscan.sys.
//
// Isti objekat opsluzuje CLI, aplikaciju, TWAIN i WIA - razlikuje se samo
// odakle dolazi handle:
//
//   DeviceRef::devicePath(L"\\\\.\\Usbscan0")  CLI / App / TWAIN
//   DeviceRef::portName(...)                   WIA, dokumentovani put
//   DeviceRef::existingHandle(...)             WIA, optimization candidate (H11)
//
// Zato prijatelj instalira drajver JEDNOM - ne menjamo mu PnP stack izmedju
// dijagnosticke i WIA faze.

#pragma once

#include "ITransport.h"
#include "ITransportProvider.h"

#include <atomic>
#include <memory>
#include <string>

namespace g2710 {

class UsbScanTransport final : public ITransport {
public:
    static Result<std::unique_ptr<UsbScanTransport>> open(const DeviceRef& ref);

    ~UsbScanTransport() override;

    Status controlIn(std::uint16_t address, Command command,
                     std::span<std::byte> buffer) override;
    Status controlOut(std::uint16_t address, Command command,
                      std::span<const std::byte> buffer) override;
    std::size_t maxControlChunk() const noexcept override { return maxControlChunk_; }

    Result<std::size_t> bulkRead(std::span<std::byte> buffer) override;
    Status bulkWrite(std::span<const std::byte> buffer) override;

    Result<std::uint32_t> waitEvent() override;

    Status resetPipe(PipeKind pipe) override;
    Status setTimeouts(const Timeouts& timeouts) override;
    Result<PipeConfiguration> pipeConfiguration() override;

    void cancel() noexcept override;
    void clearCancel() noexcept override;
    Status reopen() override;

    bool isOpen() const noexcept override { return handle_ != nullptr; }
    const char* name() const noexcept override { return "usbscan"; }

    // Register bank je 1818 bajtova i referenca ga salje u JEDNOM control
    // transferu. Da li usbscan.sys to dozvoljava nije dokumentovano i
    // proverava se u H2. Do tada podrazumevano ne delimo transfer; ako se
    // pokaze da postoji limit, H2 ga spusta ovde i deljenje se ukljucuje
    // iza istog API-ja, bez promene sloja iznad.
    //
    // Deljenje je validno ISKLJUCIVO za RegisterRead / RegisterWrite, gde je
    // wValue adresa pa se komadi mogu adresirati. Ostale komande nose
    // fiksne blokove (6 bajtova i slicno) i nikada se ne dele.
    void setMaxControlChunk(std::size_t bytes) noexcept;

    // Sirovi USB device descriptor preko IOCTL_GET_DEVICE_DESCRIPTOR.
    Result<DeviceIdentity> identity() override;

private:
    UsbScanTransport(void* handle, HandleOwnership ownership, DeviceRef ref);

    Status controlTransfer(std::uint16_t address, Command command,
                           std::byte* data, std::size_t length, bool directionIn);
    Status controlChunked(std::uint16_t address, Command command,
                          std::byte* data, std::size_t length, bool directionIn);

    Status deviceControl(unsigned long code, void* input, std::size_t inputSize,
                         void* output, std::size_t outputSize,
                         unsigned long* bytesReturned, const char* context);

    void closeHandle() noexcept;

    void* handle_ = nullptr;
    HandleOwnership ownership_ = HandleOwnership::Owned;
    DeviceRef ref_;
    Timeouts timeouts_{};
    std::size_t maxControlChunk_ = 0;  // 0 = bez deljenja
    std::atomic<bool> cancelled_{false};
};

// Produkcioni provider: uvek pravi UsbScanTransport.
class UsbScanTransportProvider final : public ITransportProvider {
public:
    Result<std::unique_ptr<ITransport>> create(const DeviceRef& ref) override;
    const char* name() const noexcept override { return "usbscan"; }
};

}  // namespace g2710
