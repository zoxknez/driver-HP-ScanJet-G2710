#include "device/DeviceArbiter.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace g2710;
using namespace std::chrono_literals;

namespace {

// Svaki test dobija svoj kljuc da paralelno pokretanje ne bi delilo bravu.
std::string uniqueKey(const char* label) {
    static std::atomic<int> counter{0};
    return std::string("test-") + label + "-" +
           std::to_string(counter.fetch_add(1)) + "-" +
           std::to_string(::GetCurrentProcessId());
}

}  // namespace

TEST(DeviceArbiter, InitializeUsesGlobalNamespace) {
    DeviceArbiter arbiter{uniqueKey("scope")};
    ASSERT_TRUE(arbiter.initialize().hasValue());

    // Ako ovo padne na Local, zakljucavanje NE stiti od WIA servisa u
    // Session 0. Pad mora biti vidljiv, nikada tih - zato je scope() javan.
    EXPECT_EQ(arbiter.scope(), ArbiterScope::Global)
        << "Global\\ nije dostupan (nedostaje SeCreateGlobalPrivilege?); "
           "medju-sesijska arbitraza je degradirana na " << toString(arbiter.scope());
    EXPECT_EQ(arbiter.mutexName().find(L"Global\\"), 0u);
}

TEST(DeviceArbiter, EmptyKeyIsRejected) {
    DeviceArbiter arbiter{""};
    const Status status = arbiter.initialize();
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

TEST(DeviceArbiter, AcquireBeforeInitializeFails) {
    DeviceArbiter arbiter{uniqueKey("uninit")};
    auto session = arbiter.acquireData(10ms, "cli");
    ASSERT_FALSE(session.hasValue());
    EXPECT_EQ(session.error().code, ErrorCode::InvalidState);
}

TEST(DeviceArbiter, DataSessionIsExclusiveAcrossThreads) {
    DeviceArbiter arbiter{uniqueKey("exclusive")};
    ASSERT_TRUE(arbiter.initialize().hasValue());

    auto held = arbiter.acquireData(1s, "wia");
    ASSERT_TRUE(held.hasValue());

    // Mutex je rekurzivan po thread-u, pa iskljucivost mora da se proverava
    // iz drugog thread-a - inace bi test lazno prosao.
    ErrorCode observed = ErrorCode::Ok;
    std::thread contender([&] {
        auto second = arbiter.acquireData(50ms, "twain");
        observed = second.hasValue() ? ErrorCode::Ok : second.error().code;
    });
    contender.join();

    EXPECT_EQ(observed, ErrorCode::Busy)
        << "drugi klijent je dobio DataSession dok je prvi drzi";
}

TEST(DeviceArbiter, ReleaseLetsNextClientIn) {
    DeviceArbiter arbiter{uniqueKey("release")};
    ASSERT_TRUE(arbiter.initialize().hasValue());

    {
        auto first = arbiter.acquireData(1s, "app");
        ASSERT_TRUE(first.hasValue());
        EXPECT_TRUE(first.value().held());
    }  // release kroz destruktor

    bool acquired = false;
    std::thread next([&] {
        auto second = arbiter.acquireData(1s, "twain");
        acquired = second.hasValue();
    });
    next.join();

    EXPECT_TRUE(acquired);
}

TEST(DeviceArbiter, ReportsOwnerSoDiagnosticsAreReadable) {
    // Poenta je da prijatelj vidi "uredjaj koristi WIA", a ne goli
    // ERROR_SHARING_VIOLATION.
    DeviceArbiter arbiter{uniqueKey("owner")};
    ASSERT_TRUE(arbiter.initialize().hasValue());
    ASSERT_TRUE(arbiter.ownerChannelAvailable());

    auto held = arbiter.acquireData(1s, "wia");
    ASSERT_TRUE(held.hasValue());
    EXPECT_EQ(arbiter.currentOwner(), "wia");

    held.value().release();
    EXPECT_TRUE(arbiter.currentOwner().empty());
}

TEST(DeviceArbiter, OwnerChannelDegradesWithoutBreakingArbitration) {
    // CreateMutexW sa Global\ prolazi obicnom korisniku, ali CreateFileMappingW
    // sa Global\ pada sa ERROR_ACCESS_DENIED - SeCreateGlobalPrivilege se trazi
    // za SECTION objekte, ne za mutekse.
    //
    // Kljucno: to sme da spusti SAMO dijagnosticki kanal. Brava, koja nosi
    // korektnost arbitraze, mora ostati Global. Da su ovo dva imena istog
    // polja, neko bi "popravio" praznu dijagnostiku spustanjem cele arbitraze
    // na Local i tiho razbio medju-sesijsko iskljucivanje.
    DeviceArbiter arbiter{uniqueKey("degrade")};
    ASSERT_TRUE(arbiter.initialize().hasValue());

    EXPECT_EQ(arbiter.scope(), ArbiterScope::Global)
        << "brava je degradirana - medju-sesijsko iskljucivanje ne radi";

    if (arbiter.ownerChannelScope() == ArbiterScope::Local) {
        // Ocekivano za neelevirani proces. Arbitraza i dalje mora raditi.
        auto held = arbiter.acquireData(1s, "app");
        ASSERT_TRUE(held.hasValue());

        ErrorCode observed = ErrorCode::Ok;
        std::thread contender([&] {
            auto second = arbiter.acquireData(50ms, "twain");
            observed = second.hasValue() ? ErrorCode::Ok : second.error().code;
        });
        contender.join();
        EXPECT_EQ(observed, ErrorCode::Busy);
    }
}

TEST(DeviceArbiter, StatusSessionNeverBlocksDataSession) {
    // WIA servis dugo drzi status-mode instancu dok TWAIN hoce data transfer.
    // Status ne sme uzimati bravu.
    DeviceArbiter arbiter{uniqueKey("status")};
    ASSERT_TRUE(arbiter.initialize().hasValue());

    const StatusSession status = arbiter.acquireStatus();
    EXPECT_TRUE(status.valid());

    auto data = arbiter.acquireData(50ms, "twain");
    EXPECT_TRUE(data.hasValue())
        << "StatusSession je blokirala DataSession";
}

TEST(DeviceArbiter, MovedSessionReleasesExactlyOnce) {
    DeviceArbiter arbiter{uniqueKey("move")};
    ASSERT_TRUE(arbiter.initialize().hasValue());

    auto acquired = arbiter.acquireData(1s, "cli");
    ASSERT_TRUE(acquired.hasValue());

    {
        DataSession moved = std::move(acquired).value();
        EXPECT_TRUE(moved.held());
    }

    bool reacquired = false;
    std::thread next([&] {
        auto again = arbiter.acquireData(1s, "cli2");
        reacquired = again.hasValue();
    });
    next.join();
    EXPECT_TRUE(reacquired);
}
