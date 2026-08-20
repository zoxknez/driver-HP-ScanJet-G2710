// Dokazuje da test seam radi IZNAD transporta.
//
// SimTransport je C++ objekat i ne moze se predstaviti laznim Win32 handle-om
// koji bi prosao kroz ReadFile / DeviceIoControl. Zato se zamenjuje provider,
// ne handle - a ovi testovi to zakljucavaju.

#include "transport/ITransportProvider.h"
#include "transport/UsbScanTransport.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace g2710;

namespace {

// Minimalni lazni transport; pun simulator dolazi u G2710-3.
class StubTransport final : public ITransport {
public:
    Status controlIn(std::uint16_t, Command, std::span<std::byte>) override { return ok(); }
    Status controlOut(std::uint16_t, Command, std::span<const std::byte>) override { return ok(); }
    std::size_t maxControlChunk() const noexcept override { return 0; }

    Result<std::size_t> bulkRead(std::span<std::byte> buffer) override { return buffer.size(); }
    Status bulkWrite(std::span<const std::byte>) override { return ok(); }

    Result<std::uint32_t> waitEvent() override { return 0u; }

    Status resetPipe(PipeKind) override { return ok(); }
    Status setTimeouts(const Timeouts&) override { return ok(); }
    Result<PipeConfiguration> pipeConfiguration() override { return PipeConfiguration{}; }
    Result<DeviceIdentity> identity() override { return DeviceIdentity{}; }

    void cancel() noexcept override {}
    Status reopen() override { return ok(); }
    bool isOpen() const noexcept override { return true; }
    const char* name() const noexcept override { return "stub"; }
};

class StubProvider final : public ITransportProvider {
public:
    Result<std::unique_ptr<ITransport>> create(const DeviceRef&) override {
        ++created;
        return std::unique_ptr<ITransport>(std::make_unique<StubTransport>());
    }
    const char* name() const noexcept override { return "stub"; }

    int created = 0;
};

}  // namespace

TEST(TransportProvider, DefaultsToProductionUsbScan) {
    EXPECT_STREQ(TransportProvider::activeProviderName(), "usbscan");
}

TEST(TransportProvider, ScopedOverrideRedirectsCreation) {
    auto provider = std::make_unique<StubProvider>();
    StubProvider* raw = provider.get();

    {
        TransportProvider::ScopedTestProvider guard{std::move(provider)};
        EXPECT_STREQ(TransportProvider::activeProviderName(), "stub");

        auto transport = TransportProvider::create(DeviceRef::defaultUsbScan());
        ASSERT_TRUE(transport.hasValue());
        EXPECT_STREQ(transport.value()->name(), "stub");
        EXPECT_EQ(raw->created, 1);
    }

    // RAII je namerno jedini nacin zamene: override koji procuri iz jednog
    // testa u drugi je tesko uocljiva greska.
    EXPECT_STREQ(TransportProvider::activeProviderName(), "usbscan");
}

TEST(TransportProvider, OverridesNestAndRestoreInOrder) {
    auto outer = std::make_unique<StubProvider>();
    {
        TransportProvider::ScopedTestProvider outerGuard{std::move(outer)};
        EXPECT_STREQ(TransportProvider::activeProviderName(), "stub");
        {
            TransportProvider::ScopedTestProvider innerGuard{
                std::make_unique<UsbScanTransportProvider>()};
            EXPECT_STREQ(TransportProvider::activeProviderName(), "usbscan");
        }
        EXPECT_STREQ(TransportProvider::activeProviderName(), "stub");
    }
    EXPECT_STREQ(TransportProvider::activeProviderName(), "usbscan");
}

TEST(DeviceRef, DefaultTargetsUsbscanZero) {
    const DeviceRef ref = DeviceRef::defaultUsbScan();
    EXPECT_EQ(ref.kind(), DeviceRef::Kind::DevicePath);
    EXPECT_EQ(ref.path(), L"\\\\.\\Usbscan0");
    EXPECT_EQ(ref.ownership(), HandleOwnership::Owned);
}

TEST(DeviceRef, PortNameIsDocumentedWiaPath) {
    // WIA ide preko GetMyDevicePortName + sopstveni CreateFile.
    const DeviceRef ref = DeviceRef::portName(L"\\\\.\\Usbscan3");
    EXPECT_EQ(ref.kind(), DeviceRef::Kind::PortName);
    EXPECT_EQ(ref.ownership(), HandleOwnership::Owned);
}

TEST(DeviceRef, BorrowedHandleIsNotOwned) {
    // GetMyDeviceHandle je optimization candidate; ako se ikada aktivira,
    // handle pripada STI-ju i mi ga NE SMEMO zatvoriti.
    int dummy = 0;
    const DeviceRef ref = DeviceRef::existingHandle(&dummy, HandleOwnership::Borrowed);
    EXPECT_EQ(ref.kind(), DeviceRef::Kind::ExistingHandle);
    EXPECT_EQ(ref.ownership(), HandleOwnership::Borrowed);
    EXPECT_EQ(ref.handle(), &dummy);
}

TEST(UsbScanTransport, RejectsEmptyHandle) {
    auto transport = UsbScanTransport::open(
        DeviceRef::existingHandle(nullptr, HandleOwnership::Borrowed));
    ASSERT_FALSE(transport.hasValue());
    EXPECT_EQ(transport.error().code, ErrorCode::InvalidArgument);
}

TEST(UsbScanTransport, RejectsEmptyPath) {
    auto transport = UsbScanTransport::open(DeviceRef::devicePath(L""));
    ASSERT_FALSE(transport.hasValue());
    EXPECT_EQ(transport.error().code, ErrorCode::InvalidArgument);
}

TEST(Command, EveryCommandHasAName) {
    const Command all[] = {
        Command::RegisterWrite, Command::RegisterRead, Command::Eeprom,
        Command::DmaEnableRead, Command::DmaEnableWrite, Command::DmaCancel,
        Command::DmaOpType, Command::ChipsetReset,
    };
    for (const Command command : all) {
        EXPECT_STRNE(toString(command), "Unknown");
    }
}

TEST(Command, RegisterReadAndWriteUseDifferentIndices) {
    // Asimetrija je namerna i dolazi iz reference; ako je neko "popravi",
    // registri se tiho korumpiraju.
    EXPECT_EQ(static_cast<int>(Command::RegisterWrite), 0x0000);
    EXPECT_EQ(static_cast<int>(Command::RegisterRead), 0x0100);
    EXPECT_NE(Command::RegisterRead, Command::RegisterWrite);
}
