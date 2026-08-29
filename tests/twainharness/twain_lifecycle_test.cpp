#include "TwainDataSource.h"
#include "SimTransport.h"
#include "FailureInjector.h"
#include "transport/ITransportProvider.h"

#include <gtest/gtest.h>

#include <array>
#include <ios>
#include <cstdlib>
#include <memory>
#include <thread>

namespace {
// Provajder koji pamti POSLEDNJI napravljen simulator.
//
// Bez toga se otkaz ne moze ubaciti: DS sam otvara uredjaj, pa test nema
// nijedan drugi put do transporta koji je zaista u upotrebi.
class RecordingProvider final : public g2710::ITransportProvider {
public:
    g2710::Result<std::unique_ptr<g2710::ITransport>> create(
        const g2710::DeviceRef&) override {
        auto transport = std::make_unique<g2710::sim::SimTransport>();
        last = transport.get();
        return std::unique_ptr<g2710::ITransport>(std::move(transport));
    }
    const char* name() const noexcept override { return "sim-recording"; }

    g2710::sim::SimTransport* last = nullptr;
};

RecordingProvider* simulator() {
    static RecordingProvider* recorder = nullptr;
    static auto provider = [] {
        auto owned = std::make_unique<RecordingProvider>();
        recorder = owned.get();
        return std::make_unique<g2710::TransportProvider::ScopedTestProvider>(std::move(owned));
    }();
    (void)provider;
    return recorder;
}

TW_UINT16 call(TW_UINT32 dg, TW_UINT16 dat, TW_UINT16 msg, TW_MEMREF data = nullptr) {
    // Harness poziva potpuno isti DS kod, ali mu umesto fizickog USB uredjaja
    // daje simulator. To dokazuje stvarni Core transfer, ne sinteticki bajt.
    (void)simulator();
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

// Prekid USRED prenosa.
//
// Postojeci testovi daju bafer veci od cele slike, pa prvi poziv odmah vrati
// TWRC_XFERDONE i stanje ode pravo u TransferDone. Stanje Transferring - u
// kome aplikacija zaista provede vecinu velikog skeniranja - nije dodirivao
// nijedan test.
//
// To je bas stanje u kome korisnik pritiska "Otkazi": TWAIN za to propisuje
// DAT_PENDINGXFERS / MSG_RESET. Prolaz koji se tada ne zatvori ostavlja cip da
// skenira i glavu da se krece.
TEST(TwainLifecycle, AbortingMidTransferIsAllowedAndClosesThePass) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS));

    // Bafer namerno manji od slike: 64 px u boji je 192 bajta po redu, slika
    // ima osam redova. U 600 bajtova staju tri - pa prenos ostaje NEDOVRSEN.
    std::array<TW_UINT8, 600> pixels{};
    TW_IMAGEMEMXFER transfer{};
    transfer.Memory.TheMem = pixels.data();
    transfer.Memory.Length = static_cast<TW_UINT32>(pixels.size());

    ASSERT_EQ(TWRC_SUCCESS, call(DG_IMAGE, DAT_IMAGEMEMXFER, MSG_GET, &transfer))
        << "bafer je ipak primio celu sliku; test ne meri stanje Transferring";
    ASSERT_GT(transfer.Rows, 0u);

    TW_PENDINGXFERS pending{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_PENDINGXFERS, MSG_RESET, &pending))
        << "aplikacija ne moze da prekine prenos koji traje";
    EXPECT_EQ(0, pending.Count);

    // Dokaz da je prolaz zaista zatvoren: sledeci prenos mora poceti od nule.
    // Da je stari ostao otvoren, uredjaj bi bio zauzet ili bi se nastavilo
    // tamo gde je stalo.
    std::array<TW_UINT8, 8192> whole{};
    TW_IMAGEMEMXFER again{};
    again.Memory.TheMem = whole.data();
    again.Memory.Length = static_cast<TW_UINT32>(whole.size());
    EXPECT_EQ(TWRC_XFERDONE, call(DG_IMAGE, DAT_IMAGEMEMXFER, MSG_GET, &again));
    EXPECT_EQ(0u, again.YOffset) << "prenos se nastavio umesto da pocne iznova";

    TW_PENDINGXFERS done{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_PENDINGXFERS, MSG_ENDXFER, &done));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}

// TWAIN dozvoljava i MSG_ENDXFER iz stanja 6, ne samo iz 7. Aplikacija time
// kaze "dosta mi je ove stranice" bez odustajanja od sesije.
TEST(TwainLifecycle, EndXferIsAllowedWhileATransferIsStillRunning) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS));

    std::array<TW_UINT8, 600> pixels{};
    TW_IMAGEMEMXFER transfer{};
    transfer.Memory.TheMem = pixels.data();
    transfer.Memory.Length = static_cast<TW_UINT32>(pixels.size());
    ASSERT_EQ(TWRC_SUCCESS, call(DG_IMAGE, DAT_IMAGEMEMXFER, MSG_GET, &transfer));

    TW_PENDINGXFERS pending{};
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_PENDINGXFERS, MSG_ENDXFER, &pending));
    EXPECT_EQ(0, pending.Count);

    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS));
    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}

// Zaustavljanje cipa je i samo transfer i moze da padne.
//
// Prva verzija je pisala `(void)s.session->finish();` - pa bi TWAIN prijavio
// uspesan prekid i kada cip nije zaustavljen. Aplikacija bi mislila da je
// skener slobodan, a glava bi se i dalje kretala.
//
// Mutaciona provera je pokazala da to niko ne meri: uklanjanje `finish()` nije
// oborilo nijedan test, jer destruktor sesije zatvori prolaz umesto njega. Dva
// puta se spolja ne razlikuju kada sve prodje - razlikuju se samo kada NE
// prodje, i to je ono sto ovaj test hvata.
TEST(TwainLifecycle, AFailedCloseIsReportedInsteadOfBeingSwallowed) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS));

    std::array<TW_UINT8, 600> pixels{};
    TW_IMAGEMEMXFER transfer{};
    transfer.Memory.TheMem = pixels.data();
    transfer.Memory.Length = static_cast<TW_UINT32>(pixels.size());
    ASSERT_EQ(TWRC_SUCCESS, call(DG_IMAGE, DAT_IMAGEMEMXFER, MSG_GET, &transfer));

    // Od ovog trenutka nijedan upis u registre ne prolazi - kao iscupan kabl.
    // Zaustavljanje cipa je upis, pa mora pasti.
    auto* sim = simulator()->last;
    ASSERT_NE(nullptr, sim);
    sim->faults().injectPermanent(g2710::sim::TransferKind::ControlOut,
                                  g2710::ErrorCode::TransportLost);

    TW_PENDINGXFERS pending{};
    EXPECT_EQ(TWRC_FAILURE, call(DG_CONTROL, DAT_PENDINGXFERS, MSG_RESET, &pending))
        << "prekid je prijavljen kao uspeh iako cip nije zaustavljen";

    TW_STATUS status{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_STATUS, MSG_GET, &status));
    EXPECT_EQ(TWCC_OPERATIONERROR, status.ConditionCode);

    sim->faults().clear();
    (void)call(DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS);
    (void)call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id);
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

// Verzija u identitetu je ono sto aplikacija prikaze u spisku izvora.
//
// Dok je bila otkucana u izvoru, bila je jedno od sedam mesta na kojima je
// pisala verzija projekta. Sest ih se slagalo slucajno; INF se vec bio razisao.
TEST(TwainIdentity, VersionComesFromTheProjectNotFromTheSource) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_GET, &id));

    EXPECT_EQ(id.Version.MajorNum, G2710_VERSION_MAJOR);
    EXPECT_EQ(id.Version.MinorNum, G2710_VERSION_MINOR);
}

// MSG_QUERYSUPPORT je prvo pitanje koje ozbiljna TWAIN aplikacija postavi.
//
// Prva verzija je vracala TWRC_SUCCESS sa hContainer = nullptr. Aplikacija koja
// radi ono sto standard nalaze - proveri povratni kod, pa zakljuca kontejner -
// zakljucavala bi nulu. Nijedan nas prolaz to nije primecivao, jer nijedan
// nije zvao QUERYSUPPORT. Videlo bi se tek u tudjem programu, kao pad skenera
// koji "ne radi sa ovim drajverom".
TEST(TwainCapabilities, QuerySupportAnswersWithARealContainer) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    TW_ENTRYPOINT entry{};
    entry.Size = sizeof(entry);
    entry.DSM_MemAllocate = allocate; entry.DSM_MemFree = release;
    entry.DSM_MemLock = lock; entry.DSM_MemUnlock = unlock;
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_ENTRYPOINT, MSG_GET, &entry));

    // Tip se pise izricito: TWAIN konstante su `int`, pa bi lista bez tipa
    // deducirala int i suzavanje bi bilo upozorenje (kod nas: greska).
    constexpr std::array<TW_UINT16, 5> kAnswering{
        CAP_SUPPORTEDCAPS, ICAP_XFERMECH, ICAP_UNITS, ICAP_PIXELTYPE, ICAP_BITDEPTH};
    for (TW_UINT16 supported : kAnswering) {
        TW_CAPABILITY cap{};
        cap.Cap = supported;
        ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_CAPABILITY, MSG_QUERYSUPPORT, &cap))
            << "cap 0x" << std::hex << supported;
        ASSERT_EQ(TWON_ONEVALUE, cap.ConType) << "cap 0x" << std::hex << supported;
        ASSERT_NE(nullptr, cap.hContainer) << "cap 0x" << std::hex << supported;
        auto* one = static_cast<pTW_ONEVALUE>(lock(cap.hContainer));
        ASSERT_NE(nullptr, one);
        EXPECT_EQ(TWTY_INT32, one->ItemType);
        EXPECT_EQ(static_cast<TW_UINT32>(TWQC_GET | TWQC_GETCURRENT | TWQC_GETDEFAULT),
                  one->Item)
            << "cap 0x" << std::hex << supported;
        unlock(cap.hContainer); release(cap.hContainer);
    }

    // Rezolucija se prepoznaje, ali dok H8 ne prodje nema nijednu operaciju.
    // Nula je odgovor koji aplikacija sme da procita; null kontejner nije.
    constexpr std::array<TW_UINT16, 2> kNotYet{ICAP_XRESOLUTION, ICAP_YRESOLUTION};
    for (TW_UINT16 unqualified : kNotYet) {
        TW_CAPABILITY cap{};
        cap.Cap = unqualified;
        ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_CAPABILITY, MSG_QUERYSUPPORT, &cap));
        ASSERT_NE(nullptr, cap.hContainer);
        auto* one = static_cast<pTW_ONEVALUE>(lock(cap.hContainer));
        ASSERT_NE(nullptr, one);
        EXPECT_EQ(0u, one->Item) << "rezolucija ne sme obecati nijednu operaciju";
        unlock(cap.hContainer); release(cap.hContainer);
    }

    // Mogucnost koju uopste ne poznajemo i dalje pada, i ne alocira nista.
    TW_CAPABILITY unknown{};
    unknown.Cap = ICAP_BRIGHTNESS;
    EXPECT_EQ(TWRC_FAILURE, call(DG_CONTROL, DAT_CAPABILITY, MSG_QUERYSUPPORT, &unknown));
    EXPECT_EQ(nullptr, unknown.hContainer);

    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}

// Ono sto QUERYSUPPORT obeca, MSG_GET mora i da isporuci - i obrnuto.
//
// Tri mesta su ranije odgovarala na isto pitanje: spisak u CAP_SUPPORTEDCAPS,
// uslov u isAdvertisedCapability, i switch u MSG_GET. Rezolucija je u jednom
// bila podrzana a u druga dva nije.
TEST(TwainCapabilities, WhatQuerySupportPromisesTheGetDelivers) {
    TW_IDENTITY id{};
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &id));
    TW_ENTRYPOINT entry{};
    entry.Size = sizeof(entry);
    entry.DSM_MemAllocate = allocate; entry.DSM_MemFree = release;
    entry.DSM_MemLock = lock; entry.DSM_MemUnlock = unlock;
    ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_ENTRYPOINT, MSG_GET, &entry));

    constexpr std::array<TW_UINT16, 7> kKnown{
        CAP_SUPPORTEDCAPS, ICAP_XFERMECH, ICAP_UNITS, ICAP_PIXELTYPE,
        ICAP_BITDEPTH, ICAP_XRESOLUTION, ICAP_YRESOLUTION};
    for (TW_UINT16 which : kKnown) {
        TW_CAPABILITY query{};
        query.Cap = which;
        ASSERT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_CAPABILITY, MSG_QUERYSUPPORT, &query));
        auto* one = static_cast<pTW_ONEVALUE>(lock(query.hContainer));
        ASSERT_NE(nullptr, one);
        const bool promised = (one->Item & TWQC_GET) != 0;
        unlock(query.hContainer); release(query.hContainer);

        TW_CAPABILITY get{};
        get.Cap = which;
        const TW_UINT16 result = call(DG_CONTROL, DAT_CAPABILITY, MSG_GET, &get);
        EXPECT_EQ(promised, result == TWRC_SUCCESS)
            << "cap 0x" << std::hex << which << " se ne slaze sam sa sobom";
        if (result == TWRC_SUCCESS) {
            EXPECT_NE(nullptr, get.hContainer);
            release(get.hContainer);
        }
    }

    EXPECT_EQ(TWRC_SUCCESS, call(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &id));
}
}  // namespace
