#include "TwainDataSource.h"
#include "SimTransport.h"
#include "transport/ITransportProvider.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace {

TW_UINT16 call(TW_UINT32 dg, TW_UINT16 dat, TW_UINT16 msg, TW_MEMREF data = nullptr) {
    static auto provider = std::make_unique<g2710::TransportProvider::ScopedTestProvider>(
        std::make_unique<g2710::sim::SimTransportProvider>());
    (void)provider;
    return G2710TwainEntry(nullptr, nullptr, dg, dat, msg, data);
}

TEST(TwainReleasePolicy, DoesNotTurnSimulatorQualificationIntoAnAdvertisedScan) {
    // Ovaj binarni fajl NAMERNO nema G2710_TWAIN_ALLOW_UNQUALIFIED. Simulator
    // postoji, ali produkcioni DS i dalje mora odbiti prenos dok H8 ne potvrdi
    // rezoluciju. Tako harness ne moze slucajno prosiriti proizvodnu ponudu.
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS));

    std::array<TW_UINT8, 4096> buffer{};
    TW_IMAGEMEMXFER transfer{};
    transfer.Memory.TheMem = buffer.data();
    transfer.Memory.Length = static_cast<TW_UINT32>(buffer.size());
    EXPECT_EQ(TWRC_FAILURE, call(DG_IMAGE, DAT_IMAGEMEMXFER, MSG_GET, &transfer));
    TW_STATUS status{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_STATUS, MSG_GET, &status));
    EXPECT_EQ(TWCC_OPERATIONERROR, status.ConditionCode);

    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}

}  // namespace
