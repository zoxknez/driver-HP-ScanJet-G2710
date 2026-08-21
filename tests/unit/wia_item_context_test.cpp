// Kontekst WIA stavke.
//
// WIA servis drzi neprozirni blok bajtova i vraca ga pri svakom pozivu. Nas
// kod mora prezivati i slucaj kada taj blok NIJE nas - dvostruko oslobadjanje,
// tudji pokazivac, nulu. Na tudjem racunaru je razlika izmedju "drajver kaze
// da nesto nije u redu" i pada WIA servisa.

#include "WiaItemContext.h"

#include <gtest/gtest.h>

#include "SimTransport.h"
#include "rts8822/Lamp.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/ScanRegisters.h"
#include "scan/ScanPlanner.h"

#include <cstdint>
#include <memory>
#include <vector>

using namespace g2710;
using namespace g2710::wia;

TEST(WiaItemContext, RootAndFlatbedAreDistinguished) {
    ItemContext* root = createItemContext(ItemKind::Root);
    ItemContext* flatbed = createItemContext(ItemKind::Flatbed);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(flatbed, nullptr);

    EXPECT_EQ(root->kind, ItemKind::Root);
    EXPECT_EQ(flatbed->kind, ItemKind::Flatbed);
    EXPECT_STREQ(toString(ItemKind::Root), "Root");
    EXPECT_STREQ(toString(ItemKind::Flatbed), "Flatbed");

    destroyItemContext(root);
    destroyItemContext(flatbed);
}

// Flatbed dobija upotrebljive vrednosti odmah - aplikacija sme da skenira bez
// ijednog podesavanja.
TEST(WiaItemContext, FlatbedStartsWithDefaults) {
    ItemContext* flatbed = createItemContext(ItemKind::Flatbed);
    ASSERT_NE(flatbed, nullptr);

    if (hasUsableConfiguration()) {
        EXPECT_GT(flatbed->settings.xResolution, 0);
        EXPECT_EQ(flatbed->settings.xResolution, flatbed->settings.yResolution);
        EXPECT_GT(flatbed->settings.xExtent, 0);
    } else {
        // Izdanje pre kvalifikacije: nema sta da se podrazumeva, i to je tacno.
        EXPECT_EQ(flatbed->settings.xResolution, 0);
    }
    destroyItemContext(flatbed);
}

TEST(WiaItemContext, FreshContextIsNotTransferring) {
    ItemContext* flatbed = createItemContext(ItemKind::Flatbed);
    ASSERT_NE(flatbed, nullptr);

    EXPECT_TRUE(flatbed->valid());
    EXPECT_FALSE(flatbed->transferring());
    EXPECT_EQ(flatbed->deliveredLines, 0);

    destroyItemContext(flatbed);
}

// --- odbrana od tudjeg bloka ---------------------------------------------------

TEST(WiaItemContext, RecognisesItsOwnBlockAndRejectsOthers) {
    ItemContext* ours = createItemContext(ItemKind::Flatbed);
    ASSERT_NE(ours, nullptr);
    EXPECT_EQ(asItemContext(ours), ours);

    EXPECT_EQ(asItemContext(nullptr), nullptr);

    // Blok koji nije nas: potpis se ne poklapa.
    std::vector<std::uint8_t> foreign(sizeof(ItemContext), 0xAB);
    EXPECT_EQ(asItemContext(foreign.data()), nullptr) << "tudji blok je prihvacen";

    destroyItemContext(ours);
}

// WIA servis ume da posalje isti blok dvaput. Odbrana je brisanje potpisa PRE
// oslobadjanja - drugi poziv tada nailazi na nesto sto nije nas.
//
// Sam drugi poziv se ne moze testirati: citanje oslobodjene memorije je greska
// bez obzira na ishod. Zato se testira KORAK - posle ponistavanja blok se vise
// ne prepoznaje, pa ni destroyItemContext nad njim nista ne radi.
TEST(WiaItemContext, InvalidatingMakesTheBlockUnrecognisable) {
    ItemContext* context = createItemContext(ItemKind::Flatbed);
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(asItemContext(context), context);

    invalidateItemContext(context);

    EXPECT_FALSE(context->valid());
    EXPECT_EQ(asItemContext(context), nullptr)
        << "ponisten blok bi drugi put prosao kao ispravan";

    // Sada je bezbedno: destroyItemContext ga odbija i nista ne oslobadja.
    destroyItemContext(context);
    EXPECT_EQ(asItemContext(context), nullptr);

    // Pravo oslobadjanje, sada rukom - potpis je ponisten pa `destroy` nece.
    context->signature = kItemContextSignature;
    destroyItemContext(context);
}

TEST(WiaItemContext, InvalidatingNullIsHarmless) {
    invalidateItemContext(nullptr);
    SUCCEED();
}

TEST(WiaItemContext, DestroyingNullIsHarmless) {
    destroyItemContext(nullptr);
    SUCCEED();
}

// --- zavrsetak prenosa ----------------------------------------------------------

// endTransfer mora biti bezbedan i kada prenosa nije ni bilo - poziva se iz
// destruktora TransferGuard-a na svakom izlazu iz drvAcquireItemData.
TEST(WiaItemContext, EndTransferIsSafeWithoutASession) {
    ItemContext* context = createItemContext(ItemKind::Flatbed);
    ASSERT_NE(context, nullptr);

    context->deliveredLines = 17;
    EXPECT_TRUE(context->endTransfer());

    EXPECT_FALSE(context->transferring());
    EXPECT_FALSE(context->closedExplicitly) << "nije bilo sta da se zatvori";
    EXPECT_EQ(context->deliveredLines, 0) << "brojac se vraca na nulu";

    EXPECT_TRUE(context->endTransfer());  // dvaput zaredom
    EXPECT_FALSE(context->transferring());

    destroyItemContext(context);
}

// Kljucna provera: kontekst sa ZIVOM sesijom mora je uredno zatvoriti.
//
// Bez toga cip ostaje da skenira i posle poslednjeg reda koji je iko procitao -
// isti kvar koji je vec jednom nadjen u ScanSession, ovde bi se vratio kroz
// zaboravljen `finish()` u endTransfer.
TEST(WiaItemContext, EndTransferClosesALiveSession) {
    sim::SimTransport device;
    rts8822::RegisterFile registers{device};
    const SafetyGate gate{SafetyLevel::FullScan};

    device.ccd().makeIdeal();
    rts8822::Lamp lamp{registers, gate};
    ASSERT_TRUE(lamp.setLamp(rts8822::LampKind::Flatbed, true));
    ASSERT_TRUE(lamp.setupPwm(rts8822::LampKind::Flatbed));
    device.advanceTime(60000);

    scan::ScanRequest request;
    request.resolution = 300;
    request.colorMode = image::ColorMode::Gray;
    request.depth = 8;
    request.region = {0, 0, 64, 40};
    request.allowUnqualified = true;

    auto planned = scan::planScan(request);
    ASSERT_TRUE(planned);

    ItemContext* context = createItemContext(ItemKind::Flatbed);
    ASSERT_NE(context, nullptr);

    context->session =
        std::make_unique<scan::ScanSession>(registers, gate, planned.value());
    ASSERT_TRUE(context->session->begin());
    EXPECT_TRUE(context->transferring());

    // Procitaj jedan red, pa napusti prenos na pola.
    std::vector<std::uint8_t> line(context->session->outputBytesPerLine(), 0);
    ASSERT_TRUE(context->session->nextLine(line, context->cancellation));

    rts8822::ScanRegisters check{registers, gate};
    ASSERT_TRUE(check.isExecuting().value()) << "prolaz mora biti u toku";

    const Status closed = context->endTransfer();
    EXPECT_TRUE(closed) << "zatvaranje prolaza je palo";

    EXPECT_FALSE(context->transferring());
    EXPECT_TRUE(context->closedExplicitly)
        << "prolaz je zatvorila tek mreza u destruktoru, ne izricit poziv";

    const auto busy = check.isExecuting();
    ASSERT_TRUE(busy);
    EXPECT_FALSE(busy.value()) << "cip je ostao da skenira";

    destroyItemContext(context);
}

TEST(WiaItemContext, EndTransferClearsCancellation) {
    ItemContext* context = createItemContext(ItemKind::Flatbed);
    ASSERT_NE(context, nullptr);

    context->cancellation.cancel();
    EXPECT_TRUE(context->cancellation.isCancelled());

    EXPECT_TRUE(context->endTransfer());
    EXPECT_FALSE(context->cancellation.isCancelled())
        << "sledeci prenos bi krenuo kao vec otkazan";

    destroyItemContext(context);
}
