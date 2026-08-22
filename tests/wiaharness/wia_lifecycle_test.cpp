// Zivotni ciklus WIA drajvera, kroz PRAVE COM objekte, bez uredjaja.
//
// Acceptance gate faze G2710-9. Do sada je bio testiran samo sloj odluka
// (WiaCapabilities, WiaEvents, WiaItemContext); ovo vozi same objekte.
//
// ZASTO SE HARNESS KOMPAJLIRA U TEST BINARNI FAJL, a ne ucitava DLL:
//
// `g2710_wia` linkuje `G2710::Core` staticki. Da harness uradi LoadLibrary +
// DllGetClassObject, DLL bi nosio SVOJU kopiju TransportProvider singletona -
// ScopedTestProvider iz test procesa ne bi imao nikakvog efekta, a test bi i
// dalje PROLAZIO, samo bi merio nesto drugo. Zato se izvori kompajliraju
// ovde, pa je provajder jedan objekat.
//
// STA OVDE NIJE, i zasto:
//
//   drvInitItemProperties, drvValidateItemProperties, i citanje osobina u
//   drvAcquireItemData zovu wiasReadPropLong / wiasWritePropLong. Izmereno je
//   da te funkcije van WIA servisa vracaju E_INVALIDARG - skladiste osobina
//   pravi servis. To ostaje H11.
//
//   `wiasCreateDrvItem` je izmeren i RADI van servisa, pa se stablo pravi.
//
// Sve sto se moze pogresiti u prenosu je zato izvuceno u `runTransfer`, koji
// ne dira nijednu WIA osobinu i vozi se ovde nad simulatorom.
//
// Build koristi G2710_WIA_ALLOW_UNQUALIFIED=1. Bez toga ponuda je prazna,
// drvInitializeWia odbija na vratima, i nema zivotnog ciklusa koji bi se
// testirao. Grana "nema sta da se ponudi -> odbij" pokrivena je u izdanju,
// u unit/wia_capabilities_test.cpp.

#include "G2710MiniDrv.h"
#include "G2710Usd.h"
#include "WiaTransfer.h"

#include "SimTransport.h"
#include "transport/ITransportProvider.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace g2710;
using namespace g2710::wia;

namespace {

// IID_IStiDeviceControl je u stiusd.h samo DEKLARISAN - nijedna SDK biblioteka
// ga ne definise, a `#include <initguid.h>` bi definisao i sve ostale GUID-ove
// iz tog zaglavlja, pa bi se sudarili sa WiaGuid.lib. Zato se ovde definise
// samo taj jedan, sa vrednoscu iz SDK zaglavlja.
const GUID kIidStiDeviceControl = {
    0x128A9860, 0x52DC, 0x11D0, {0x9E, 0xDF, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// --- laznjak koji STI servis inace daje --------------------------------------

// Vraca ime porta i rezim otvaranja; sve ostalo je van produkcione putanje pa
// vraca E_NOTIMPL. Bas to i treba da bude vidljivo: ako drajver ikada pocne da
// zove nesto drugo, test ce to odmah pokazati.
class FakeDeviceControl final : public IStiDeviceControl {
public:
    std::wstring portName = L"\\\\.\\Usbscan0";
    DWORD openMode = STI_DEVICE_CREATE_DATA;
    HRESULT openModeResult = S_OK;
    HRESULT portNameResult = S_OK;

    int openModeCalls = 0;
    int portNameCalls = 0;

    // Redosled poziva, doslovno. MASTER plan trazi rezim PRE imena porta -
    // rezim odlucuje da li smemo da trazimo podatke.
    std::vector<std::string> calls;

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == kIidStiDeviceControl) {
            *object = this;
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override { return --references_; }

    STDMETHODIMP Initialize(DWORD, DWORD, LPCWSTR, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP RawReadData(LPVOID, LPDWORD, LPOVERLAPPED) override { return E_NOTIMPL; }
    STDMETHODIMP RawWriteData(LPVOID, DWORD, LPOVERLAPPED) override { return E_NOTIMPL; }
    STDMETHODIMP RawReadCommand(LPVOID, LPDWORD, LPOVERLAPPED) override { return E_NOTIMPL; }
    STDMETHODIMP RawWriteCommand(LPVOID, DWORD, LPOVERLAPPED) override { return E_NOTIMPL; }
    STDMETHODIMP RawDeviceControl(USD_CONTROL_CODE, LPVOID, DWORD, LPVOID, DWORD,
                                  LPDWORD) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetLastError(LPDWORD) override { return E_NOTIMPL; }

    STDMETHODIMP GetMyDevicePortName(LPWSTR path, DWORD size) override {
        calls.emplace_back("GetMyDevicePortName");
        ++portNameCalls;
        if (FAILED(portNameResult)) {
            return portNameResult;
        }
        if (path == nullptr || size == 0) {
            return E_INVALIDARG;
        }
        const std::size_t copied = std::min<std::size_t>(portName.size(), size - 1);
        std::wmemcpy(path, portName.c_str(), copied);
        path[copied] = L'\0';
        return S_OK;
    }

    // NIJE na produkcionoj putanji. Vraca E_NOTIMPL namerno: ako se drajver
    // ikada osloni na njega pre nego sto ga H11 kvalifikuje, ovo pada.
    STDMETHODIMP GetMyDeviceHandle(LPHANDLE handle) override {
        calls.emplace_back("GetMyDeviceHandle");
        if (handle != nullptr) {
            *handle = INVALID_HANDLE_VALUE;
        }
        return E_NOTIMPL;
    }

    STDMETHODIMP GetMyDeviceOpenMode(LPDWORD mode) override {
        calls.emplace_back("GetMyDeviceOpenMode");
        ++openModeCalls;
        if (FAILED(openModeResult)) {
            return openModeResult;
        }
        if (mode == nullptr) {
            return E_INVALIDARG;
        }
        *mode = openMode;
        return S_OK;
    }

    STDMETHODIMP WriteToErrorLog(DWORD, LPCWSTR, DWORD) override { return S_OK; }

private:
    ULONG references_ = 1;
};

// --- sink koji pamti umesto da salje ------------------------------------------

// Nije `final`: jedan test iz njega izvodi sink koji otkazuje kroz token.
class RecordingSink : public ITransferSink {
public:
    TransferGeometry geometry{};
    int beginCalls = 0;
    int lines = 0;
    int progressCalls = 0;
    int finishCalls = 0;
    std::vector<int> percents;
    std::vector<std::uint8_t> bytes;

    // Posle kog reda da se pretvaramo da je aplikacija odustala. -1 = nikad.
    int cancelAfterLine = -1;

    // Posle kog reda da vratimo tvrdu gresku. -1 = nikad.
    int failAfterLine = -1;
    HRESULT failure = E_FAIL;

    HRESULT begin(const TransferGeometry& g) override {
        geometry = g;
        ++beginCalls;
        return S_OK;
    }

    HRESULT writeLine(std::span<const std::uint8_t> line) override {
        if (failAfterLine >= 0 && lines >= failAfterLine) {
            return failure;
        }
        if (cancelAfterLine >= 0 && lines >= cancelAfterLine) {
            return S_FALSE;
        }
        bytes.insert(bytes.end(), line.begin(), line.end());
        ++lines;
        return S_OK;
    }

    HRESULT progress(int percent) override {
        ++progressCalls;
        percents.push_back(percent);
        return S_OK;
    }

    HRESULT finish() override {
        ++finishCalls;
        return S_OK;
    }
};

// --- lazni WIA callback -------------------------------------------------------

class FakeCallback final : public IWiaMiniDrvCallBack {
public:
    struct Call {
        LONG message = 0;
        LONG status = 0;
        LONG percent = 0;
        LONG offset = 0;
        LONG length = 0;
    };

    std::vector<Call> calls;

    // Na kom pozivu IT_MSG_DATA da se vrati S_FALSE. -1 = nikad.
    int cancelOnDataCall = -1;
    int dataCalls = 0;

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IWiaMiniDrvCallBack) {
            *object = this;
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override { return --references_; }

    STDMETHODIMP MiniDrvCallback(LONG message, LONG status, LONG percent, LONG offset,
                                 LONG length, PMINIDRV_TRANSFER_CONTEXT, LONG) override {
        calls.push_back(Call{message, status, percent, offset, length});
        if (message == IT_MSG_DATA) {
            ++dataCalls;
            if (cancelOnDataCall >= 0 && dataCalls > cancelOnDataCall) {
                return S_FALSE;
            }
        }
        return S_OK;
    }

    int countOf(LONG message) const {
        return static_cast<int>(std::count_if(
            calls.begin(), calls.end(), [&](const Call& c) { return c.message == message; }));
    }

private:
    ULONG references_ = 1;
};

// --- zajednicko okruzenje -----------------------------------------------------

class WiaLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        provider_ = std::make_unique<TransportProvider::ScopedTestProvider>(
            std::make_unique<sim::SimTransportProvider>());
    }

    // Stari provajder se PRVO rusi. Dodela u mestu radi obrnuto - napravi novi
    // pa unisti stari - a unistavanje starog VRACA produkcioni transport.
    // Sledeci open() bi tada otvorio pravi \\.\Usbscan0.
    void TearDown() override { provider_.reset(); }

    // Uredjaj otvoren istim putem kojim ga otvara prenos.
    std::unique_ptr<G2710Device> openDevice() {
        DeviceOptions options;
        options.safety = SafetyGate{SafetyLevel::FullScan};
        options.clientName = "wiaharness";

        auto device = G2710Device::open(DeviceRef::defaultUsbScan(), options);
        EXPECT_TRUE(device);
        if (!device) {
            return nullptr;
        }
        EXPECT_TRUE(device.value()->identify());
        EXPECT_TRUE(device.value()->begin());
        return std::move(device.value());
    }

    // Mala oblast: test ne sme da traje kao pravo skeniranje.
    static ItemSettings smallColorScan() {
        ItemSettings settings;
        settings.xResolution = 300;
        settings.yResolution = 300;
        settings.dataType = WiaDataType::Color;
        settings.wiaDepth = 24;
        settings.xPosition = 0;
        settings.yPosition = 0;
        settings.xExtent = 64;
        settings.yExtent = 8;
        return settings;
    }

private:
    std::unique_ptr<TransportProvider::ScopedTestProvider> provider_;
};

// Zagrevanje lampe je u produkciji tri sekunde. Test koji svaki put spava tri
// sekunde je test koji se prestane pokretati.
constexpr std::chrono::milliseconds kNoWarmup{0};

}  // namespace

// =============================================================================
// 1. IStiUSD - produkciona putanja otvaranja
// =============================================================================

TEST_F(WiaLifecycleTest, InitializeFollowsTheDocumentedOpenPath) {
    FakeDeviceControl control;
    G2710Usd usd;

    ASSERT_EQ(S_OK, usd.Initialize(&control, STI_VERSION_REAL, nullptr));

    // Redosled iz MASTER plana: rezim otvaranja PRE imena porta.
    ASSERT_EQ(2u, control.calls.size());
    EXPECT_EQ("GetMyDeviceOpenMode", control.calls[0]);
    EXPECT_EQ("GetMyDevicePortName", control.calls[1]);

    // GetMyDeviceHandle NIJE na produkcionoj putanji dok ga H11 ne kvalifikuje.
    EXPECT_EQ(control.calls.end(),
              std::find(control.calls.begin(), control.calls.end(), "GetMyDeviceHandle"));

    EXPECT_NE(nullptr, usd.device());
    EXPECT_EQ(L"\\\\.\\Usbscan0", usd.portName());
}

TEST_F(WiaLifecycleTest, InitializeIdentifiesTheDeviceBeforeReturning) {
    FakeDeviceControl control;
    G2710Usd usd;
    ASSERT_EQ(S_OK, usd.Initialize(&control, STI_VERSION_REAL, nullptr));

    // Ime porta koje STI daje je deljeno. Ako iza njega stoji tudji uredjaj,
    // vendor komande sa G2710 semantikom ne smeju krenuti - zato identify ide
    // odmah, a ne pri prvom skeniranju.
    ASSERT_NE(nullptr, usd.device());
    EXPECT_TRUE(usd.device()->isIdentified());
}

TEST_F(WiaLifecycleTest, InitializeWithoutControlIsRefused) {
    G2710Usd usd;
    EXPECT_EQ(STIERR_INVALID_PARAM, usd.Initialize(nullptr, STI_VERSION_REAL, nullptr));
    EXPECT_EQ(nullptr, usd.device());
}

TEST_F(WiaLifecycleTest, AnEmptyPortNameIsRefusedInsteadOfOpeningNothing) {
    FakeDeviceControl control;
    control.portName.clear();

    G2710Usd usd;
    EXPECT_EQ(STIERR_OBJECTNOTFOUND, usd.Initialize(&control, STI_VERSION_REAL, nullptr));
    EXPECT_EQ(nullptr, usd.device());
}

TEST_F(WiaLifecycleTest, AFailingOpenModeStopsBeforeAskingForThePort) {
    FakeDeviceControl control;
    control.openModeResult = E_ACCESSDENIED;

    G2710Usd usd;
    EXPECT_EQ(E_ACCESSDENIED, usd.Initialize(&control, STI_VERSION_REAL, nullptr));

    // Ime porta se ne trazi ako se ne zna sme li se uopste traziti podatke.
    EXPECT_EQ(0, control.portNameCalls);
    EXPECT_EQ(nullptr, usd.device());
}

// =============================================================================
// 2. Dva interfejsa, jedan objekat
// =============================================================================

TEST_F(WiaLifecycleTest, TheMiniDriverAndTheUsdShareOneDevice) {
    FakeDeviceControl control;
    G2710Usd usd;
    ASSERT_EQ(S_OK, usd.Initialize(&control, STI_VERSION_REAL, nullptr));

    IWiaMiniDrv* miniDrv = nullptr;
    ASSERT_EQ(S_OK, usd.QueryInterface(IID_IWiaMiniDrv, reinterpret_cast<void**>(&miniDrv)));
    ASSERT_NE(nullptr, miniDrv);

    // Da su dva objekta, dva bi se borila za isti uredjaj kroz arbitrazu.
    // Nazad na IStiUSD mora dati bas ovaj objekat.
    IStiUSD* back = nullptr;
    ASSERT_EQ(S_OK, miniDrv->QueryInterface(IID_IStiUSD, reinterpret_cast<void**>(&back)));
    EXPECT_EQ(static_cast<IStiUSD*>(&usd), back);

    back->Release();
    miniDrv->Release();
}

TEST_F(WiaLifecycleTest, AnUnknownInterfaceIsRefusedAndClearsTheOutParameter) {
    G2710Usd usd;
    void* nowhere = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xDEADBEEF));

    EXPECT_EQ(E_NOINTERFACE, usd.QueryInterface(IID_IWiaMiniDrvCallBack, &nowhere));

    // COM trazi da izlazni pokazivac bude ocisten i kada se odbija. Bez toga
    // pozivalac vidi smece koje izgleda kao valjan objekat.
    EXPECT_EQ(nullptr, nowhere);
}

// =============================================================================
// 3. IStiUSD - ostatak, nad otvorenim uredjajem
// =============================================================================

TEST_F(WiaLifecycleTest, CapabilitiesAndStatusAnswerAfterInitialize) {
    FakeDeviceControl control;
    G2710Usd usd;
    ASSERT_EQ(S_OK, usd.Initialize(&control, STI_VERSION_REAL, nullptr));

    STI_USD_CAPS caps{};
    ASSERT_EQ(S_OK, usd.GetCapabilities(&caps));
    EXPECT_EQ(static_cast<DWORD>(STI_VERSION_REAL), caps.dwVersion);
    EXPECT_NE(0u, caps.dwGenericCaps & STI_GENCAP_WIA);

    STI_DEVICE_STATUS status{};
    status.StatusMask = STI_DEVSTATUS_ONLINE_STATE;
    ASSERT_EQ(S_OK, usd.GetStatus(&status));
}

TEST_F(WiaLifecycleTest, NullArgumentsAreRefusedNotDereferenced) {
    G2710Usd usd;
    EXPECT_EQ(STIERR_INVALID_PARAM, usd.GetCapabilities(nullptr));
    EXPECT_EQ(STIERR_INVALID_PARAM, usd.GetStatus(nullptr));
}

TEST_F(WiaLifecycleTest, LockAndUnlockArePaired) {
    FakeDeviceControl control;
    G2710Usd usd;
    ASSERT_EQ(S_OK, usd.Initialize(&control, STI_VERSION_REAL, nullptr));

    ASSERT_EQ(S_OK, usd.LockDevice());
    ASSERT_EQ(S_OK, usd.LockDevice());
    EXPECT_EQ(S_OK, usd.UnLockDevice());
    EXPECT_EQ(S_OK, usd.UnLockDevice());

    // Visak otkljucavanja je greska pozivaoca, ne stanje iz koga se nastavlja.
    EXPECT_NE(S_OK, usd.UnLockDevice());
}

// =============================================================================
// 4. Prenos - ono zbog cega harness i postoji
// =============================================================================

TEST_F(WiaLifecycleTest, AWholeTransferDeliversEveryLine) {
    auto device = openDevice();
    ASSERT_NE(nullptr, device);

    ItemContext item;
    item.kind = ItemKind::Flatbed;
    RecordingSink sink;
    LONG deviceError = 0;

    ASSERT_EQ(S_OK, runTransfer(*device, item, smallColorScan(), sink, &deviceError,
                                kNoWarmup));

    EXPECT_EQ(0, deviceError);
    EXPECT_EQ(1, sink.beginCalls);
    EXPECT_EQ(1, sink.finishCalls);
    EXPECT_GT(sink.lines, 0);
    EXPECT_EQ(sink.geometry.lines, sink.lines);
    EXPECT_EQ(sink.geometry.bytesPerLine * static_cast<std::size_t>(sink.lines),
              sink.bytes.size());

    // Boja, 8 bita po kanalu: tri bajta po pikselu.
    EXPECT_EQ(static_cast<std::size_t>(sink.geometry.widthInPixels) * 3,
              sink.geometry.bytesPerLine);
    EXPECT_EQ(24, sink.geometry.wiaDepth);
}

TEST_F(WiaLifecycleTest, TheSessionIsClosedExplicitlyNotByTheDestructorNet) {
    auto device = openDevice();
    ASSERT_NE(nullptr, device);

    ItemContext item;
    item.kind = ItemKind::Flatbed;
    RecordingSink sink;
    LONG deviceError = 0;

    ASSERT_EQ(S_OK, runTransfer(*device, item, smallColorScan(), sink, &deviceError,
                                kNoWarmup));

    // Sesija koja se ne zatvori ostavlja cip da skenira i glavu da se krece.
    // Mreza u destruktoru je poslednja odbrana, ne redovan put - i bez ovog
    // polja se ta razlika ne bi videla.
    EXPECT_TRUE(item.closedExplicitly);
    EXPECT_FALSE(item.transferring());
}

TEST_F(WiaLifecycleTest, CancelMidTransferReturnsSFalseAndStillClosesThePass) {
    auto device = openDevice();
    ASSERT_NE(nullptr, device);

    ItemContext item;
    item.kind = ItemKind::Flatbed;
    RecordingSink sink;
    sink.cancelAfterLine = 3;
    LONG deviceError = 0;

    // Odustajanje aplikacije NIJE greska - zato S_FALSE, ne E_ABORT.
    EXPECT_EQ(S_FALSE, runTransfer(*device, item, smallColorScan(), sink, &deviceError,
                                   kNoWarmup));

    EXPECT_EQ(3, sink.lines);
    EXPECT_EQ(0, sink.finishCalls);

    // Najvaznije u celom fajlu: prekinut prenos mora zatvoriti prolaz.
    EXPECT_TRUE(item.closedExplicitly);
    EXPECT_FALSE(item.transferring());
}

TEST_F(WiaLifecycleTest, AHardErrorMidTransferStillClosesThePass) {
    auto device = openDevice();
    ASSERT_NE(nullptr, device);

    ItemContext item;
    item.kind = ItemKind::Flatbed;
    RecordingSink sink;
    sink.failAfterLine = 2;
    sink.failure = E_OUTOFMEMORY;
    LONG deviceError = 0;

    EXPECT_EQ(E_OUTOFMEMORY, runTransfer(*device, item, smallColorScan(), sink,
                                         &deviceError, kNoWarmup));
    EXPECT_TRUE(item.closedExplicitly);
    EXPECT_FALSE(item.transferring());
}

TEST_F(WiaLifecycleTest, CancellingThroughTheTokenIsReportedAsSFalse) {
    auto device = openDevice();
    ASSERT_NE(nullptr, device);

    ItemContext item;
    item.kind = ItemKind::Flatbed;
    LONG deviceError = 0;

    // Token se resetuje na pocetku runTransfer-a, pa otkazivanje mora doci
    // IZ prenosa - kao sto u produkciji dolazi iz drugog poziva na uredjaj.
    struct CancellingSink final : RecordingSink {
        ItemContext* target = nullptr;
        HRESULT writeLine(std::span<const std::uint8_t> line) override {
            const HRESULT result = RecordingSink::writeLine(line);
            if (lines >= 2 && target != nullptr) {
                target->cancellation.cancel();
            }
            return result;
        }
    } cancelling;
    cancelling.target = &item;

    EXPECT_EQ(S_FALSE, runTransfer(*device, item, smallColorScan(), cancelling,
                                   &deviceError, kNoWarmup));

    // Otkazano kroz token: ErrorCode::Cancelled se preslikava u S_FALSE.
    EXPECT_EQ(static_cast<LONG>(ErrorCode::Cancelled), deviceError);
    EXPECT_TRUE(item.closedExplicitly);
}

TEST_F(WiaLifecycleTest, ProgressIsReportedButNotOncePerLine) {
    auto device = openDevice();
    ASSERT_NE(nullptr, device);

    ItemContext item;
    item.kind = ItemKind::Flatbed;
    RecordingSink sink;
    LONG deviceError = 0;

    ItemSettings settings = smallColorScan();
    settings.yExtent = 200;

    ASSERT_EQ(S_OK, runTransfer(*device, item, settings, sink, &deviceError, kNoWarmup));

    // Javljati svaki red znaci hiljade COM poziva po slici.
    EXPECT_GT(sink.progressCalls, 0);
    EXPECT_LE(sink.progressCalls, kProgressSteps);
    EXPECT_LT(sink.progressCalls, sink.lines);

    // Napredak ne sme ici unazad.
    EXPECT_TRUE(std::is_sorted(sink.percents.begin(), sink.percents.end()));
    EXPECT_LE(sink.percents.back(), 100);
}

TEST_F(WiaLifecycleTest, SettingsOutsideTheOfferAreRefusedBeforeAnythingMoves) {
    auto device = openDevice();
    ASSERT_NE(nullptr, device);

    ItemContext item;
    item.kind = ItemKind::Flatbed;
    RecordingSink sink;
    LONG deviceError = 0;

    ItemSettings settings = smallColorScan();
    settings.xResolution = 1234;  // ne postoji ni u kvalifikacionoj ponudi
    settings.yResolution = 1234;

    EXPECT_EQ(WIA_ERROR_INVALID_COMMAND,
              runTransfer(*device, item, settings, sink, &deviceError, kNoWarmup));

    // Nista se nije pomerilo: ni lampa, ni sesija.
    EXPECT_EQ(0, sink.beginCalls);
    EXPECT_FALSE(item.transferring());
}

TEST_F(WiaLifecycleTest, TheRootItemCannotBeScanned) {
    auto device = openDevice();
    ASSERT_NE(nullptr, device);

    ItemContext item;
    item.kind = ItemKind::Root;  // koren je folder, ne izvor slike
    RecordingSink sink;
    LONG deviceError = 0;

    EXPECT_EQ(E_UNEXPECTED,
              runTransfer(*device, item, smallColorScan(), sink, &deviceError, kNoWarmup));
    EXPECT_EQ(0, sink.beginCalls);
}

TEST_F(WiaLifecycleTest, TwoTransfersInARowBothSucceed) {
    auto device = openDevice();
    ASSERT_NE(nullptr, device);

    ItemContext item;
    item.kind = ItemKind::Flatbed;
    LONG deviceError = 0;

    // Ako prvi prenos ostavi cip da skenira, drugi zatice zauzet uredjaj.
    // Ovo je najjeftiniji nacin da se to primeti bez hardvera.
    for (int pass = 0; pass < 2; ++pass) {
        RecordingSink sink;
        ASSERT_EQ(S_OK, runTransfer(*device, item, smallColorScan(), sink, &deviceError,
                                    kNoWarmup))
            << "prolaz " << pass;
        EXPECT_GT(sink.lines, 0) << "prolaz " << pass;
        EXPECT_TRUE(item.closedExplicitly) << "prolaz " << pass;
    }
}

// =============================================================================
// 5. Sink nad baferima servisa
// =============================================================================

namespace {

// MINIDRV_TRANSFER_CONTEXT je obican POD; servis nije potreban da bi se
// napravio. Zato se i ovaj sloj moze meriti bez uredjaja.
struct TransferHarness {
    MINIDRV_TRANSFER_CONTEXT context{};
    FakeCallback callback;
    std::vector<BYTE> buffer;

    void setUpBanded(LONG bufferSize) {
        buffer.assign(static_cast<std::size_t>(bufferSize), 0);
        context.pTransferBuffer = buffer.data();
        context.lBufferSize = bufferSize;
        context.bTransferDataCB = TRUE;
        context.pIWiaMiniDrvCallBack = &callback;
    }

    void setUpWholeImage(LONG imageSize) {
        buffer.assign(static_cast<std::size_t>(imageSize), 0);
        context.pTransferBuffer = buffer.data();
        context.lBufferSize = imageSize;
        context.bTransferDataCB = FALSE;
        context.pIWiaMiniDrvCallBack = &callback;
    }
};

TransferGeometry geometryFor(int lines, std::size_t bytesPerLine) {
    TransferGeometry geometry;
    geometry.widthInPixels = static_cast<int>(bytesPerLine / 3);
    geometry.lines = lines;
    geometry.wiaDepth = 24;
    geometry.xResolution = 300;
    geometry.yResolution = 300;
    geometry.bytesPerLine = bytesPerLine;
    return geometry;
}

}  // namespace

TEST(WiaTransferSink, BandedModeFlushesWhenTheBufferIsFull) {
    TransferHarness harness;
    harness.setUpBanded(300);  // tri reda po 100 bajtova

    WiaCallbackSink sink{&harness.context};
    ASSERT_EQ(S_OK, sink.begin(geometryFor(10, 100)));

    const std::vector<std::uint8_t> line(100, 0x5A);
    for (int i = 0; i < 10; ++i) {
        ASSERT_EQ(S_OK, sink.writeLine(line)) << "red " << i;
    }
    ASSERT_EQ(S_OK, sink.finish());

    // Deset redova po sto bajtova kroz bafer od tristo: tri pune predaje plus
    // ostatak na kraju.
    EXPECT_EQ(4, harness.callback.countOf(IT_MSG_DATA));
    EXPECT_EQ(1000, sink.deliveredBytes());
    EXPECT_EQ(1, harness.callback.countOf(IT_MSG_TERMINATION));
}

TEST(WiaTransferSink, WholeImageModePlacesEveryLineAtItsOffset) {
    TransferHarness harness;
    harness.setUpWholeImage(500);

    WiaCallbackSink sink{&harness.context};
    ASSERT_EQ(S_OK, sink.begin(geometryFor(5, 100)));

    for (int i = 0; i < 5; ++i) {
        const std::vector<std::uint8_t> line(100, static_cast<std::uint8_t>(i + 1));
        ASSERT_EQ(S_OK, sink.writeLine(line)) << "red " << i;
    }
    ASSERT_EQ(S_OK, sink.finish());

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(static_cast<BYTE>(i + 1), harness.buffer[static_cast<std::size_t>(i) * 100])
            << "red " << i << " nije na svom mestu";
    }
    EXPECT_EQ(500, sink.deliveredBytes());
    EXPECT_EQ(0, harness.callback.countOf(IT_MSG_DATA));
}

TEST(WiaTransferSink, GeometryIsWrittenBeforeTheFirstByte) {
    TransferHarness harness;
    harness.setUpWholeImage(500);

    WiaCallbackSink sink{&harness.context};
    ASSERT_EQ(S_OK, sink.begin(geometryFor(5, 100)));

    // WIA cita ova polja pre nego sto uzme ijedan bajt.
    EXPECT_EQ(5, harness.context.lLines);
    EXPECT_EQ(100, harness.context.cbWidthInBytes);
    EXPECT_EQ(500, harness.context.lImageSize);
    EXPECT_EQ(500, harness.context.lItemSize);
    EXPECT_EQ(24, harness.context.lDepth);
    EXPECT_EQ(300, harness.context.lXRes);
}

TEST(WiaTransferSink, AnApplicationThatGivesUpDuringAFlushStopsTheTransfer) {
    TransferHarness harness;
    harness.setUpBanded(200);
    harness.callback.cancelOnDataCall = 1;  // druga predaja vraca S_FALSE

    WiaCallbackSink sink{&harness.context};
    ASSERT_EQ(S_OK, sink.begin(geometryFor(10, 100)));

    const std::vector<std::uint8_t> line(100, 0x11);
    HRESULT last = S_OK;
    for (int i = 0; i < 10 && last == S_OK; ++i) {
        last = sink.writeLine(line);
    }
    EXPECT_EQ(S_FALSE, last);
}

TEST(WiaTransferSink, ALineThatCannotFitTheBufferIsRefusedInsteadOfOverrunning) {
    TransferHarness harness;
    harness.setUpBanded(50);  // manje od jednog reda

    WiaCallbackSink sink{&harness.context};
    ASSERT_EQ(S_OK, sink.begin(geometryFor(10, 100)));

    // Bez ove provere bi se pisalo van bafera koji je dao servis.
    const std::vector<std::uint8_t> line(100, 0x22);
    EXPECT_EQ(E_UNEXPECTED, sink.writeLine(line));
}

TEST(WiaTransferSink, WithoutABufferNothingIsWritten) {
    MINIDRV_TRANSFER_CONTEXT context{};
    WiaCallbackSink sink{&context};
    ASSERT_EQ(S_OK, sink.begin(geometryFor(5, 100)));

    const std::vector<std::uint8_t> line(100, 0x33);
    EXPECT_EQ(E_UNEXPECTED, sink.writeLine(line));
}

// =============================================================================
// 6. Preslikavanje gresaka
// =============================================================================

TEST(WiaTransferErrors, EveryErrorCodeMapsToWhatTheApplicationExpects) {
    // Otkazivanje NIJE greska - to je jedina stavka u ovoj tabeli koja se lako
    // pogresi, i jedina koju aplikacija tumaci kao "korisnik je odustao".
    EXPECT_EQ(S_FALSE, toTransferHresult(ErrorCode::Cancelled));

    EXPECT_EQ(WIA_ERROR_BUSY, toTransferHresult(ErrorCode::Busy));
    EXPECT_EQ(WIA_ERROR_OFFLINE, toTransferHresult(ErrorCode::TransportLost));
    EXPECT_EQ(WIA_ERROR_DEVICE_COMMUNICATION, toTransferHresult(ErrorCode::Timeout));
    EXPECT_EQ(WIA_ERROR_INVALID_COMMAND, toTransferHresult(ErrorCode::SafetyViolation));
    EXPECT_EQ(WIA_ERROR_INVALID_COMMAND, toTransferHresult(ErrorCode::NotImplementedIn10));
    EXPECT_EQ(E_FAIL, toTransferHresult(ErrorCode::InvalidArgument));
}
