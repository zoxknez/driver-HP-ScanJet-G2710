// Upis PNM fajla.
//
// Format je izabran zato sto ne sakriva nista: zaglavlje je citljivo, podaci
// su sirovi. Testovi zato proveravaju BAJTOVE, ne "da li se otvara" - kada sa
// prijateljevog uredjaja stigne slika, mora se moci reci sta je tacno na
// hiljaditom mestu.

#include "ImageOutput.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace g2710;
using namespace g2710::cli;

namespace {

class PnmWriterTest : public ::testing::Test {
protected:
    std::filesystem::path path;

    void SetUp() override {
        path = std::filesystem::temp_directory_path() /
               ("g2710-pnm-" + std::to_string(::testing::UnitTest::GetInstance()
                                                  ->current_test_info()
                                                  ->line()) +
                ".pnm");
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    std::vector<std::uint8_t> readBack() const {
        std::ifstream file(path, std::ios::binary);
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
    }

    std::string headerOf(const std::vector<std::uint8_t>& data, std::size_t fields) const {
        std::string header;
        std::size_t newlines = 0;
        for (std::uint8_t byte : data) {
            header.push_back(static_cast<char>(byte));
            if (byte == '\n' && ++newlines == fields) {
                break;
            }
        }
        return header;
    }
};

}  // namespace

TEST_F(PnmWriterTest, ColorHeaderIsP6WithMaxValue) {
    auto writer = PnmWriter::create(path.string(), image::ColorMode::Color, 8, 4, 2);
    ASSERT_TRUE(writer);
    std::unique_ptr<PnmWriter> output{writer.value()};

    const std::vector<std::uint8_t> line(4 * 3, 0x40);
    ASSERT_TRUE(output->writeLine(line));
    ASSERT_TRUE(output->writeLine(line));
    ASSERT_TRUE(output->close());

    const auto data = readBack();
    EXPECT_EQ(headerOf(data, 3), "P6\n4 2\n255\n");
    EXPECT_EQ(data.size(), 11u + 24u);
}

TEST_F(PnmWriterTest, GrayHeaderIsP5AndLineartIsP4WithoutMaxValue) {
    {
        auto writer = PnmWriter::create(path.string(), image::ColorMode::Gray, 8, 8, 1);
        ASSERT_TRUE(writer);
        std::unique_ptr<PnmWriter> output{writer.value()};
        ASSERT_TRUE(output->writeLine(std::vector<std::uint8_t>(8, 0x11)));
        ASSERT_TRUE(output->close());
        EXPECT_EQ(headerOf(readBack(), 3), "P5\n8 1\n255\n");
    }
    {
        auto writer = PnmWriter::create(path.string(), image::ColorMode::Lineart, 8, 8, 1);
        ASSERT_TRUE(writer);
        std::unique_ptr<PnmWriter> output{writer.value()};
        ASSERT_TRUE(output->writeLine(std::vector<std::uint8_t>(1, 0xAA)));
        ASSERT_TRUE(output->close());

        const auto data = readBack();
        EXPECT_EQ(headerOf(data, 2), "P4\n8 1\n") << "P4 nema polje maksimuma";
        EXPECT_EQ(data.back(), 0xAA);
    }
}

TEST_F(PnmWriterTest, SixteenBitHeaderCarriesTheLargerMaximum) {
    auto writer = PnmWriter::create(path.string(), image::ColorMode::Gray, 16, 2, 1);
    ASSERT_TRUE(writer);
    std::unique_ptr<PnmWriter> output{writer.value()};
    ASSERT_TRUE(output->writeLine(std::vector<std::uint8_t>{0x34, 0x12, 0x78, 0x56}));
    ASSERT_TRUE(output->close());

    EXPECT_EQ(headerOf(readBack(), 3), "P5\n2 1\n65535\n");
}

// PNM je big-endian po specifikaciji, a cevovod radi little-endian. Bez
// obrtanja svaka 16-bitna slika izgleda kao sum.
TEST_F(PnmWriterTest, SixteenBitSamplesAreWrittenBigEndian) {
    auto writer = PnmWriter::create(path.string(), image::ColorMode::Gray, 16, 2, 1);
    ASSERT_TRUE(writer);
    std::unique_ptr<PnmWriter> output{writer.value()};

    // Little-endian ulaz: 0x1234 pa 0x5678.
    ASSERT_TRUE(output->writeLine(std::vector<std::uint8_t>{0x34, 0x12, 0x78, 0x56}));
    ASSERT_TRUE(output->close());

    const auto data = readBack();
    ASSERT_GE(data.size(), 4u);
    const std::size_t pixels = data.size() - 4;

    EXPECT_EQ(data[pixels + 0], 0x12) << "visi bajt mora ici prvi";
    EXPECT_EQ(data[pixels + 1], 0x34);
    EXPECT_EQ(data[pixels + 2], 0x56);
    EXPECT_EQ(data[pixels + 3], 0x78);
}

TEST_F(PnmWriterTest, EightBitSamplesAreWrittenUntouched) {
    auto writer = PnmWriter::create(path.string(), image::ColorMode::Gray, 8, 4, 1);
    ASSERT_TRUE(writer);
    std::unique_ptr<PnmWriter> output{writer.value()};
    ASSERT_TRUE(output->writeLine(std::vector<std::uint8_t>{1, 2, 3, 4}));
    ASSERT_TRUE(output->close());

    const auto data = readBack();
    ASSERT_GE(data.size(), 4u);
    const std::size_t pixels = data.size() - 4;
    EXPECT_EQ(data[pixels + 0], 1);
    EXPECT_EQ(data[pixels + 3], 4);
}

// Zaglavlje vec tvrdi koliko redova ima. Fajl sa manje redova je pokvaren i to
// se mora reci odmah, a ne ostaviti citaocu da otkrije.
TEST_F(PnmWriterTest, ClosingWithTooFewLinesIsAnError) {
    auto writer = PnmWriter::create(path.string(), image::ColorMode::Gray, 8, 4, 3);
    ASSERT_TRUE(writer);
    std::unique_ptr<PnmWriter> output{writer.value()};

    ASSERT_TRUE(output->writeLine(std::vector<std::uint8_t>(4, 0)));
    ASSERT_TRUE(output->writeLine(std::vector<std::uint8_t>(4, 0)));

    const Status closed = output->close();
    ASSERT_FALSE(closed) << "dva reda tamo gde zaglavlje obecava tri";
    EXPECT_EQ(closed.error().code, ErrorCode::ShortTransfer);
    EXPECT_EQ(output->linesWritten(), 2);
}

TEST_F(PnmWriterTest, ClosingWithEveryLineWrittenSucceeds) {
    auto writer = PnmWriter::create(path.string(), image::ColorMode::Gray, 8, 4, 3);
    ASSERT_TRUE(writer);
    std::unique_ptr<PnmWriter> output{writer.value()};

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(output->writeLine(std::vector<std::uint8_t>(4, 0)));
    }
    EXPECT_TRUE(output->close());
    EXPECT_EQ(output->linesWritten(), 3);
}

TEST_F(PnmWriterTest, EmptyImageAndUnwritablePathAreRefused) {
    EXPECT_FALSE(PnmWriter::create(path.string(), image::ColorMode::Gray, 8, 0, 5));
    EXPECT_FALSE(PnmWriter::create(path.string(), image::ColorMode::Gray, 8, 5, 0));

    const auto bad = PnmWriter::create("", image::ColorMode::Gray, 8, 4, 1);
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error().code, ErrorCode::Internal);
}

TEST_F(PnmWriterTest, WritingAfterCloseIsRefused) {
    auto writer = PnmWriter::create(path.string(), image::ColorMode::Gray, 8, 4, 1);
    ASSERT_TRUE(writer);
    std::unique_ptr<PnmWriter> output{writer.value()};

    ASSERT_TRUE(output->writeLine(std::vector<std::uint8_t>(4, 0)));
    ASSERT_TRUE(output->close());

    const Status refused = output->writeLine(std::vector<std::uint8_t>(4, 0));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidState);
}

TEST_F(PnmWriterTest, ExtensionMatchesTheMode) {
    EXPECT_STREQ(pnmExtension(image::ColorMode::Color), "ppm");
    EXPECT_STREQ(pnmExtension(image::ColorMode::Gray), "pgm");
    EXPECT_STREQ(pnmExtension(image::ColorMode::Lineart), "pbm");
}
