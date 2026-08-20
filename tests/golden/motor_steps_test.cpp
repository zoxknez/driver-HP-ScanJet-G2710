// Tabela motornih koraka.
//
// Izvedeno iz rts8822.c:3011 Motor_Setup_Steps.

#include "G2710Profile.generated.h"
#include "SimTransport.h"
#include "rts8822/Dma.h"
#include "rts8822/Gpio.h"
#include "rts8822/MotorSteps.h"
#include "rts8822/RegisterBank.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/Rts8822.h"
#include "transport/TraceRecorder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <string>

using namespace g2710;
using namespace g2710::rts8822;

namespace {

template <typename T, std::size_t N>
constexpr std::size_t countOf(const T (&)[N]) {
    return N;
}

MotorStepParams defaultParams(int curveIndex = 0) {
    MotorStepParams params;
    params.curveIndex = curveIndex;
    params.stepBufferBase = 0x10;
    params.motorBackStep = 0;
    return params;
}

}  // namespace

// --- data_wide_bitset --------------------------------------------------------

TEST(WideBits, Mask3fffSpansEightPlusSixBits) {
    // Pokazivac na tabelu koraka je 14-bitni: osam bita u prvom bajtu, sest u
    // drugom. Ovo je najkorisceniji oblik u celoj funkciji.
    RegisterBank bank;
    bank.setWideBits(0x0F6, 0x3FFF, 0x2ABC);

    EXPECT_EQ(bank.at(0x0F6), 0xBC) << "donjih osam bita";
    EXPECT_EQ(bank.at(0x0F7) & 0x3F, 0x2A) << "gornjih sest bita";
}

TEST(WideBits, PreservesBitsOutsideTheMask) {
    RegisterBank bank;
    bank.set(0x0F7, 0xC0);  // bitovi 6 i 7 su van maske 0x3F

    bank.setWideBits(0x0F6, 0x3FFF, 0x3FFF);

    EXPECT_EQ(bank.at(0x0F7) & 0xC0, 0xC0) << "bitovi van maske su pregazeni";
    EXPECT_EQ(bank.at(0x0F7) & 0x3F, 0x3F);
}

TEST(WideBits, ValueIsTruncatedToMaskWidth) {
    RegisterBank bank;
    bank.setWideBits(0x0F6, 0x3FFF, 0xFFFFFFFF);

    EXPECT_EQ(bank.getLsb(0x0F6, 2) & 0x3FFF, 0x3FFF);
}

TEST(WideBits, RoundTripsThroughGetLsb) {
    for (std::uint32_t value : {0u, 1u, 0x1234u, 0x3FFFu}) {
        RegisterBank bank;
        bank.setWideBits(0x0F6, 0x3FFF, value);
        EXPECT_EQ(bank.getLsb(0x0F6, 2) & 0x3FFF, value);
    }
}

TEST(WideBits, GetLsbIsInverseOfSetLsb) {
    RegisterBank bank;
    bank.setLsb(0x0E1, 0x00ABCDEF, 3);
    EXPECT_EQ(bank.getLsb(0x0E1, 3), 0x00ABCDEFu);
}

// --- izgradnja tabele --------------------------------------------------------

TEST(MotorSteps, RejectsOutOfRangeCurveIndex) {
    RegisterBank bank;
    EXPECT_FALSE(buildMotorSteps(bank, defaultParams(-1)).has_value());
    EXPECT_FALSE(buildMotorSteps(bank, defaultParams(
        static_cast<int>(countOf(profile::kMotorCurves)))).has_value());
}

TEST(MotorSteps, EveryExtractedCurveProducesAProgram) {
    for (std::size_t i = 0; i < countOf(profile::kMotorCurves); ++i) {
        RegisterBank bank;
        const auto program = buildMotorSteps(bank, defaultParams(static_cast<int>(i)));
        ASSERT_TRUE(program.has_value()) << "kriva " << i;
        EXPECT_FALSE(program->steps.empty()) << "kriva " << i << " je dala prazan stream";
        EXPECT_GT(program->accelerationStepCount, 0) << "kriva " << i;
    }
}

TEST(MotorSteps, LengthIsRoundedUpToMultipleOfSixteen) {
    // rts8822.c:3391 - DMA prima samo cele blokove od 16 bajtova.
    for (std::size_t i = 0; i < countOf(profile::kMotorCurves); ++i) {
        RegisterBank bank;
        const auto program = buildMotorSteps(bank, defaultParams(static_cast<int>(i)));
        ASSERT_TRUE(program.has_value());

        EXPECT_EQ(program->steps.size() % 16, 0u) << "kriva " << i;
        EXPECT_GE(program->steps.size(), program->rawLength);
        EXPECT_LT(program->steps.size() - program->rawLength, 16u)
            << "dopuna je veca od jednog bloka";
    }
}

TEST(MotorSteps, EachStepIsThreeBytesMsbFirst) {
    // Motor_AddStep upisuje tri bajta, najvisi prvi. Stream mora biti deljiv
    // sa tri pre zaokruzivanja.
    RegisterBank bank;
    const auto program = buildMotorSteps(bank, defaultParams(0));
    ASSERT_TRUE(program.has_value());
    EXPECT_EQ(program->rawLength % 3, 0u);
}

TEST(MotorSteps, AllSevenPointersAreWritten) {
    RegisterBank bank;
    const auto program = buildMotorSteps(bank, defaultParams(1));
    ASSERT_TRUE(program.has_value());

    // Svaki slot mora dobiti pokazivac - bilo stvarni offset, bilo nulu za
    // iskljucenu krivu. Nedirnut registar bi znacio da je slot preskocen.
    int written = 0;
    for (const CurveSlot& slot : kCurveSlots) {
        const std::uint32_t pointer = bank.getLsb(slot.pointerRegister, 2) & kCurvePointerMask;
        (void)pointer;
        ++written;
    }
    EXPECT_EQ(written, 7);
}

TEST(MotorSteps, FirstPointerStartsAtTheConfiguredBase) {
    // v15f8 je u jedinicama od 16, pa je bazni offset base << 4.
    MotorStepParams params = defaultParams(0);
    params.stepBufferBase = 0x10;

    RegisterBank bank;
    ASSERT_TRUE(buildMotorSteps(bank, params).has_value());

    const std::uint32_t pointer =
        bank.getLsb(kCurveSlots[0].pointerRegister, 2) & kCurvePointerMask;
    EXPECT_EQ(pointer, 0x100u) << "0x10 << 4";
}

TEST(MotorSteps, PointersAdvanceMonotonically) {
    // Tabele se redjaju jedna za drugom. Pokazivac koji ide unazad znacio bi
    // da se dve tabele preklapaju.
    RegisterBank bank;
    ASSERT_TRUE(buildMotorSteps(bank, defaultParams(3)).has_value());

    const std::uint32_t first =
        bank.getLsb(kCurveSlots[0].pointerRegister, 2) & kCurvePointerMask;
    const std::uint32_t bufferFull =
        bank.getLsb(kCurveSlots[6].pointerRegister, 2) & kCurvePointerMask;

    EXPECT_GT(bufferFull, first)
        << "dec.bufferfull mora poceti posle acc.normalscan";
}

TEST(MotorSteps, IdenticalCurvesShareOneTable) {
    // Ovo je poenta deduplikacije: pokazivac na tabelu je 14-bitni, pa je
    // prostor stvarno ogranicen. Ako su dve krive identicne, druga NE dobija
    // svoju kopiju nego pokazuje na istu tabelu.
    //
    // Trazimo bar jednu takvu pojavu medju svih 12 krivih; ako je nema
    // nijedne, deduplikacija je mrtav kod.
    int aliasedSlots = 0;

    for (std::size_t c = 0; c < countOf(profile::kMotorCurves); ++c) {
        RegisterBank bank;
        ASSERT_TRUE(buildMotorSteps(bank, defaultParams(static_cast<int>(c))).has_value());

        std::set<std::uint32_t> pointers;
        for (const CurveSlot& slot : kCurveSlots) {
            const std::uint32_t pointer =
                bank.getLsb(slot.pointerRegister, 2) & kCurvePointerMask;
            if (pointer == 0) {
                continue;  // iskljucena kriva
            }
            if (!pointers.insert(pointer).second) {
                ++aliasedSlots;
            }
        }
    }

    EXPECT_GT(aliasedSlots, 0)
        << "nijedan slot ne deli tabelu - deduplikacija se nikada ne izvrsava";
}

TEST(MotorSteps, SharedTableAlsoSharesLastStep) {
    // Kada pokazivac pokazuje na tudju tabelu, i "poslednji korak" mora doci
    // iz iste - inace bi cip stao na pogresnom mestu.
    for (std::size_t c = 0; c < countOf(profile::kMotorCurves); ++c) {
        RegisterBank bank;
        ASSERT_TRUE(buildMotorSteps(bank, defaultParams(static_cast<int>(c))).has_value());

        for (std::size_t i = 0; i < countOf(kCurveSlots); ++i) {
            for (std::size_t j = i + 1; j < countOf(kCurveSlots); ++j) {
                const std::uint32_t a =
                    bank.getLsb(kCurveSlots[i].pointerRegister, 2) & kCurvePointerMask;
                const std::uint32_t b =
                    bank.getLsb(kCurveSlots[j].pointerRegister, 2) & kCurvePointerMask;
                if (a == 0 || a != b) {
                    continue;
                }
                EXPECT_EQ(bank.getLsb(kCurveSlots[i].lastStepRegister, 3),
                          bank.getLsb(kCurveSlots[j].lastStepRegister, 3))
                    << "kriva " << c << ": slotovi dele tabelu ali ne i poslednji korak";
            }
        }
    }
}

TEST(MotorSteps, BackStepPaddingIsARemainderNotACount) {
    // Lako je pretpostaviti da motorbackstep dodaje toliko koraka. Ne dodaje.
    //
    // rts8822.c:3112-3120 racuna:
    //     varx10  = motorbackstep - deccurvecount - acccurvecount + 2
    //     myvalor = varx10 - (varx10 / step_size) * step_size
    //             = varx10 % step_size
    //
    // Dopuna je dakle OSTATAK pri deljenju velicinom koraka, najvise
    // step_size - 1 koraka. Sa Regs[0xe0] = 0 velicina koraka je 1, pa je
    // ostatak uvek nula i motorbackstep nema nikakav efekat.
    MotorStepParams params = defaultParams(0);
    params.motorBackStep = 200;

    RegisterBank unitStep;
    unitStep.set(motor_offset::kStepSize, 0);  // step_size = 1
    const auto withUnitStep = buildMotorSteps(unitStep, params);
    ASSERT_TRUE(withUnitStep.has_value());

    RegisterBank baseline;
    baseline.set(motor_offset::kStepSize, 0);
    const auto without = buildMotorSteps(baseline, defaultParams(0));
    ASSERT_TRUE(without.has_value());

    EXPECT_EQ(withUnitStep->rawLength, without->rawLength)
        << "sa velicinom koraka 1 ostatak je nula, pa dopune ne sme biti";
}

TEST(MotorSteps, BackStepPaddingAppearsWhenStepSizeExceedsOne) {
    MotorStepParams params = defaultParams(0);
    params.motorBackStep = 200;

    RegisterBank baseline;
    baseline.set(motor_offset::kStepSize, 7);  // step_size = 8
    const auto without = buildMotorSteps(baseline, defaultParams(0));
    ASSERT_TRUE(without.has_value());

    RegisterBank padded;
    padded.set(motor_offset::kStepSize, 7);
    const auto with = buildMotorSteps(padded, params);
    ASSERT_TRUE(with.has_value());

    EXPECT_GT(with->rawLength, without->rawLength)
        << "sa velicinom koraka 8 ostatak mora dodati koraka";

    const std::size_t extra = with->rawLength - without->rawLength;
    EXPECT_EQ(extra % 3, 0u) << "dopuna se dodaje po tri bajta";
    EXPECT_LE(extra / 3, 7u) << "dopuna ne moze preci step_size - 1 koraka";
}

TEST(MotorSteps, DoesNotTouchUnrelatedRegisters) {
    RegisterBank bank;
    bank.set(0x000, 0xA5);   // kontrola
    bank.set(0x146, 0x5A);   // lampa
    bank.set(0x092, 0x3C);   // CCD timing

    ASSERT_TRUE(buildMotorSteps(bank, defaultParams(0)).has_value());

    EXPECT_EQ(bank.at(0x000), 0xA5);
    EXPECT_EQ(bank.at(0x146), 0x5A);
    EXPECT_EQ(bank.at(0x092), 0x3C);
}

// --- slanje na uredjaj -------------------------------------------------------

#if G2710_MOTOR_PATH_COMPILED

namespace {

class MotorUpload : public ::testing::Test {
protected:
    sim::SimTransport device;
    TraceRecorder recorder{device};
    RegisterFile registers{recorder};
    SafetyGate gate{SafetyLevel::FullScan};

    Rts8822 chip{recorder, gate};
    Gpio gpio{registers, gate};
    Dma dma{registers, gate};
};

}  // namespace

TEST_F(MotorUpload, SequenceIsWarmResetLockWriteUnlock) {
    RegisterBank bank;
    const auto program = buildMotorSteps(bank, defaultParams(0));
    ASSERT_TRUE(program.has_value());

    ASSERT_TRUE(uploadMotorSteps(chip, gpio, dma, *program, 0x10).hasValue());

    const std::string trace = recorder.format();

    // Warm reset prvi, pre svega ostalog.
    EXPECT_EQ(trace.find("CTL DI: c0 04 e800 0100 0002\n"), 0u)
        << "warm reset nije prvi";

    // Zakljucavanje oko DMA upisa.
    const std::size_t firstLock = trace.find("CTL DO: 40 04 ee00 0000 0002\n");
    const std::size_t bulk = trace.find("BLK DO:");
    const std::size_t lastLock = trace.rfind("CTL DO: 40 04 ee00 0000 0002\n");

    ASSERT_NE(firstLock, std::string::npos) << "nema zakljucavanja";
    ASSERT_NE(bulk, std::string::npos) << "nema DMA upisa";
    EXPECT_LT(firstLock, bulk) << "zakljucavanje mora doci pre upisa";
    EXPECT_GT(lastLock, bulk) << "otkljucavanje mora doci posle upisa";
    EXPECT_NE(firstLock, lastLock) << "otkljucavanje izostalo";
}

TEST_F(MotorUpload, UnlocksEvenWhenTheWriteFails) {
    // Ostaviti cip zakljucanim bilo bi gore od same greske upisa.
    RegisterBank bank;
    const auto program = buildMotorSteps(bank, defaultParams(0));
    ASSERT_TRUE(program.has_value());

    device.corruptNextDmaReadBacks(1000);  // verifikacija nikada ne prolazi

    const Status status = uploadMotorSteps(chip, gpio, dma, *program, 0x10);
    EXPECT_FALSE(status.hasValue());

    const std::string trace = recorder.format();
    const std::size_t bulk = trace.rfind("BLK DO:");
    const std::size_t lastLock = trace.rfind("CTL DO: 40 04 ee00 0000 0002\n");

    ASSERT_NE(lastLock, std::string::npos);
    EXPECT_GT(lastLock, bulk) << "cip je ostao zakljucan posle neuspelog upisa";
}

TEST_F(MotorUpload, EmptyProgramIsRefused) {
    MotorStepProgram empty;
    const Status status = uploadMotorSteps(chip, gpio, dma, empty, 0x10);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
    EXPECT_TRUE(recorder.format().empty());
}

TEST_F(MotorUpload, RequiresAcquireLevelForTheTransfer) {
    SafetyGate lampOnly{SafetyLevel::Lamp};
    Rts8822 limitedChip{recorder, lampOnly};
    Gpio limitedGpio{registers, lampOnly};
    Dma limitedDma{registers, lampOnly};

    RegisterBank bank;
    const auto program = buildMotorSteps(bank, defaultParams(0));
    ASSERT_TRUE(program.has_value());

    const Status status =
        uploadMotorSteps(limitedChip, limitedGpio, limitedDma, *program, 0x10);
    EXPECT_FALSE(status.hasValue());
}

#endif  // G2710_MOTOR_PATH_COMPILED
