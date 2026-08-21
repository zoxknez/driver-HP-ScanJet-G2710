// Trajno cuvanje kalibracije.
//
// Kes stedi desetak sekundi pred svaki scan, ali samo ako je pouzdan. Zato
// testovi najvise paze na ono sto ide NAOPAKO: zapis druge verzije, zapis
// drugog uredjaja, presecen fajl. Lose procitana kalibracija je gora od
// nikakve - ona unisti sliku i izgleda kao kvar senzora.

#include "calib/CalibrationCache.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace g2710;
using namespace g2710::calib;

namespace {

class CalibrationCacheTest : public ::testing::Test {
protected:
    std::filesystem::path root;

    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("g2710-cache-" +
                std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    CalibrationCache cache() const { return CalibrationCache{root.string()}; }

    static CalibrationKey key(const std::string& device = "G2710-0001", int resolution = 300) {
        CalibrationKey result;
        result.deviceId = device;
        result.resolution = resolution;
        result.section = CalibrationSection::Reflective;
        result.depth = 8;
        return result;
    }

    static CalibrationRecord record(const CalibrationKey& forKey, std::size_t pixels = 8) {
        CalibrationRecord result;
        result.key = forKey;
        result.savedAtUnixSeconds = 1755000000;
        result.shading.pixelsPerLine = pixels;
        result.shading.darkOffset.assign(pixels * kChannels, 0.0);
        result.shading.gain.assign(pixels * kChannels, 0.0);

        for (std::size_t i = 0; i < pixels * kChannels; ++i) {
            result.shading.darkOffset[i] = 100.0 + static_cast<double>(i);
            result.shading.gain[i] = 1.0 + static_cast<double>(i) * 0.01;
        }
        result.gainOffset = initialGainOffset(1);
        result.gainOffset.gain = {7, 8, 9};
        result.gainOffset.evenDcg = {0x101, 0x102, 0x103};
        result.gainOffset.oddDcg = {0x111, 0x112, 0x113};
        return result;
    }
};

}  // namespace

// --- kljuc ------------------------------------------------------------------------

TEST_F(CalibrationCacheTest, KeyNameCarriesEveryPartOfTheKey) {
    CalibrationKey k = key("HP-G2710-abc", 600);
    k.depth = 16;
    EXPECT_EQ(k.fileName(), "HP-G2710-abc-600dpi-flatbed-16bit.cal");

    k.section = CalibrationSection::Transparent;
    EXPECT_EQ(k.fileName(), "HP-G2710-abc-600dpi-tma-16bit.cal");
}

// Oznaka uredjaja dolazi sa uredjaja i ne sme uneti razdelnik putanje.
TEST_F(CalibrationCacheTest, KeyNameIsSafeForTheFileSystem) {
    const CalibrationKey k = key("../../etc/passwd\\x:*?");
    const std::string name = k.fileName();

    EXPECT_EQ(name.find('/'), std::string::npos);
    EXPECT_EQ(name.find('\\'), std::string::npos);
    EXPECT_EQ(name.find(':'), std::string::npos);
    EXPECT_EQ(name.find(".."), std::string::npos);
}

TEST_F(CalibrationCacheTest, InvalidKeysAreRejected) {
    CalibrationKey empty;
    EXPECT_FALSE(empty.valid());

    CalibrationKey noResolution = key();
    noResolution.resolution = 0;
    EXPECT_FALSE(noResolution.valid());

    CalibrationKey oddDepth = key();
    oddDepth.depth = 12;
    EXPECT_FALSE(oddDepth.valid()) << "kes zna samo za 8 i 16 bita";

    EXPECT_FALSE(cache().load(empty));
    EXPECT_FALSE(cache().remove(empty));
}

// --- upis i citanje ----------------------------------------------------------------

TEST_F(CalibrationCacheTest, StoredRecordComesBackUnchanged) {
    const auto storage = cache();
    const CalibrationKey k = key();
    const CalibrationRecord written = record(k);

    ASSERT_TRUE(storage.store(written));
    EXPECT_TRUE(storage.contains(k));

    const auto read = storage.load(k);
    ASSERT_TRUE(read);
    const CalibrationRecord& got = read.value();

    EXPECT_TRUE(got.key == k);
    EXPECT_EQ(got.savedAtUnixSeconds, written.savedAtUnixSeconds);
    EXPECT_EQ(got.shading.pixelsPerLine, written.shading.pixelsPerLine);
    EXPECT_EQ(got.shading.darkOffset, written.shading.darkOffset);
    EXPECT_EQ(got.shading.gain, written.shading.gain);
    EXPECT_EQ(got.gainOffset.gain, written.gainOffset.gain);
    EXPECT_EQ(got.gainOffset.evenDcg, written.gainOffset.evenDcg);
    EXPECT_EQ(got.gainOffset.oddDcg, written.gainOffset.oddDcg);
}

// Nekalibrisano nije otkaz. Prvi scan na novom uredjaju mora proci mirno.
TEST_F(CalibrationCacheTest, MissingRecordIsEmptyNotAnError) {
    const auto storage = cache();
    const CalibrationKey k = key();

    EXPECT_FALSE(storage.contains(k));
    const auto read = storage.load(k);
    ASSERT_TRUE(read) << "odsustvo kalibracije nije greska";
    EXPECT_TRUE(read.value().empty());
}

TEST_F(CalibrationCacheTest, DirectoryIsCreatedOnFirstStore) {
    const auto storage = cache();
    EXPECT_FALSE(std::filesystem::exists(root));

    ASSERT_TRUE(storage.store(record(key())));
    EXPECT_TRUE(std::filesystem::exists(root));
}

// Cetiri stavke kljuca su cetiri razlicita zapisa - koeficijenti se ne dele.
TEST_F(CalibrationCacheTest, EachPartOfTheKeyGivesItsOwnRecord) {
    const auto storage = cache();

    CalibrationKey base = key("dev", 300);
    ASSERT_TRUE(storage.store(record(base, 4)));

    CalibrationKey otherResolution = base;
    otherResolution.resolution = 600;
    EXPECT_FALSE(storage.contains(otherResolution));

    CalibrationKey otherDepth = base;
    otherDepth.depth = 16;
    EXPECT_FALSE(storage.contains(otherDepth));

    CalibrationKey otherSection = base;
    otherSection.section = CalibrationSection::Transparent;
    EXPECT_FALSE(storage.contains(otherSection));

    CalibrationKey otherDevice = base;
    otherDevice.deviceId = "drugi";
    EXPECT_FALSE(storage.contains(otherDevice));
}

TEST_F(CalibrationCacheTest, StoringTwiceOverwrites) {
    const auto storage = cache();
    const CalibrationKey k = key();

    CalibrationRecord first = record(k, 4);
    ASSERT_TRUE(storage.store(first));

    CalibrationRecord second = record(k, 4);
    second.gainOffset.gain = {1, 2, 3};
    second.savedAtUnixSeconds = 1755000999;
    ASSERT_TRUE(storage.store(second));

    const auto read = storage.load(k);
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value().gainOffset.gain, second.gainOffset.gain);
    EXPECT_EQ(read.value().savedAtUnixSeconds, second.savedAtUnixSeconds);
}

// --- ono sto ide naopako -------------------------------------------------------------

TEST_F(CalibrationCacheTest, EmptyCoefficientsAreRefused) {
    const auto storage = cache();
    CalibrationRecord broken = record(key());
    broken.shading = ShadingCoefficients{};

    const Status refused = storage.store(broken);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
}

TEST_F(CalibrationCacheTest, CoefficientLengthMustMatchTheWidth) {
    const auto storage = cache();
    CalibrationRecord broken = record(key(), 8);
    broken.shading.gain.pop_back();

    const Status refused = storage.store(broken);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
}

TEST_F(CalibrationCacheTest, ForeignFileIsRejected) {
    std::filesystem::create_directories(root);
    const CalibrationKey k = key();
    {
        std::ofstream stream(root / k.fileName(), std::ios::binary);
        stream << "ovo nije kalibracija\n";
    }

    const auto read = cache().load(k);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().code, ErrorCode::DeviceError);
}

// Verzija formata postoji da bi stari zapis bio ODBIJEN, a ne procitan napola.
TEST_F(CalibrationCacheTest, DifferentFormatVersionIsRejected) {
    const auto storage = cache();
    const CalibrationKey k = key();
    ASSERT_TRUE(storage.store(record(k, 4)));

    std::string content;
    {
        std::ifstream stream(root / k.fileName(), std::ios::binary);
        content.assign((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
    }
    const std::size_t versionAt = content.find('\n') + 1;
    content[versionAt] = '9';
    {
        std::ofstream stream(root / k.fileName(), std::ios::binary | std::ios::trunc);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    const auto read = storage.load(k);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().code, ErrorCode::DeviceError);
}

TEST_F(CalibrationCacheTest, TruncatedFileIsRejected) {
    const auto storage = cache();
    const CalibrationKey k = key();
    ASSERT_TRUE(storage.store(record(k, 16)));

    const auto path = root / k.fileName();
    const auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size / 2);

    const auto read = storage.load(k);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().code, ErrorCode::DeviceError);
}

// Zapis pod tudjim imenom ne sme proci samo zato sto je fajl na pravom mestu.
TEST_F(CalibrationCacheTest, RecordBelongingToAnotherKeyIsRejected) {
    const auto storage = cache();
    const CalibrationKey mine = key("moj", 300);
    const CalibrationKey theirs = key("tudji", 300);

    ASSERT_TRUE(storage.store(record(theirs, 4)));
    std::filesystem::rename(root / theirs.fileName(), root / mine.fileName());

    const auto read = storage.load(mine);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().code, ErrorCode::DeviceError);
}

// --- brisanje --------------------------------------------------------------------------

TEST_F(CalibrationCacheTest, RemovingWorksAndRemovingNothingIsFine) {
    const auto storage = cache();
    const CalibrationKey k = key();
    ASSERT_TRUE(storage.store(record(k, 4)));

    EXPECT_TRUE(storage.remove(k));
    EXPECT_FALSE(storage.contains(k));
    EXPECT_TRUE(storage.remove(k)) << "brisanje nepostojeceg nije greska";
}

TEST_F(CalibrationCacheTest, RemovingAllForOneDeviceLeavesTheOthers) {
    const auto storage = cache();

    for (int resolution : {150, 300, 600}) {
        ASSERT_TRUE(storage.store(record(key("moj", resolution), 4)));
    }
    ASSERT_TRUE(storage.store(record(key("tudji", 300), 4)));

    const auto removed = storage.removeAllFor("moj");
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed.value(), 3);

    EXPECT_FALSE(storage.contains(key("moj", 300)));
    EXPECT_TRUE(storage.contains(key("tudji", 300)));
}

TEST_F(CalibrationCacheTest, RemovingAllFromAnEmptyCacheIsFine) {
    const auto removed = cache().removeAllFor("bilo-ko");
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed.value(), 0);

    EXPECT_FALSE(cache().removeAllFor(""));
}

TEST_F(CalibrationCacheTest, DefaultDirectoryIsUnderTheUserProfile) {
    const auto directory = CalibrationCache::defaultDirectory();
    ASSERT_TRUE(directory);
    EXPECT_NE(directory.value().find("G2710"), std::string::npos);
    EXPECT_NE(directory.value().find("calibration"), std::string::npos);
}
