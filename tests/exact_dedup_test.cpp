//
// Created by shrey on 24-Jul-26.
//

#include <gtest/gtest.h>

#include <fstream>
#include <string>

//Modules to be tested
#include "dejaview/exact_dedup.hpp"
#include "dejaview/hashing.hpp"
#include "dejaview/scanner.hpp"

namespace fs = std::filesystem;


//ExactDedupTest is a test fixture: a class that contains common setup code shared by multiple tests.
class ExactDedupTest : public ::testing::Test {
protected:
    void SetUp() override { //Make temp files
        dir_ = fs::temp_directory_path() / "dejaview_dedup_test";
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); } //Delete temp files

    // Writes a fake "image": JPEG magic bytes + given body.
    fs::path make(const std::string& name, const std::string& body) {
        const fs::path p = dir_ / name;
        std::ofstream out(p, std::ios::binary);
        out.put('\xFF'); out.put('\xD8'); out.put('\xFF'); //Write magic numbers for jpeg file
        out.write(body.data(), static_cast<std::streamsize>(body.size())); //Writ the rest of the content as specisfoed by individual test cases
        return p;
    }

    fs::path dir_;
};

TEST_F(ExactDedupTest, EmptyFileHashIsFnvOffsetBasis) {
    const fs::path p = dir_ / "empty.bin";
    std::ofstream(p, std::ios::binary).close();
    std::uint64_t h = 0;
    std::string err;
    ASSERT_TRUE(dejaview::hash_file(p, h, err));
    EXPECT_EQ(h, 0xcbf29ce484222325ULL);  // FNV-1a offset basis, by definition
}

TEST_F(ExactDedupTest, FindsIdenticalFilesAcrossNames) {
    make("a.jpg", "same-content");
    make("b.jpg", "same-content");
    make("c.jpg", "same-content");
    make("different.jpg", "other-stuff!");  // same size as nothing relevant

    const auto scan = dejaview::scan_directories({dir_});
    const auto dupes = dejaview::find_exact_duplicates(scan.files);

    ASSERT_EQ(dupes.groups.size(), 1u);
    EXPECT_EQ(dupes.groups[0].size(), 3u);
    EXPECT_TRUE(dupes.errors.empty());
}

TEST_F(ExactDedupTest, SameSizeDifferentContentIsNotADuplicate) {
    make("x.jpg", "aaaaaaaa");
    make("y.jpg", "bbbbbbbb");  // identical size, different bytes

    const auto scan = dejaview::scan_directories({dir_});
    const auto dupes = dejaview::find_exact_duplicates(scan.files);

    EXPECT_TRUE(dupes.groups.empty());
    EXPECT_EQ(dupes.files_hashed, 2u);  // same size forced hashing...
}

TEST_F(ExactDedupTest, UniqueSizesAreNeverHashed) {
    make("one.jpg", "short");
    make("two.jpg", "much-longer-content-here");

    const auto scan = dejaview::scan_directories({dir_});
    const auto dupes = dejaview::find_exact_duplicates(scan.files);

    EXPECT_TRUE(dupes.groups.empty());
    EXPECT_EQ(dupes.files_hashed, 0u);  // ...but unique sizes skip I/O entirely
}

TEST_F(ExactDedupTest, ReclaimableBytesCountsExtraCopiesOnly) {
    make("a.jpg", "0123456789");  // 13 bytes with magic prefix
    make("b.jpg", "0123456789");
    const auto scan = dejaview::scan_directories({dir_});
    const auto dupes = dejaview::find_exact_duplicates(scan.files);
    ASSERT_EQ(dupes.groups.size(), 1u);
    EXPECT_EQ(dupes.reclaimable_bytes, 13u);  // one redundant copy of 13 bytes
}