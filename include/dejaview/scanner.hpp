#pragma once //Include the header only once
#include<cstdint> //provides fixed sixe integer types
#include<string>
#include<filesystem>
#include<vector>

namespace dejaview {
    enum class ImageFormat {Jpeg, Png}; //Types of image supported, strongly typed
    struct FileEntry { //Stores info about one image
        std:: filesystem::path path;
        std::uintmax_t size_bytes = 0; //unimax_t is the larget unsigned integer type
        std::filesystem::file_time_type mtime{}; //Stores last modification time. Used for caching
        ImageFormat format = ImageFormat::Jpeg; //Default is Jpeg
        bool extension_mismatch = false; //True if: Jped files renamed as png and vice versa
    };

    struct ScanError { //For error encountered whilr scanning the directory fo images
        std:: filesystem::path path;
        std :: string message;
    };

    struct ScanOptions { //Stores user selected scanning options
        bool follow_symlinks = false; //No scanning of linked folders
        std::uintmax_t min_file_size = 1; //To ignore files smaller thnan this. default is 1
    };

    struct ScanResult { //Contains everything produced by the scan
        std :: vector<FileEntry> files; //Stores every image found
        std::vector<ScanError> errors; //    "     "   error encountered
        std::size_t entries_visited = 0; //How many images checked
        std:: size_t skipped_non_image = 0; //Ignored beacuse non - image
    };

    //This function scans directories andd return ScanResult
    //Param 1: const std::vector<std::filesystem::path>& roots => A list of directories to scan.
    // const &: Pass by reference (for efficiency) and const so thet the function can't modify
    //Param 2: Scanning options
    ScanResult scan_directories(const std::vector<std::filesystem::path>& roots,
        const ScanOptions& options = {});
}