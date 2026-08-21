#include "G2710MiniDrv.h"

#include "G2710Profile.generated.h"
#include "G2710Usd.h"
#include "WiaCapabilities.h"
#include "WiaEvents.h"

#include <wiamdef.h>

#include <algorithm>
#include <vector>

namespace g2710::wia {
namespace {

// Formati koje nudimo. BMP je obavezan (WIA ga podrazumeva), ostali su ono
// sto Windows Scan i Paint zaista traze.
const GUID* kFormats[] = {
    &WiaImgFmt_BMP,
    &WiaImgFmt_PNG,
    &WiaImgFmt_JPEG,
    &WiaImgFmt_TIFF,
};

// Naredbe uredjaja koje podrzavamo.
WIA_DEV_CAP_DRV g_capabilities[] = {
    // Dogadjaji - tri fizicka dugmeta.
    {const_cast<GUID*>(&WIA_EVENT_SCAN_IMAGE), WIA_NOTIFICATION_EVENT,
     const_cast<LPOLESTR>(L"Scan"), const_cast<LPOLESTR>(L"Dugme Scan"), nullptr},
    {const_cast<GUID*>(&WIA_EVENT_SCAN_PRINT_IMAGE), WIA_NOTIFICATION_EVENT,
     const_cast<LPOLESTR>(L"Copy"), const_cast<LPOLESTR>(L"Dugme Copy"), nullptr},
    {const_cast<GUID*>(&WIA_EVENT_SCAN_OCR_IMAGE), WIA_NOTIFICATION_EVENT,
     const_cast<LPOLESTR>(L"Pdf"), const_cast<LPOLESTR>(L"Dugme PDF"), nullptr},

    // Naredbe. WIA_ACTION_EVENT je oznaka za naredbu, ne za dogadjaj -
    // imena su u wiadef.h obrnuta od ocekivanja.
    {const_cast<GUID*>(&WIA_CMD_SYNCHRONIZE), WIA_ACTION_EVENT,
     const_cast<LPOLESTR>(L"Synchronize"), const_cast<LPOLESTR>(L"Osvezi stanje"), nullptr},
};

constexpr LONG kCapabilityCount =
    static_cast<LONG>(sizeof(g_capabilities) / sizeof(g_capabilities[0]));

std::vector<WIA_FORMAT_INFO>& formatInfo() {
    // Staticki, jer WIA servis drzi pokazivac posle povratka iz poziva.
    static std::vector<WIA_FORMAT_INFO> formats = [] {
        std::vector<WIA_FORMAT_INFO> result;
        for (const GUID* format : kFormats) {
            WIA_FORMAT_INFO info{};
            info.guidFormatID = *format;
            info.lTymed = TYMED_CALLBACK;
            result.push_back(info);

            info.lTymed = TYMED_FILE;
            result.push_back(info);
        }
        return result;
    }();
    return formats;
}

// Iz naseg Rejection u WIA gresku koju aplikacija ume da prikaze.
HRESULT toHresult(Rejection reason) noexcept {
    switch (reason) {
        case Rejection::None:
            return S_OK;
        case Rejection::NoUsableConfiguration:
            // Nije "los parametar" nego "uredjaj jos nije kvalifikovan".
            return WIA_ERROR_INVALID_COMMAND;
        default:
            return E_INVALIDARG;
    }
}

}  // namespace

// --- IUnknown --------------------------------------------------------------
//
// Zivotni vek je USD objekat. Minidriver ne broji sopstvene reference: kada bi
// ih brojao, WIA servis bi mogao da ga oslobodi dok USD jos drzi transport.

STDMETHODIMP G2710MiniDrv::QueryInterface(REFIID riid, void** object) {
    return owner_->QueryInterface(riid, object);
}

STDMETHODIMP_(ULONG) G2710MiniDrv::AddRef() { return owner_->AddRef(); }

STDMETHODIMP_(ULONG) G2710MiniDrv::Release() { return owner_->Release(); }

// --- stablo ----------------------------------------------------------------

STDMETHODIMP G2710MiniDrv::drvInitializeWia(BYTE* /*wiasContext*/, LONG /*flags*/,
                                            BSTR /*deviceId*/, BSTR rootFullItemName,
                                            IUnknown* /*stiDevice*/, IUnknown* /*unknownInner*/,
                                            IWiaDrvItem** drvItemRoot, IUnknown** unknownOuter,
                                            LONG* deviceError) {
    if (drvItemRoot == nullptr || deviceError == nullptr) {
        return E_POINTER;
    }
    *drvItemRoot = nullptr;
    *deviceError = 0;
    if (unknownOuter != nullptr) {
        *unknownOuter = nullptr;
    }

    // Pre nego sto se ijedna stavka napravi: ima li se sta ponuditi.
    //
    // U izdanju pre hardverske kvalifikacije nema. Bolje je odbiti ovde -
    // aplikacija tada kaze "uredjaj nije spreman" - nego napraviti stablo sa
    // praznom listom rezolucija, koje izgleda kao pokvaren skener.
    if (!hasUsableConfiguration()) {
        return WIA_ERROR_INVALID_COMMAND;
    }

    IWiaDrvItem* root = nullptr;
    ItemContext* rootContext = createItemContext(ItemKind::Root);
    if (rootContext == nullptr) {
        return E_OUTOFMEMORY;
    }

    BSTR rootName = SysAllocString(L"Root");
    if (rootName == nullptr) {
        destroyItemContext(rootContext);
        return E_OUTOFMEMORY;
    }

    HRESULT result = wiasCreateDrvItem(
        WiaItemTypeFolder | WiaItemTypeDevice | WiaItemTypeRoot, rootName, rootFullItemName,
        this, 0, nullptr, &root);
    SysFreeString(rootName);

    if (FAILED(result)) {
        destroyItemContext(rootContext);
        return result;
    }

    // Stavka flatbeda. Njen kontekst nosi podesavanja aplikacije.
    ItemContext* flatbedContext = createItemContext(ItemKind::Flatbed);
    if (flatbedContext == nullptr) {
        root->Release();
        destroyItemContext(rootContext);
        return E_OUTOFMEMORY;
    }

    BSTR itemName = SysAllocString(L"Flatbed");
    BSTR fullName = SysAllocString((std::wstring(rootFullItemName) + L"\\Flatbed").c_str());
    if (itemName == nullptr || fullName == nullptr) {
        SysFreeString(itemName);
        SysFreeString(fullName);
        root->Release();
        destroyItemContext(rootContext);
        destroyItemContext(flatbedContext);
        return E_OUTOFMEMORY;
    }

    IWiaDrvItem* flatbed = nullptr;
    BYTE* deviceContext = nullptr;
    result = wiasCreateDrvItem(WiaItemTypeFile | WiaItemTypeImage | WiaItemTypeDevice,
                               itemName, fullName, this, sizeof(ItemContext*),
                               &deviceContext, &flatbed);
    SysFreeString(itemName);
    SysFreeString(fullName);

    if (FAILED(result)) {
        root->Release();
        destroyItemContext(rootContext);
        destroyItemContext(flatbedContext);
        return result;
    }

    // WIA servis cuva blok bajtova; mi u njega stavljamo pokazivac na nas
    // kontekst. Blok je fiksne velicine, pa je pokazivac jedini nacin da se
    // u njemu nadje objekat sa netrivijalnim konstruktorom.
    *reinterpret_cast<ItemContext**>(deviceContext) = flatbedContext;

    result = flatbed->AddItemToFolder(root);
    flatbed->Release();

    if (FAILED(result)) {
        root->Release();
        destroyItemContext(rootContext);
        destroyItemContext(flatbedContext);
        return result;
    }

    destroyItemContext(rootContext);  // koren nema svoja podesavanja
    *drvItemRoot = root;
    return S_OK;
}

STDMETHODIMP G2710MiniDrv::drvUnInitializeWia(BYTE* /*wiasContext*/) {
    // Stavke oslobadja WIA servis kroz drvFreeDrvItemContext; ovde nema sta.
    return S_OK;
}

STDMETHODIMP G2710MiniDrv::drvFreeDrvItemContext(LONG /*flags*/, BYTE* context,
                                                 LONG* deviceError) {
    if (deviceError != nullptr) {
        *deviceError = 0;
    }
    if (context == nullptr) {
        return S_OK;
    }

    auto** slot = reinterpret_cast<ItemContext**>(context);
    destroyItemContext(*slot);
    *slot = nullptr;
    return S_OK;
}

// --- osobine ---------------------------------------------------------------

STDMETHODIMP G2710MiniDrv::drvInitItemProperties(BYTE* wiasContext, LONG /*flags*/,
                                                 LONG* deviceError) {
    if (wiasContext == nullptr || deviceError == nullptr) {
        return E_POINTER;
    }
    *deviceError = 0;

    LONG itemType = 0;
    if (const HRESULT result = wiasGetItemType(wiasContext, &itemType); FAILED(result)) {
        return result;
    }
    if ((itemType & WiaItemTypeRoot) != 0) {
        // Koren nosi samo osobine uredjaja; WIA servis ih vec ima iz INF-a.
        return S_OK;
    }

    const std::vector<int> resolutions = offeredResolutions();
    if (resolutions.empty()) {
        return WIA_ERROR_INVALID_COMMAND;
    }

    const ItemSettings defaults = defaultSettings();
    const scan::ScanRegion surface = scan::fullFlatbedRegion(defaults.xResolution);

    // Rezolucije: lista, ne opseg. Uredjaj ume tacno ove i nijednu izmedju.
    std::vector<LONG> values(resolutions.begin(), resolutions.end());
    if (const HRESULT result = wiasSetValidListLong(
            wiasContext, WIA_IPS_XRES, static_cast<ULONG>(values.size()),
            defaults.xResolution, values.data());
        FAILED(result)) {
        return result;
    }
    if (const HRESULT result = wiasSetValidListLong(
            wiasContext, WIA_IPS_YRES, static_cast<ULONG>(values.size()),
            defaults.yResolution, values.data());
        FAILED(result)) {
        return result;
    }

    std::vector<LONG> types;
    for (WiaDataType type : offeredDataTypes()) {
        types.push_back(static_cast<LONG>(type));
    }
    if (const HRESULT result = wiasSetValidListLong(
            wiasContext, WIA_IPA_DATATYPE, static_cast<ULONG>(types.size()),
            static_cast<LONG>(WiaDataType::Color), types.data());
        FAILED(result)) {
        return result;
    }

    // Dubina zavisi od tipa podataka, pa se lista postavlja iznova pri svakoj
    // izmeni tipa - u drvValidateItemProperties.
    std::vector<LONG> depths;
    for (int depth : offeredBitDepths(WiaDataType::Color)) {
        depths.push_back(depth);
    }
    if (const HRESULT result = wiasSetValidListLong(
            wiasContext, WIA_IPA_DEPTH, static_cast<ULONG>(depths.size()), 24, depths.data());
        FAILED(result)) {
        return result;
    }

    // Oblast: opseg u pikselima na tekucoj rezoluciji.
    if (const HRESULT result =
            wiasSetValidRangeLong(wiasContext, WIA_IPS_XPOS, 0, 0, surface.width - 1, 1);
        FAILED(result)) {
        return result;
    }
    if (const HRESULT result =
            wiasSetValidRangeLong(wiasContext, WIA_IPS_YPOS, 0, 0, surface.height - 1, 1);
        FAILED(result)) {
        return result;
    }
    if (const HRESULT result = wiasSetValidRangeLong(wiasContext, WIA_IPS_XEXTENT, 1,
                                                     surface.width, surface.width, 1);
        FAILED(result)) {
        return result;
    }
    if (const HRESULT result = wiasSetValidRangeLong(wiasContext, WIA_IPS_YEXTENT, 1,
                                                     surface.height, surface.height, 1);
        FAILED(result)) {
        return result;
    }

    return S_OK;
}

STDMETHODIMP G2710MiniDrv::drvValidateItemProperties(BYTE* wiasContext, LONG /*flags*/,
                                                     ULONG /*count*/,
                                                     const PROPSPEC* /*propSpec*/,
                                                     LONG* deviceError) {
    if (wiasContext == nullptr || deviceError == nullptr) {
        return E_POINTER;
    }
    *deviceError = 0;

    LONG itemType = 0;
    if (const HRESULT result = wiasGetItemType(wiasContext, &itemType); FAILED(result)) {
        return result;
    }
    if ((itemType & WiaItemTypeRoot) != 0) {
        return S_OK;
    }

    ItemSettings settings;
    LONG value = 0;

    if (FAILED(wiasReadPropLong(wiasContext, WIA_IPS_XRES, &value, nullptr, TRUE))) {
        return E_INVALIDARG;
    }
    settings.xResolution = value;
    if (FAILED(wiasReadPropLong(wiasContext, WIA_IPS_YRES, &value, nullptr, TRUE))) {
        return E_INVALIDARG;
    }
    settings.yResolution = value;
    if (FAILED(wiasReadPropLong(wiasContext, WIA_IPA_DATATYPE, &value, nullptr, TRUE))) {
        return E_INVALIDARG;
    }
    settings.dataType = static_cast<WiaDataType>(value);
    if (FAILED(wiasReadPropLong(wiasContext, WIA_IPA_DEPTH, &value, nullptr, TRUE))) {
        return E_INVALIDARG;
    }
    settings.wiaDepth = value;

    if (SUCCEEDED(wiasReadPropLong(wiasContext, WIA_IPS_XPOS, &value, nullptr, FALSE))) {
        settings.xPosition = value;
    }
    if (SUCCEEDED(wiasReadPropLong(wiasContext, WIA_IPS_YPOS, &value, nullptr, FALSE))) {
        settings.yPosition = value;
    }
    if (SUCCEEDED(wiasReadPropLong(wiasContext, WIA_IPS_XEXTENT, &value, nullptr, FALSE))) {
        settings.xExtent = value;
    }
    if (SUCCEEDED(wiasReadPropLong(wiasContext, WIA_IPS_YEXTENT, &value, nullptr, FALSE))) {
        settings.yExtent = value;
    }

    const Validation checked = validate(settings);
    if (!checked.accepted()) {
        return toHresult(checked.reason);
    }

    // Sve sto je popravljeno mora se VRATITI aplikaciji. WIA to i ocekuje -
    // aplikacija posle validacije cita nazad i prikazuje sta ce zaista biti.
    if (checked.changed) {
        const ItemSettings& good = checked.corrected;
        if (FAILED(wiasWritePropLong(wiasContext, WIA_IPS_XRES, good.xResolution)) ||
            FAILED(wiasWritePropLong(wiasContext, WIA_IPS_YRES, good.yResolution)) ||
            FAILED(wiasWritePropLong(wiasContext, WIA_IPA_DEPTH, good.wiaDepth)) ||
            FAILED(wiasWritePropLong(wiasContext, WIA_IPS_XPOS, good.xPosition)) ||
            FAILED(wiasWritePropLong(wiasContext, WIA_IPS_YPOS, good.yPosition)) ||
            FAILED(wiasWritePropLong(wiasContext, WIA_IPS_XEXTENT, good.xExtent)) ||
            FAILED(wiasWritePropLong(wiasContext, WIA_IPS_YEXTENT, good.yExtent))) {
            return E_FAIL;
        }
    }

    // Izvedene vrednosti koje aplikacija cita, a ne postavlja.
    const auto size = imageSizeInBytes(checked.corrected);
    if (!size) {
        return E_INVALIDARG;
    }

    const image::ColorMode mode = toColorMode(checked.corrected.dataType);
    const image::LineGeometry geometry = image::computeLineGeometry(
        mode, bitsPerChannelFrom(mode, checked.corrected.wiaDepth),
        static_cast<std::size_t>(checked.corrected.xExtent));

    if (FAILED(wiasWritePropLong(wiasContext, WIA_IPA_PIXELS_PER_LINE,
                                 checked.corrected.xExtent)) ||
        FAILED(wiasWritePropLong(wiasContext, WIA_IPA_NUMBER_OF_LINES,
                                 checked.corrected.yExtent)) ||
        FAILED(wiasWritePropLong(wiasContext, WIA_IPA_BYTES_PER_LINE,
                                 static_cast<LONG>(geometry.bytesPerLine))) ||
        FAILED(wiasWritePropLong(wiasContext, WIA_IPA_ITEM_SIZE,
                                 static_cast<LONG>(size.value())))) {
        return E_FAIL;
    }

    return S_OK;
}

STDMETHODIMP G2710MiniDrv::drvWriteItemProperties(BYTE* wiasContext, LONG /*flags*/,
                                                  PMINIDRV_TRANSFER_CONTEXT /*transfer*/,
                                                  LONG* deviceError) {
    if (deviceError != nullptr) {
        *deviceError = 0;
    }
    // Vrednosti se u uredjaj upisuju tek pri prenosu (ScanSession::begin), jer
    // tek tada znamo da se zaista skenira. Ovde se samo potvrdjuje da su
    // upotrebljive.
    LONG ignored = 0;
    return drvValidateItemProperties(wiasContext, 0, 0, nullptr, &ignored);
}

STDMETHODIMP G2710MiniDrv::drvReadItemProperties(BYTE* /*wiasContext*/, LONG /*flags*/,
                                                 ULONG /*count*/, const PROPSPEC* /*propSpec*/,
                                                 LONG* deviceError) {
    if (deviceError != nullptr) {
        *deviceError = 0;
    }
    // Nijedna nasa osobina se ne cita sa uredjaja u trenutku citanja - sve su
    // vec u WIA skladistu. Uredjaj se ne dira bez potrebe.
    return S_OK;
}

// --- zakljucavanje ---------------------------------------------------------

STDMETHODIMP G2710MiniDrv::drvLockWiaDevice(BYTE* /*wiasContext*/, LONG /*flags*/,
                                            LONG* deviceError) {
    if (deviceError != nullptr) {
        *deviceError = 0;
    }
    // Ista arbitraza kao za STI - jedan Global\ mutex za sve klijente.
    return owner_->LockDevice();
}

STDMETHODIMP G2710MiniDrv::drvUnLockWiaDevice(BYTE* /*wiasContext*/, LONG /*flags*/,
                                              LONG* deviceError) {
    if (deviceError != nullptr) {
        *deviceError = 0;
    }
    return owner_->UnLockDevice();
}

// --- ostalo ----------------------------------------------------------------

STDMETHODIMP G2710MiniDrv::drvAnalyzeItem(BYTE* /*wiasContext*/, LONG /*flags*/,
                                          LONG* deviceError) {
    if (deviceError != nullptr) {
        *deviceError = 0;
    }
    // Prepoznavanje vise slika na staklu je 1.1; do tada je jedna stavka jedna
    // slika. Vracanje E_NOTIMPL bi aplikaciju navelo da misli da je greska.
    return S_FALSE;
}

STDMETHODIMP G2710MiniDrv::drvDeleteItem(BYTE* /*wiasContext*/, LONG /*flags*/,
                                         LONG* deviceError) {
    if (deviceError != nullptr) {
        *deviceError = 0;
    }
    // Stablo je fiksno; brisanje flatbeda nema smisla.
    return E_NOTIMPL;
}

STDMETHODIMP G2710MiniDrv::drvGetDeviceErrorStr(LONG /*flags*/, LONG deviceError,
                                                LPOLESTR* text, LONG* newDeviceError) {
    if (text == nullptr || newDeviceError == nullptr) {
        return E_POINTER;
    }
    *newDeviceError = 0;

    // Poruke su za korisnika koji ne zna sta je ADC ni DMA.
    switch (static_cast<ErrorCode>(deviceError)) {
        case ErrorCode::Busy:
            *text = const_cast<LPOLESTR>(L"Skener trenutno koristi drugi program.");
            break;
        case ErrorCode::TransportLost:
            *text = const_cast<LPOLESTR>(L"Veza sa skenerom je prekinuta. Proverite kabl.");
            break;
        case ErrorCode::DeviceNotFound:
            *text = const_cast<LPOLESTR>(L"Uredjaj na ovom portu nije HP ScanJet G2710.");
            break;
        case ErrorCode::Timeout:
            *text = const_cast<LPOLESTR>(L"Skener nije odgovorio na vreme.");
            break;
        case ErrorCode::Cancelled:
            *text = const_cast<LPOLESTR>(L"Skeniranje je prekinuto.");
            break;
        default:
            *text = const_cast<LPOLESTR>(L"Skeniranje nije uspelo.");
            break;
    }
    return S_OK;
}

STDMETHODIMP G2710MiniDrv::drvDeviceCommand(BYTE* /*wiasContext*/, LONG /*flags*/,
                                            const GUID* command, IWiaDrvItem** newItem,
                                            LONG* deviceError) {
    if (command == nullptr || deviceError == nullptr) {
        return E_POINTER;
    }
    *deviceError = 0;
    if (newItem != nullptr) {
        *newItem = nullptr;
    }

    if (*command == WIA_CMD_SYNCHRONIZE) {
        // Osvezavanje stanja je citanje, ne kretanje.
        G2710Device* device = owner_->device();
        if (device == nullptr) {
            return WIA_ERROR_OFFLINE;
        }
        const auto home = device->isHeadAtHome();
        return home ? S_OK : WIA_ERROR_OFFLINE;
    }
    return E_NOTIMPL;
}

STDMETHODIMP G2710MiniDrv::drvGetCapabilities(BYTE* /*wiasContext*/, LONG flags, LONG* count,
                                              WIA_DEV_CAP_DRV** capabilities,
                                              LONG* deviceError) {
    if (count == nullptr || capabilities == nullptr || deviceError == nullptr) {
        return E_POINTER;
    }
    *deviceError = 0;

    // WIA trazi ili dogadjaje, ili naredbe, ili oboje. Prva tri unosa su
    // dogadjaji, cetvrti je naredba - zato se odgovara isecanjem, ne granama.
    constexpr LONG kEventCount = 3;

    if ((flags & WIA_DEVICE_COMMANDS) != 0 && (flags & WIA_DEVICE_EVENTS) == 0) {
        *count = kCapabilityCount - kEventCount;
        *capabilities = &g_capabilities[kEventCount];
        return S_OK;
    }
    if ((flags & WIA_DEVICE_EVENTS) != 0 && (flags & WIA_DEVICE_COMMANDS) == 0) {
        *count = kEventCount;
        *capabilities = g_capabilities;
        return S_OK;
    }

    *count = kCapabilityCount;
    *capabilities = g_capabilities;
    return S_OK;
}

STDMETHODIMP G2710MiniDrv::drvGetWiaFormatInfo(BYTE* /*wiasContext*/, LONG /*flags*/,
                                               LONG* count, WIA_FORMAT_INFO** formats,
                                               LONG* deviceError) {
    if (count == nullptr || formats == nullptr || deviceError == nullptr) {
        return E_POINTER;
    }
    *deviceError = 0;

    std::vector<WIA_FORMAT_INFO>& info = formatInfo();
    *count = static_cast<LONG>(info.size());
    *formats = info.data();
    return S_OK;
}

STDMETHODIMP G2710MiniDrv::drvNotifyPnpEvent(const GUID* /*event*/, BSTR /*deviceId*/,
                                             ULONG /*reserved*/) {
    // Uredjaj se ne otvara na PnP dogadjaj; otvara ga IStiUSD::Initialize.
    return S_OK;
}

}  // namespace g2710::wia
