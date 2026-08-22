#include "TwainDataSource.h"
#include "SimTransport.h"
#include "transport/ITransportProvider.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <thread>

namespace {
TW_UINT16 call(TW_UINT32 dg, TW_UINT16 dat, TW_UINT16 msg, TW_MEMREF data = nullptr) {
    // Harness poziva potpuno isti DS kod, ali mu umesto fizickog USB uredjaja
    // daje simulator. To dokazuje stvarni Core transfer, ne sinteticki bajt.
    static auto provider = std::make_unique<g2710::TransportProvider::ScopedTestProvider>(
        std::make_unique<g2710::sim::SimTransportProvider>());
    (void)provider;
    return G2710TwainEntry(nullptr, nullptr, dg, dat, msg, data);
}

TW_HANDLE TW_CALLINGSTYLE allocate(TW_UINT32 size) { return std::malloc(size); }
void TW_CALLINGSTYLE release(TW_HANDLE handle) { std::free(handle); }
TW_MEMREF TW_CALLINGSTYLE lock(TW_HANDLE handle) { return handle; }
void TW_CALLINGSTYLE unlock(TW_HANDLE) {}

TEST(TwainLifecycle, IdentityAndExactStateMachine) {
    TW_IDENTITY id{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_GET, &id));
    EXPECT_STREQ("HP ScanJet G2710", id.ProductName);
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_GETFIRST, &id));
    EXPECT_STREQ("HP ScanJet G2710", id.ProductName);
    EXPECT_EQ(TWRC_ENDOFLIST, call(DG_CONTROL, DAT_IDENTITY, MSG_GETNEXT, &id));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    EXPECT_EQ(TWRC_FAILURE, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    TW_STATUS status{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_STATUS, MSG_GET, &status));
    EXPECT_EQ(TWCC_SEQERROR, status.ConditionCode);
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS));
    TW_IMAGEINFO info{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_IMAGE, DAT_IMAGEINFO, MSG_GET, &info));
    EXPECT_EQ(64, info.ImageWidth);
    EXPECT_EQ(8, info.ImageLength);
    EXPECT_EQ(TWPT_RGB, info.PixelType);
    TW_IMAGELAYOUT layout{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_IMAGE, DAT_IMAGELAYOUT, MSG_GET, &layout));
    EXPECT_EQ(1u, layout.DocumentNumber);
    EXPECT_GT(layout.Frame.Right.Frac, 0u);
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}

TEST(TwainAbi, ExportedEntryUsesTheOfficialFiveArgumentContract) {
    TW_IDENTITY id{};
    // Poziv ide kroz deklaraciju iz zvanicnog twain.h. Da je export ostao na
    // pogresnom sest-argumentnom ABI-ju, ovaj test bi se srusio ili pokvario
    // stek na x86 pre nego sto bi DSM uopste stigao da prijavi problem.
    EXPECT_EQ(TWRC_SUCCESS, DS_Entry(nullptr, DG_CONTROL, DAT_IDENTITY, MSG_GETFIRST, &id));
    EXPECT_STREQ("HP ScanJet G2710", id.ProductName);
}

TEST(TwainLifecycle, ConcurrentOpenHasExactlyOneWinner) {
    TW_IDENTITY first{}, second{};
    TW_UINT16 a = TWRC_FAILURE, b = TWRC_FAILURE;
    std::thread left([&] { a = call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &first); });
    std::thread right([&] { b = call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &second); });
    left.join(); right.join();
    EXPECT_EQ(1, static_cast<int>(a == TWRC_SUCCESS) + static_cast<int>(b == TWRC_SUCCESS));
    TW_IDENTITY close{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &close));
}

TEST(TwainLifecycle, MemoryTransferHoldsThenReleasesGlobalDataSession) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS));
    std::array<TW_UINT8, 8192> pixels{};
    TW_IMAGEMEMXFER transfer{};
    transfer.Memory.TheMem = pixels.data(); transfer.Memory.Length = static_cast<TW_UINT32>(pixels.size());
    EXPECT_EQ(TWRC_XFERDONE, call(DG_IMAGE, DAT_IMAGEMEMXFER, MSG_GET, &transfer));
    EXPECT_GT(transfer.BytesWritten, 0u);
    EXPECT_LE(transfer.BytesWritten, pixels.size());
    EXPECT_GT(transfer.Rows, 0u);
    EXPECT_EQ(transfer.Rows * transfer.BytesPerRow, transfer.BytesWritten);
    EXPECT_NE(0, pixels[0]);
    TW_PENDINGXFERS pending{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_PENDINGXFERS, MSG_ENDXFER, &pending));
    EXPECT_EQ(0, pending.Count);
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}

TEST(TwainLifecycle, ResetAfterTransferReleasesTheDataSession) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS));
    std::array<TW_UINT8, 4096> pixels{};
    TW_IMAGEMEMXFER transfer{};
    transfer.Memory.TheMem = pixels.data(); transfer.Memory.Length = static_cast<TW_UINT32>(pixels.size());
    ASSERT_EQ(TWRC_XFERDONE, call(DG_IMAGE, DAT_IMAGEMEMXFER, MSG_GET, &transfer));
    TW_PENDINGXFERS pending{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_PENDINGXFERS, MSG_RESET, &pending));
    EXPECT_EQ(0, pending.Count);
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}

TEST(TwainLifecycle, NativeTransferUsesDsmMemoryAndReturnsAnRgbDib) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    TW_ENTRYPOINT entry{};
    entry.Size = sizeof(entry);
    entry.DSM_MemAllocate = allocate; entry.DSM_MemFree = release;
    entry.DSM_MemLock = lock; entry.DSM_MemUnlock = unlock;
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_ENTRYPOINT, MSG_GET, &entry));
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS));
    TW_HANDLE image = nullptr;
    ASSERT_EQ(TWRC_XFERDONE, call(DG_IMAGE, DAT_IMAGENATIVEXFER, MSG_GET, &image));
    ASSERT_NE(nullptr, image);
    const auto* header = static_cast<const BITMAPINFOHEADER*>(lock(image));
    ASSERT_NE(nullptr, header);
    EXPECT_EQ(sizeof(BITMAPINFOHEADER), header->biSize);
    EXPECT_EQ(64, header->biWidth);
    EXPECT_EQ(8, header->biHeight);
    EXPECT_EQ(24, header->biBitCount);
    EXPECT_EQ(BI_RGB, header->biCompression);
    unlock(image); release(image);
    TW_PENDINGXFERS pending{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_PENDINGXFERS, MSG_ENDXFER, &pending));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}

TEST(TwainCapabilities, UsesTheDsmAllocatorAndDoesNotAdvertiseUnqualifiedDpi) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    TW_ENTRYPOINT entry{};
    entry.Size = sizeof(entry);
    entry.DSM_MemAllocate = allocate; entry.DSM_MemFree = release;
    entry.DSM_MemLock = lock; entry.DSM_MemUnlock = unlock;
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_ENTRYPOINT, MSG_GET, &entry));

    TW_CAPABILITY mechanism{};
    mechanism.Cap = ICAP_XFERMECH;
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_CAPABILITY, MSG_GET, &mechanism));
    ASSERT_EQ(TWON_ENUMERATION, mechanism.ConType);
    auto* value = static_cast<pTW_ENUMERATION>(lock(mechanism.hContainer));
    ASSERT_NE(nullptr, value);
    EXPECT_EQ(TWTY_UINT16, value->ItemType);
    ASSERT_EQ(2u, value->NumItems);
    EXPECT_EQ(1u, value->CurrentIndex);
    const auto* mechanisms = reinterpret_cast<const TW_UINT16*>(value->ItemList);
    EXPECT_EQ(TWSX_NATIVE, mechanisms[0]);
    EXPECT_EQ(TWSX_MEMORY, mechanisms[1]);
    unlock(mechanism.hContainer); release(mechanism.hContainer);

    TW_CAPABILITY supported{};
    supported.Cap = CAP_SUPPORTEDCAPS;
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_CAPABILITY, MSG_GET, &supported));
    ASSERT_EQ(TWON_ARRAY, supported.ConType);
    auto* capabilities = static_cast<pTW_ARRAY>(lock(supported.hContainer));
    ASSERT_NE(nullptr, capabilities);
    EXPECT_EQ(TWTY_UINT16, capabilities->ItemType);
    ASSERT_EQ(4u, capabilities->NumItems);
    const auto* list = reinterpret_cast<const TW_UINT16*>(capabilities->ItemList);
    EXPECT_EQ(ICAP_XFERMECH, list[0]);
    EXPECT_EQ(ICAP_BITDEPTH, list[3]);
    unlock(supported.hContainer); release(supported.hContainer);

    TW_CAPABILITY dpi{};
    dpi.Cap = ICAP_XRESOLUTION;
    EXPECT_EQ(TWRC_FAILURE, call(DG_CONTROL, DAT_CAPABILITY, MSG_GET, &dpi));
    TW_STATUS status{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_STATUS, MSG_GET, &status));
    EXPECT_EQ(TWCC_CAPUNSUPPORTED, status.ConditionCode);
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}
}  // namespace
