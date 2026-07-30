//
// Created by shrey on 23-Jul-26.
//

#include "dejaview/scanner.hpp"
#include <algorithm>
#include<array>
#include<fstream>

namespace fs = std::filesystem; //Alias for namespace

namespace dejaview {
    //the below namespace is Anonymous namespace: Everything inside this namespace is private to this source file
    //similar to making helper functions static
    namespace {
        //Magic Number Detection or Signature Sniffing:
        //Read the beginning of a file and determine whether it is actually a JPEG or PNG.
        //Param1: The file to inspect
        //Param2: The output Param. True: This si an image
        //Param3: enum class ImageFormat {Jpeg, png}
        //Param4: Error message
        bool sniff_format(const fs::path& p, bool& is_image, ImageFormat& format_out, std::string& error_out) {
            std::ifstream in(p, std::ios::binary); //Create an input file stream. Read raw bytes
            //"in" is a pointer to the beginning of the file
            if (!in) { //Opeingi fails
                error_out = "cannot open the file.";
                return false;
            }
            //Storing the first 8 bytes of the file in an array of 8 bytes
            //Size of unsigned char is 1 byte
            std::array<unsigned char, 8> buf{}; //All 8 bytes contain 0 as of now
            //buf.data(): returns the address of the first byte of buf: Start writing here
            //buf.size() = 8 : Read 8 bytes
            //.read(): Copies bytes from the file to the array
            //why reinterpret_cast?
                    //buf.data() return unsigned char*
                    //read wants char*
                    //reinterpret_cast<char*> simply tells the compiler: "Use this same block of memory as a char*."
            in.read(reinterpret_cast<char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
            const auto n = in.gcount();

            is_image = false;
            //Detecting Jpeg
            if (n >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
                is_image = true;
                format_out = ImageFormat::Jpeg;
            }
            else { //Detecting png. kPngSig stores the signature of png file
                static constexpr std :: array<unsigned char, 8> kPngSig = {
                    0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
                };
                if (n == 8 && std::equal(buf.begin(), buf.end(), kPngSig.begin())) {
                    is_image = true;
                    format_out = ImageFormat::Png;
                }
            }
            return true; //Means file was read successfully != File is an image (for that check is_image)
        }

        bool has_image_extension(const fs::path& p) {
            std:: string ext = p.extension().string(); //Get file ectension
            //change to lowercase
            std:: transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) {return std::tolower(c);});
            return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
        }
    }

    ScanResult scan_directories(const std::vector<fs::path>& roots,
        const ScanOptions& options) {
        ScanResult result; //All three fields (files, errors, visited) are empty
        //If a directory cannot be opened then skip, don't crash
        auto iter_opts = fs::directory_options::skip_permission_denied;
        //if enabled then follow the links and scan the directories
        if (options.follow_symlinks) {
            iter_opts |= fs::directory_options::follow_directory_symlink;
        }
        for (const auto& root: roots) {//Iterate through allthe given roots
            std:: error_code ec;
            if (!fs::is_directory(root, ec)) {
                result.errors.push_back({root, "not a readable directory"});
                continue;
            }
            //Recursively iterate from each root.
            fs::recursive_directory_iterator it(root, iter_opts, ec);
            const fs::recursive_directory_iterator end;
            while (it != end) {
                const fs:: directory_entry& entry = *it;
                ++result.entries_visited;

                std:: error_code entry_ec;
                const bool is_regular = entry.is_regular_file(entry_ec); //Not a directory. Just a file
                //Handeling files
                if (!entry_ec && is_regular) {
                    const auto size = entry.file_size(entry_ec);
                    if (!entry_ec && size >= options.min_file_size) {
                        bool is_image = false;
                        ImageFormat format{};
                        std::string sniff_err;

                        //If reading failed
                        if (!sniff_format(entry.path(), is_image, format, sniff_err)) {
                            result.errors.push_back({entry.path(), sniff_err});
                        }
                        else if (is_image) {
                            FileEntry fe;
                            fe.path = entry.path();
                            fe.size_bytes = size;
                            fe.mtime = entry.last_write_time(entry_ec);
                            fe.format = format;
                            fe.extension_mismatch = !has_image_extension(entry.path());
                            result.files.push_back(std::move(fe));
                        }else {
                            ++result.skipped_non_image;
                        }
                    }
                }
                //To the next file
                it.increment(ec);
                if (ec) {
                    result.errors.push_back({entry.path(),
                            "traversal error: " + ec.message()});
                    ec.clear();
                }
            }
        }
        return result;
    }

    bool detect_image_format(const fs::path& p, ImageFormat& out, std::string& error) {
        bool is_image = false;
        if (!sniff_format(p, is_image, out, error)) return false;
        if (!is_image) {
            error = "not a JPEG or PNG";
            return false;
        }
        return true;
    }

}
