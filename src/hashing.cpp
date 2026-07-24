//
// Created by shrey on 24-Jul-26.
//

#include "dejaview/hashing.hpp"
#include<array>
#include<fstream>

namespace dejaview {
    namespace {
        //Values taken in the standard FNV-1a (Fowler-Noll-Vo) hashing algorithm
        //Link: https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
        constexpr std :: uint64_t kFnvOffsetBasic = 0xcbf29ce484222325ULL; //Starting value of the hash
        constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL; //Another value required by the FNV-1a algo
        constexpr std :: size_t kBufSize = 64 * 1024; //65536 Bytes = 64 KB => Read the file 64 KB at a time
    }

    //This function's job is: File -> 64-bit fingerprint
    bool hash_file(const std::filesystem::path &p, std::uint64_t &hash_out, std::string &error) {
        std :: ifstream in(p, std::ios::binary); //Pointer 'in' points to the start of the file
        if (!in) {
            error = "Can not open for hashing";
            return false;
        }
        std :: uint64_t h = kFnvOffsetBasic; // h is the running fingerprint. Here, we initialize the hash
        std:: array<char, kBufSize> buf; //Stores the current kBufSize bytes chunk of the file
        while (in) {
            in.read(buf.data(), static_cast<std::streamsize>(buf.size())); //Read one chunk of size kBufSize. (The last read might be of  < kBufSize. So we are using the actual array 'buf' size)
            const std :: streamsize n = in.gcount(); //How many bytes were read
            for (std :: streamsize i = 0; i < n; ++i) { //Logic of FNV-1a algorithm
                h ^= static_cast<unsigned char>(buf[i]);
                h *= kFnvPrime;
            }
        }
        if (in.bad()) { //Error happend. EOF not reached
            error = "read error while hashing";
            return false;
        }
        //EOF reached
        hash_out = h;
        return true;
    }

    //Runs only for the images whose hashes match. Two different files with the same fingerprint
    bool files_identical(const std::filesystem::path &a, const std::filesystem::path &b, bool &identical_out, std::string &error) {
        std :: ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
        if (!fa || !fb) {
            error = "cannot open for comparison";
            return false;
        }
        std :: array<char, kBufSize> ba, bb;
        while (true) {
            fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
            fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
            const std :: streamsize na = fa.gcount(), nb = fb.gcount();
            if (fa.bad() || fb.bad()) {
                error = "read error while comparing";
                return false;
            }
            if (na != nb || !std::equal(ba.begin(), ba.begin() + na, bb.begin())) {
                identical_out = false;
                return true;
            }
            if (na == 0) break;
        }
        identical_out = true;
        return true;
    }
}
