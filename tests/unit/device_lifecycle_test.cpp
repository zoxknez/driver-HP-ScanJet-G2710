// Zivotni ciklus uredjaja kroz G2710Device.
//
// Provera identiteta je ovde, a ne u CLI-ju, upravo zato da bi vazila za sve
// klijente. Klijent koji je zaobidje slao bi vendor komande uredjaju koji nije
// nas - a \\.\Usbscan0 je deljeno ime.

#include "G2710Profile.generated.h"
#include <windows.h>

#include "SimTransport.h"
#include "device/G2710Device.h"
#include "transport/ITransportProvider.h"

#include <gtest/gtest.h>

#include <memory>

using namespace g2710;
using namespace std::chrono_literals;

namespace {

// Provider koji vraca simulator i zadrzava pokazivac na njega, da test moze
// da menja simulirani hardver posle otvaranja.
class SimProvider final : public ITransportProvider {
public:
    Result<std::unique_ptr<ITransport>> create(const DeviceRef&) override {
        auto transport = std::make_unique<sim::SimTransport>();
        last = transport.get();
        return std::unique_ptr<ITransport>(std::move(transport));
    }
    const char* name() const noexcept override { return "sim"; }

    sim::SimTransport* last = nullptr;
};

class Lifecycle : public ::testing::Test {
protected:
    SimProvider* provider = nullptr;
    std::unique_ptr<TransportProvider::ScopedTestProvider> guard;

    void SetUp() override {
        auto owned = std::make_unique<SimProvider>();
        provider = owned.get();
        guard = std::make_unique<TransportProvider::ScopedTestProvider>(std::move(owned));
    }

    void TearDown() override { guard.reset(); }

    static DeviceOptions options(const char* client = "test") {
        DeviceOptions opts;
        opts.safety = SafetyGate{SafetyLevel::FullScan};
        opts.clientName = client;
        opts.acquireTimeout = 200ms;
        return opts;
    }

    Result<std::unique_ptr<G2710Device>> openDevice(const char* client = "test") {
        return G2710Device::open(DeviceRef::defaultUsbScan(), options(client));
    }
};

}  // namespace

// Simulator NIJE deljeni skener.
//
// Fizicki uredjaj je jedan i o njega se zaista otimaju WIA servis, TWAIN i
// aplikacija - zato arbitraza i postoji. Simulator zivi unutar procesa koji ga
// je napravio; dva procesa koja ga koriste ne dele nista.
//
// Prvo su oba dobijala isti kljuc. Posledica: dva test projekta pokrenuta
// paralelno otimala su se o istu bravu i CEO paket testova je pao na istek
// roka, iako svaki projekat sam prolazi. Ista greska bi pogodila i coveka koji
// drzi otvorenu aplikaciju u rezimu simulatora dok pokrece wizard.
TEST(DeviceArbitration, TheSimulatorIsNotTheSharedScanner) {
    TransportProvider::ScopedTestProvider guard{std::make_unique<sim::SimTransportProvider>()};

    DeviceOptions options;
    options.safety = SafetyGate{SafetyLevel::ReadOnly};
    options.clientName = "kljuc-test";

    auto device = G2710Device::open(DeviceRef::defaultUsbScan(), options);
    ASSERT_TRUE(device);

    // Kljuc mora nositi id procesa - inace dva procesa dele bravu koju nemaju
    // zasto da dele.
    const std::wstring name = device.value()->arbiterMutexName();
    const std::wstring expected = L"-sim-" + std::to_wstring(::GetCurrentProcessId());

    EXPECT_NE(std::wstring::npos, name.find(expected))
        << "simulator koristi kljuc fizickog uredjaja";

    // A mehanizam ostaje isti: i dalje Global\, i dalje ista arbitraza.
    EXPECT_EQ(0u, name.find(L"Global\\"));
    EXPECT_EQ(ArbiterScope::Global, device.value()->arbiterScope());
}


TEST_F(Lifecycle, OpenDoesNotClaimTheDeviceYet) {
    // Otvaranje i zauzimanje su odvojeni koraci: prvo moze uspeti a drugo ne.
    auto device = openDevice();
    ASSERT_TRUE(device.hasValue());

    EXPECT_EQ(device.value()->state(), DeviceState::Opened);
    EXPECT_FALSE(device.value()->isIdentified());
}

TEST_F(Lifecycle, IdentifyAcceptsTheRealDevice) {
    auto device = openDevice();
    ASSERT_TRUE(device.hasValue());

    ASSERT_TRUE(device.value()->identify().hasValue());
    EXPECT_EQ(device.value()->state(), DeviceState::Identified);
    EXPECT_EQ(device.value()->identity().vendorId, profile::kUsbVendorId);
    EXPECT_EQ(device.value()->identity().productId, profile::kUsbProductId);
}

TEST_F(Lifecycle, IdentifyRefusesSomebodyElsesScanner) {
    // Na razvojnoj masini je na \\.\Usbscan0 bio HP LaserJet MFP M139-M142.
    // Njegov identitet je 03F0:0372 - isti proizvodjac, drugi uredjaj.
    auto device = openDevice();
    ASSERT_TRUE(device.hasValue());

    provider->last->setIdentity(DeviceIdentity{0x03F0, 0x0372, 0x0100});

    const Status status = device.value()->identify();
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::DeviceNotFound);
    EXPECT_FALSE(device.value()->isIdentified());
    EXPECT_EQ(device.value()->state(), DeviceState::Opened)
        << "presao je u Identified iako uredjaj nije nas";
}

TEST_F(Lifecycle, BeginRefusesWithoutIdentify) {
    // Zauzimanje nepotvrdjenog uredjaja znacilo bi da sledeca komanda ide u
    // nepoznato.
    auto device = openDevice();
    ASSERT_TRUE(device.hasValue());

    const Status status = device.value()->begin();
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidState);
}

TEST_F(Lifecycle, BeginTakesExclusiveOwnership) {
    auto device = openDevice("wia");
    ASSERT_TRUE(device.hasValue());
    ASSERT_TRUE(device.value()->identify().hasValue());
    ASSERT_TRUE(device.value()->begin().hasValue());

    EXPECT_EQ(device.value()->state(), DeviceState::Idle);
    EXPECT_EQ(device.value()->currentOwner(), "wia");
    EXPECT_EQ(device.value()->arbiterScope(), ArbiterScope::Global)
        << "brava je degradirana - medju-sesijsko iskljucivanje ne radi";
}

TEST_F(Lifecycle, EndReleasesOwnership) {
    auto device = openDevice("app");
    ASSERT_TRUE(device.hasValue());
    ASSERT_TRUE(device.value()->identify().hasValue());
    ASSERT_TRUE(device.value()->begin().hasValue());

    ASSERT_TRUE(device.value()->end().hasValue());
    EXPECT_EQ(device.value()->state(), DeviceState::Identified);
    EXPECT_TRUE(device.value()->currentOwner().empty());
}

TEST_F(Lifecycle, SensorReadsGoThroughTheSameRegistersAsRealHardware) {
    auto device = openDevice();
    ASSERT_TRUE(device.hasValue());
    ASSERT_TRUE(device.value()->identify().hasValue());
    ASSERT_TRUE(device.value()->begin().hasValue());

    auto home = device.value()->isHeadAtHome();
    ASSERT_TRUE(home.hasValue());
    EXPECT_TRUE(home.value());

    provider->last->motor().teleportTo(4000);
    home = device.value()->isHeadAtHome();
    ASSERT_TRUE(home.hasValue());
    EXPECT_FALSE(home.value());
}

// --- gubitak veze i oporavak -------------------------------------------------

TEST_F(Lifecycle, TransportLossFromAReadDropsStateAndPosition) {
    auto device = openDevice();
    ASSERT_TRUE(device.hasValue());
    ASSERT_TRUE(device.value()->identify().hasValue());
    ASSERT_TRUE(device.value()->begin().hasValue());

    device.value()->headPosition().setKnown(1234);
    provider->last->faults().injectPermanent(sim::TransferKind::Any,
                                             ErrorCode::TransportLost);

    const auto result = device.value()->isHeadAtHome();
    ASSERT_FALSE(result.hasValue());

    EXPECT_EQ(device.value()->state(), DeviceState::TransportLost);
    EXPECT_FALSE(device.value()->headPosition().isKnown())
        << "pozicija je ostala 'poznata' posle gubitka veze";
}

TEST_F(Lifecycle, RecoveryReopensButDoesNotRestorePosition) {
    // Sustina: ponovna veza ne govori nista o tome gde je glava stala.
    auto device = openDevice();
    ASSERT_TRUE(device.hasValue());
    ASSERT_TRUE(device.value()->identify().hasValue());
    ASSERT_TRUE(device.value()->begin().hasValue());
    device.value()->headPosition().setKnown(1234);

    provider->last->faults().injectPermanent(sim::TransferKind::Any,
                                             ErrorCode::TransportLost);
    (void)device.value()->isHeadAtHome();
    ASSERT_EQ(device.value()->state(), DeviceState::TransportLost);

    provider->last->faults().clear();
    ASSERT_TRUE(device.value()->recoverFromTransportLoss().hasValue());

    EXPECT_EQ(device.value()->state(), DeviceState::Opened);
    EXPECT_FALSE(device.value()->headPosition().isKnown())
        << "oporavak je vratio poziciju bez HOME-a";
    EXPECT_FALSE(device.value()->isIdentified())
        << "identitet se mora ponovo potvrditi posle ponovnog otvaranja";
}

TEST_F(Lifecycle, RecoveryIsRefusedWhenNothingWasLost) {
    auto device = openDevice();
    ASSERT_TRUE(device.hasValue());
    ASSERT_TRUE(device.value()->identify().hasValue());

    const Status status = device.value()->recoverFromTransportLoss();
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidState);
}

TEST_F(Lifecycle, CancellationTokenIsSharedWithTheTransport) {
    auto device = openDevice();
    ASSERT_TRUE(device.hasValue());
    ASSERT_TRUE(device.value()->identify().hasValue());
    ASSERT_TRUE(device.value()->begin().hasValue());

    device.value()->cancel();
    EXPECT_TRUE(device.value()->cancellation().isCancelled());

    // Transport je takodje otkazan; naredna operacija to prijavljuje.
    const auto result = device.value()->isHeadAtHome();
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

TEST_F(Lifecycle, SafetyCeilingIsCarriedIntoTheDevice) {
    DeviceOptions restricted;
    restricted.safety = SafetyGate{SafetyLevel::ReadOnly};
    restricted.clientName = "probe";
    restricted.acquireTimeout = 200ms;

    auto device = G2710Device::open(DeviceRef::defaultUsbScan(), restricted);
    ASSERT_TRUE(device.hasValue());

    // Nivo 1 prolazi.
    EXPECT_TRUE(device.value()->identify().hasValue());
    EXPECT_TRUE(device.value()->isHeadAtHome().hasValue());

    EXPECT_EQ(device.value()->safety().effective(), SafetyLevel::ReadOnly);
}
