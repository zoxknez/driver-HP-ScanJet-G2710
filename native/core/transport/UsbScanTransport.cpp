#include "UsbScanTransport.h"

#include <windows.h>

// usbscan.h koristi CTL_CODE / METHOD_BUFFERED / FILE_ANY_ACCESS, ali ih ne
// povlaci sam - winioctl.h mora doci pre njega.
#include <winioctl.h>

#include <usbscan.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace g2710 {
namespace {

// usbscan.sys ne koristi bmRequestType iz IO_BLOCK_EX - sam ga izvodi iz
// fTransferDirectionIn. Vrednost popunjavamo samo radi citljivosti trace-a;
// izvor istine je smer. Vidi docs/PROTOCOL-RTS8822.md, 3.
constexpr UCHAR kVendorRequest = 0x04;
constexpr UCHAR kRequestTypeIn = 0xC0;
constexpr UCHAR kRequestTypeOut = 0x40;

// CTL_CODE se siri u `int`, a DeviceIoControl ocekuje DWORD. Skupljeno ovde da
// se vidi tacno koji deo usbscan.sys povrsine koristimo.
constexpr unsigned long kIoctlSendUsbRequest =
    static_cast<unsigned long>(IOCTL_SEND_USB_REQUEST);
constexpr unsigned long kIoctlGetChannelAlign =
    static_cast<unsigned long>(IOCTL_GET_CHANNEL_ALIGN_RQST);
constexpr unsigned long kIoctlWaitOnDeviceEvent =
    static_cast<unsigned long>(IOCTL_WAIT_ON_DEVICE_EVENT);
constexpr unsigned long kIoctlResetPipe =
    static_cast<unsigned long>(IOCTL_RESET_PIPE);
constexpr unsigned long kIoctlSetTimeout =
    static_cast<unsigned long>(IOCTL_SET_TIMEOUT);
constexpr unsigned long kIoctlGetPipeConfiguration =
    static_cast<unsigned long>(IOCTL_GET_PIPE_CONFIGURATION);
constexpr unsigned long kIoctlGetDeviceDescriptor =
    static_cast<unsigned long>(IOCTL_GET_DEVICE_DESCRIPTOR);

// USBSCAN_TIMEOUT je u SEKUNDAMA, ne milisekundama.
unsigned long toSeconds(std::chrono::milliseconds ms) noexcept {
    if (ms.count() <= 0) {
        return 0;
    }
    const auto seconds = (ms.count() + 999) / 1000;  // zaokruzi navise
    return static_cast<unsigned long>(
        std::min<long long>(seconds, std::numeric_limits<unsigned long>::max()));
}

PIPE_TYPE toPipeType(PipeKind pipe) noexcept {
    switch (pipe) {
        case PipeKind::BulkIn:    return READ_DATA_PIPE;
        case PipeKind::BulkOut:   return WRITE_DATA_PIPE;
        case PipeKind::Interrupt: return EVENT_PIPE;
    }
    return ALL_PIPE;
}

// Win32 greska -> nasa. Razlika izmedju "istekao rok" i "veza je nestala" je
// sustinska: prva ostavlja uredjaj upotrebljivim, druga proglasava poziciju
// glave nepoznatom i zahteva HOME. Vidi docs/SAFETY.md.
ErrorCode classify(DWORD win32) noexcept {
    switch (win32) {
        case ERROR_SEM_TIMEOUT:
        case WAIT_TIMEOUT:
            return ErrorCode::Timeout;

        case ERROR_OPERATION_ABORTED:
        case ERROR_CANCELLED:
            return ErrorCode::Cancelled;

        case ERROR_DEVICE_NOT_CONNECTED:
        case ERROR_DEV_NOT_EXIST:
        case ERROR_NO_SUCH_DEVICE:
        case ERROR_DEVICE_REMOVED:
        case ERROR_FILE_NOT_FOUND:
            return ErrorCode::TransportLost;

        case ERROR_GEN_FAILURE:
        case ERROR_BAD_COMMAND:
            return ErrorCode::Stalled;

        case ERROR_SHARING_VIOLATION:
        case ERROR_ACCESS_DENIED:
            return ErrorCode::Busy;

        default:
            return ErrorCode::DeviceError;
    }
}

Error win32Error(const char* context) noexcept {
    const DWORD code = ::GetLastError();
    return Error{classify(code), static_cast<std::uint32_t>(code), context};
}

// Overlapped operacija sa sopstvenim dogadjajem. Cancel iz drugog thread-a
// radi preko CancelIoEx nad istim handle-om.
class Overlapped {
public:
    Overlapped() noexcept {
        ZeroMemory(&ov_, sizeof(ov_));
        ov_.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    ~Overlapped() {
        if (ov_.hEvent != nullptr) {
            ::CloseHandle(ov_.hEvent);
        }
    }

    Overlapped(const Overlapped&) = delete;
    Overlapped& operator=(const Overlapped&) = delete;

    bool valid() const noexcept { return ov_.hEvent != nullptr; }
    OVERLAPPED* get() noexcept { return &ov_; }

    // Ceka zavrsetak operacije koja je vratila ERROR_IO_PENDING.
    bool wait(HANDLE device, DWORD* transferred) noexcept {
        return ::GetOverlappedResult(device, &ov_, transferred, TRUE) != FALSE;
    }

private:
    OVERLAPPED ov_{};
};

}  // namespace

const char* toString(Command command) noexcept {
    switch (command) {
        case Command::RegisterWrite:  return "RegisterWrite";
        case Command::RegisterRead:   return "RegisterRead";
        case Command::Eeprom:         return "Eeprom";
        case Command::DmaEnableRead:  return "DmaEnableRead";
        case Command::DmaEnableWrite: return "DmaEnableWrite";
        case Command::DmaCancel:      return "DmaCancel";
        case Command::DmaOpType:      return "DmaOpType";
        case Command::ChipsetReset:   return "ChipsetReset";
    }
    return "Unknown";
}

// ------------------------------------------------------------- DeviceRef ----

DeviceRef DeviceRef::devicePath(std::wstring path) {
    DeviceRef ref;
    ref.kind_ = Kind::DevicePath;
    ref.path_ = std::move(path);
    ref.ownership_ = HandleOwnership::Owned;
    return ref;
}

DeviceRef DeviceRef::portName(std::wstring name) {
    DeviceRef ref;
    ref.kind_ = Kind::PortName;
    ref.path_ = std::move(name);
    ref.ownership_ = HandleOwnership::Owned;
    return ref;
}

DeviceRef DeviceRef::existingHandle(void* handle, HandleOwnership ownership) {
    DeviceRef ref;
    ref.kind_ = Kind::ExistingHandle;
    ref.handle_ = handle;
    ref.ownership_ = ownership;
    return ref;
}

DeviceRef DeviceRef::defaultUsbScan() {
    return devicePath(L"\\\\.\\Usbscan0");
}

// ------------------------------------------------------ UsbScanTransport ----

UsbScanTransport::UsbScanTransport(void* handle, HandleOwnership ownership, DeviceRef ref)
    : handle_(handle), ownership_(ownership), ref_(std::move(ref)) {}

UsbScanTransport::~UsbScanTransport() {
    closeHandle();
}

void UsbScanTransport::closeHandle() noexcept {
    // Handle koji je dosao spolja (GetMyDeviceHandle) zatvara njegov vlasnik.
    if (handle_ != nullptr && ownership_ == HandleOwnership::Owned) {
        ::CloseHandle(static_cast<HANDLE>(handle_));
    }
    handle_ = nullptr;
}

Result<std::unique_ptr<UsbScanTransport>> UsbScanTransport::open(const DeviceRef& ref) {
    if (ref.kind() == DeviceRef::Kind::ExistingHandle) {
        if (ref.handle() == nullptr || ref.handle() == INVALID_HANDLE_VALUE) {
            return fail(ErrorCode::InvalidArgument, "open: prazan handle");
        }
        return std::unique_ptr<UsbScanTransport>(
            new UsbScanTransport(ref.handle(), ref.ownership(), ref));
    }

    if (ref.path().empty()) {
        return fail(ErrorCode::InvalidArgument, "open: prazna putanja");
    }

    // FILE_FLAG_OVERLAPPED je neophodan: bez njega cancel usred transfera ne
    // radi, a to je zahtev i WIA i TWAIN sloja.
    const HANDLE handle = ::CreateFileW(
        ref.path().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        return win32Error("CreateFileW");
    }

    return std::unique_ptr<UsbScanTransport>(
        new UsbScanTransport(handle, HandleOwnership::Owned, ref));
}

void UsbScanTransport::setMaxControlChunk(std::size_t bytes) noexcept {
    maxControlChunk_ = bytes;
}

Status UsbScanTransport::deviceControl(unsigned long code, void* input, std::size_t inputSize,
                                       void* output, std::size_t outputSize,
                                       unsigned long* bytesReturned, const char* context) {
    if (handle_ == nullptr) {
        return fail(ErrorCode::NotOpen, context);
    }
    if (cancelled_.load(std::memory_order_acquire)) {
        return fail(ErrorCode::Cancelled, context);
    }

    Overlapped ov;
    if (!ov.valid()) {
        return win32Error("CreateEventW");
    }

    DWORD returned = 0;
    const BOOL okNow = ::DeviceIoControl(
        static_cast<HANDLE>(handle_), code,
        input, static_cast<DWORD>(inputSize),
        output, static_cast<DWORD>(outputSize),
        &returned, ov.get());

    if (!okNow) {
        if (::GetLastError() != ERROR_IO_PENDING) {
            return win32Error(context);
        }
        if (!ov.wait(static_cast<HANDLE>(handle_), &returned)) {
            return win32Error(context);
        }
    }

    if (bytesReturned != nullptr) {
        *bytesReturned = returned;
    }
    return ok();
}

Status UsbScanTransport::controlTransfer(std::uint16_t address, Command command,
                                         std::byte* data, std::size_t length,
                                         bool directionIn) {
    if (length > (std::numeric_limits<unsigned>::max)()) {
        return fail(ErrorCode::InvalidArgument, "controlTransfer: predugacak transfer");
    }

    IO_BLOCK_EX block;
    ZeroMemory(&block, sizeof(block));
    block.uOffset = address;                       // wValue
    block.uLength = static_cast<unsigned>(length); // wLength
    block.pbyData = reinterpret_cast<PUCHAR>(data);
    block.uIndex = static_cast<unsigned>(command); // wIndex = KOMANDA
    block.bRequest = kVendorRequest;
    block.bmRequestType = directionIn ? kRequestTypeIn : kRequestTypeOut;
    block.fTransferDirectionIn = directionIn ? TRUE : FALSE;

    unsigned long returned = 0;
    const Status status = deviceControl(kIoctlSendUsbRequest, &block, sizeof(block),
                                        &block, sizeof(block), &returned,
                                        "IOCTL_SEND_USB_REQUEST");
    if (!status) {
        return status;
    }
    return ok();
}

Status UsbScanTransport::controlChunked(std::uint16_t address, Command command,
                                        std::byte* data, std::size_t length,
                                        bool directionIn) {
    const bool isRegisterSpace =
        command == Command::RegisterRead || command == Command::RegisterWrite;

    if (maxControlChunk_ == 0 || length <= maxControlChunk_) {
        return controlTransfer(address, command, data, length, directionIn);
    }

    // Deljenje pomera adresu, pa je validno samo tamo gde je wValue stvarno
    // adresa. Komandni blokovi se nikada ne dele - ako je neki veci od
    // limita, to je greska u pozivu, ne nesto sto smemo tiho zaobici.
    if (!isRegisterSpace) {
        return fail(ErrorCode::InvalidArgument,
                    "controlChunked: komandni blok premasuje limit");
    }

    std::size_t offset = 0;
    while (offset < length) {
        const std::size_t chunk = (std::min)(maxControlChunk_, length - offset);
        const std::size_t nextAddress = static_cast<std::size_t>(address) + offset;
        if (nextAddress > (std::numeric_limits<std::uint16_t>::max)()) {
            return fail(ErrorCode::InvalidArgument, "controlChunked: adresa je prekoracila 16 bita");
        }

        const Status status = controlTransfer(static_cast<std::uint16_t>(nextAddress),
                                              command, data + offset, chunk, directionIn);
        if (!status) {
            return status;
        }
        offset += chunk;
    }
    return ok();
}

Status UsbScanTransport::controlIn(std::uint16_t address, Command command,
                                   std::span<std::byte> buffer) {
    return controlChunked(address, command, buffer.data(), buffer.size(), true);
}

Status UsbScanTransport::controlOut(std::uint16_t address, Command command,
                                    std::span<const std::byte> buffer) {
    // IO_BLOCK_EX::pbyData nije const ni za upis; usbscan ga ne menja kada je
    // fTransferDirectionIn FALSE.
    auto* data = const_cast<std::byte*>(buffer.data());
    return controlChunked(address, command, data, buffer.size(), false);
}

Result<std::size_t> UsbScanTransport::bulkRead(std::span<std::byte> buffer) {
    if (handle_ == nullptr) {
        return fail(ErrorCode::NotOpen, "bulkRead");
    }
    if (cancelled_.load(std::memory_order_acquire)) {
        return fail(ErrorCode::Cancelled, "bulkRead");
    }

    Overlapped ov;
    if (!ov.valid()) {
        return win32Error("CreateEventW");
    }

    DWORD read = 0;
    const BOOL okNow = ::ReadFile(static_cast<HANDLE>(handle_), buffer.data(),
                                  static_cast<DWORD>(buffer.size()), &read, ov.get());
    if (!okNow) {
        if (::GetLastError() != ERROR_IO_PENDING) {
            return win32Error("ReadFile");
        }
        if (!ov.wait(static_cast<HANDLE>(handle_), &read)) {
            return win32Error("ReadFile");
        }
    }
    return static_cast<std::size_t>(read);
}

Status UsbScanTransport::bulkWrite(std::span<const std::byte> buffer) {
    if (handle_ == nullptr) {
        return fail(ErrorCode::NotOpen, "bulkWrite");
    }
    if (cancelled_.load(std::memory_order_acquire)) {
        return fail(ErrorCode::Cancelled, "bulkWrite");
    }

    Overlapped ov;
    if (!ov.valid()) {
        return win32Error("CreateEventW");
    }

    DWORD written = 0;
    const BOOL okNow = ::WriteFile(static_cast<HANDLE>(handle_), buffer.data(),
                                   static_cast<DWORD>(buffer.size()), &written, ov.get());
    if (!okNow) {
        if (::GetLastError() != ERROR_IO_PENDING) {
            return win32Error("WriteFile");
        }
        if (!ov.wait(static_cast<HANDLE>(handle_), &written)) {
            return win32Error("WriteFile");
        }
    }

    if (written != buffer.size()) {
        return fail(ErrorCode::ShortTransfer, "bulkWrite");
    }
    return ok();
}

Result<std::uint32_t> UsbScanTransport::waitEvent() {
    // Velicina event bafera dolazi sa uredjaja, ne pretpostavlja se.
    CHANNEL_INFO info;
    ZeroMemory(&info, sizeof(info));
    unsigned long returned = 0;
    const Status channel = deviceControl(kIoctlGetChannelAlign, &info, sizeof(info),
                                         &info, sizeof(info), &returned,
                                         "IOCTL_GET_CHANNEL_ALIGN_RQST");
    if (!channel) {
        return channel.error();
    }

    const std::size_t size = info.EventChannelSize != 0
                                 ? static_cast<std::size_t>(info.EventChannelSize)
                                 : sizeof(std::uint32_t);
    std::vector<std::uint8_t> buffer(size, 0);

    const Status status = deviceControl(kIoctlWaitOnDeviceEvent, nullptr, 0,
                                        buffer.data(), buffer.size(), &returned,
                                        "IOCTL_WAIT_ON_DEVICE_EVENT");
    if (!status) {
        return status.error();
    }

    // Maska dolazi little-endian; mapiranje maska -> dugme radi sloj iznad.
    std::uint32_t mask = 0;
    const std::size_t count = (std::min)(static_cast<std::size_t>(returned), sizeof(mask));
    for (std::size_t i = 0; i < count; ++i) {
        mask |= static_cast<std::uint32_t>(buffer[i]) << (8 * i);
    }
    return mask;
}

Status UsbScanTransport::resetPipe(PipeKind pipe) {
    PIPE_TYPE type = toPipeType(pipe);
    unsigned long returned = 0;
    return deviceControl(kIoctlResetPipe, &type, sizeof(type),
                         nullptr, 0, &returned, "IOCTL_RESET_PIPE");
}

Status UsbScanTransport::setTimeouts(const Timeouts& timeouts) {
    USBSCAN_TIMEOUT value;
    ZeroMemory(&value, sizeof(value));
    value.TimeoutRead = toSeconds(timeouts.read);
    value.TimeoutWrite = toSeconds(timeouts.write);
    value.TimeoutEvent = toSeconds(timeouts.event);

    unsigned long returned = 0;
    const Status status = deviceControl(kIoctlSetTimeout, &value, sizeof(value),
                                        nullptr, 0, &returned, "IOCTL_SET_TIMEOUT");
    if (status) {
        timeouts_ = timeouts;
    }
    return status;
}

Result<PipeConfiguration> UsbScanTransport::pipeConfiguration() {
    USBSCAN_PIPE_CONFIGURATION raw;
    ZeroMemory(&raw, sizeof(raw));
    unsigned long returned = 0;

    const Status status = deviceControl(kIoctlGetPipeConfiguration, nullptr, 0,
                                        &raw, sizeof(raw), &returned,
                                        "IOCTL_GET_PIPE_CONFIGURATION");
    if (!status) {
        return status.error();
    }

    PipeConfiguration config;
    const ULONG count = (std::min)(raw.NumberOfPipes, static_cast<ULONG>(MAX_NUM_PIPES));
    for (ULONG i = 0; i < count; ++i) {
        const USBSCAN_PIPE_INFORMATION& pipe = raw.PipeInfo[i];
        switch (pipe.PipeType) {
            case USBSCAN_PIPE_BULK:
                if ((pipe.EndpointAddress & BULKIN_FLAG) != 0) {
                    config.bulkIn = pipe.EndpointAddress;
                } else {
                    config.bulkOut = pipe.EndpointAddress;
                }
                break;
            case USBSCAN_PIPE_INTERRUPT:
                config.interrupt = pipe.EndpointAddress;
                config.hasInterrupt = true;
                break;
            default:
                break;
        }
    }
    return config;
}

Result<UsbScanTransport::DeviceIdentity> UsbScanTransport::identity() {
    DEVICE_DESCRIPTOR raw;
    ZeroMemory(&raw, sizeof(raw));
    unsigned long returned = 0;

    const Status status = deviceControl(kIoctlGetDeviceDescriptor, &raw, sizeof(raw),
                                        &raw, sizeof(raw), &returned,
                                        "IOCTL_GET_DEVICE_DESCRIPTOR");
    if (!status) {
        return status.error();
    }

    DeviceIdentity id;
    id.vendorId = raw.usVendorId;
    id.productId = raw.usProductId;
    id.bcdDevice = raw.usBcdDevice;
    return id;
}

void UsbScanTransport::cancel() noexcept {
    cancelled_.store(true, std::memory_order_release);
    if (handle_ != nullptr) {
        // CancelIoEx prekida i transfere koje je pokrenuo drugi thread; to je
        // jedini nacin da cancel usred scana stvarno radi.
        ::CancelIoEx(static_cast<HANDLE>(handle_), nullptr);
    }
}

Status UsbScanTransport::reopen() {
    if (ref_.kind() == DeviceRef::Kind::ExistingHandle) {
        // Handle je tudji; ne mozemo ga ponovo otvoriti. Vlasnik (STI) mora
        // ponovo inicijalizovati uredjaj.
        return fail(ErrorCode::InvalidState, "reopen: handle je pozajmljen");
    }

    closeHandle();
    cancelled_.store(false, std::memory_order_release);

    auto reopened = open(ref_);
    if (!reopened) {
        return reopened.error();
    }

    // Preuzmi novi handle, a staru instancu pusti da nestane bez zatvaranja.
    UsbScanTransport& fresh = *reopened.value();
    handle_ = fresh.handle_;
    ownership_ = fresh.ownership_;
    fresh.handle_ = nullptr;

    if (timeouts_.read.count() != 0 || timeouts_.write.count() != 0 ||
        timeouts_.event.count() != 0) {
        const Status restored = setTimeouts(timeouts_);
        if (!restored) {
            return restored;
        }
    }
    return ok();
}

// ---------------------------------------------- UsbScanTransportProvider ----

Result<std::unique_ptr<ITransport>> UsbScanTransportProvider::create(const DeviceRef& ref) {
    auto transport = UsbScanTransport::open(ref);
    if (!transport) {
        return transport.error();
    }
    return std::unique_ptr<ITransport>(std::move(transport).value().release());
}

}  // namespace g2710
