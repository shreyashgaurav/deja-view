//
// Created by shrey on 24-Jul-26.
//

#pragma once //This says to Include this header file only once -> prevents redefinition errors
#include <cstdint> //For fixed integer types
#include<filesystem>
#include<string>

namespace dejaview {
    //Function defn of hash_file: param1-> file path, param2->output hash, param3->The error string message
    bool hash_file(const std ::filesystem::path& p, std::uint64_t& hash_out,
                    std::string& error);

    //Con=mpares two files: param 1 and param2: Files to be compared, param3: Same or not, param4: error message if any
    bool files_identical(const std :: filesystem::path& a,
        const std::filesystem::path& b, bool& identical_out,
        std :: string& error);
}