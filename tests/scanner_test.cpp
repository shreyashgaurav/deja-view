//
// Created by shrey on 23-Jul-26.
//

#include<gtest/gtest.h>
#include<fstream>
#include <algorithm>
#include "dejaview/scanner.hpp"

#include <unistd.h>

#include <gtest/gtest.h>

namespace fs = std :: filesystem;
using dejaview::ImageFormat;

class ScannerTest : public :: testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / "dejaview_scanner_test";
        fs::remove_all(dir_);;
        fs::create_directories(dir_ / "sub");

        write(dir_ / "real.jpg", {0xFF, 0xD8, 0xFF, 0xE0, 'j', 'u', 'n', 'k'});
        write(dir_ / "sub" / "real.png", {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 'x'});
        write(dir_ / "notes.txt", {'h', 'e', 'l', 'l', 'o'});
        write(dir_ / "fake.jpg", {'n', 'o', 't', ' ', 'j', 'p', 'g'});
        write(dir_ / "sneaky.dat", {0xFF, 0xD8, 0xFF, 0xE0, 'd', 'a', 't', 'a'}); // Jpge in disuise (only extension is different)
    }
    void TearDown() override {fs::remove_all(dir_); }
    void write(const fs::path& p, std::initializer_list<unsigned char> bytes) {
        std :: ofstream out(p, std::ios::binary);
        for (unsigned char b : bytes) out.put(static_cast<char>(b));
    }
    fs::path dir_;
};

TEST_F(ScannerTest, FindsImageByContentNotExtension) {
    const auto result = dejaview::scan_directories({dir_});
    ASSERT_EQ(result.files.size(), 3u); //real.jpg, real.png, sneaky.dat
    std :: size_t jpegs = 0, pngs = 0, mismatches = 0;
    for (const auto& f : result.files) {
        (f.format == ImageFormat::Jpeg ? jpegs : pngs)++;
        if (f.extension_mismatch) ++mismatches;
    }
    EXPECT_EQ(jpegs, 2u);
    EXPECT_EQ(pngs, 1u);
    EXPECT_EQ(mismatches, 1u);
    EXPECT_EQ(result.skipped_non_image, 2u);
    EXPECT_TRUE(result.errors.empty());

}

TEST_F(ScannerTest, NonexistentRootIsAnErrorNotACrash) {
    const auto result = dejaview::scan_directories({dir_ / "does_not_exist"});
    EXPECT_TRUE(result.files.empty());
    ASSERT_EQ(result.errors.size(), 1u);
}

TEST_F(ScannerTest, RecursesIntoSubdirectories) {
    const auto result = dejaview::scan_directories({dir_});
    const bool found_nested = std::any_of(
        result.files.begin(), result.files.end(), [&](const auto& f) {return f.path.filename() == "real.png";});
    EXPECT_TRUE(found_nested);
}