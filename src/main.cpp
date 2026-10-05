
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include "list_tree.h"
#include <zstr.hpp>
#include <openssl/sha.h>
#include <zlib.h>


// Convert a byte to a 2-character hexadecimal string.
// Example: 0xab -> "ab"
std::string char_hex(unsigned char data)
{
    const std::string hex = "0123456789abcdef";

    size_t top_part = data >> 4;
    size_t bottom_part = data & 0x0f;

    return std::string{
        hex[top_part],
        hex[bottom_part]
    };
}


int main(int argc, char *argv[])
{
    // Flush output immediately
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // Debug message
    std::cerr << "Logs from your program will appear here!\n";


    // =========================================================
    // Check command
    // =========================================================

    if (argc < 2)
    {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }

    std::string command = argv[1];


    // =========================================================
    // INIT
    // =========================================================

    if (command == "init")
    {
        try
        {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");

            std::ofstream headFile(".git/HEAD");

            if (headFile.is_open())
            {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            }
            else
            {
                std::cerr
                    << "Failed to create .git/HEAD file.\n";

                return EXIT_FAILURE;
            }

            std::cout << "Initialized git directory\n";
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }


    // =========================================================
    // CAT-FILE
    // =========================================================

    else if (command == "cat-file")
    {
        if (argc < 4)
        {
            std::cerr
                << "Usage: cat-file -p <hash>\n";

            return EXIT_FAILURE;
        }

        std::string file_name = argv[3];

        // Git object path:
        //
        // .git/objects/ab/cdef123...
        //

        std::string file_location =
            std::string(".git/objects/") +
            file_name.substr(0, 2) +
            "/" +
            file_name.substr(2);


        // Open and decompress the object
        zstr::ifstream file(file_location);

        if (!file.is_open())
        {
            std::cerr
                << "Could not open object file.\n";

            return EXIT_FAILURE;
        }


        // Read decompressed object
        std::string contents(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );


        // Git object format:
        //
        // blob <size>\0<contents>
        //

        size_t header_size =
            contents.find('\0') + 1;


        // Print only the actual contents
        std::cout <<
            std::string_view(
                contents.begin() + header_size,
                contents.end()
            );
    }


    // =========================================================
    // HASH-OBJECT
    // =========================================================

    else if (command == "hash-object")
    {
        if (argc < 4)
        {
            std::cerr
                << "Usage: hash-object -w <file>\n";

            return EXIT_FAILURE;
        }


        // Get filename
        std::string file_name = argv[3];


        // -----------------------------------------------------
        // Read file
        // -----------------------------------------------------

        std::ifstream file(
            file_name,
            std::ios::binary
        );

        if (!file.is_open())
        {
            std::cerr
                << "Could not open file.\n";

            return EXIT_FAILURE;
        }


        std::string contents(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        file.close();


        // -----------------------------------------------------
        // Create Git blob header
        // -----------------------------------------------------

        // Git blob header:
        //
        // blob <size>\0
        //

        std::string header =
            "blob " +
            std::to_string(contents.size()) +
            '\0';


        // Complete Git object:
        //
        // header + contents
        //

        std::string store =
            header + contents;


        // -----------------------------------------------------
        // Calculate SHA-1
        // -----------------------------------------------------

        unsigned char output_buffer[SHA_DIGEST_LENGTH];

        SHA1(
            reinterpret_cast<const unsigned char*>(
                store.data()
            ),
            store.size(),
            output_buffer
        );


        // Print 40-character SHA-1
        for (size_t i = 0;
             i < SHA_DIGEST_LENGTH;
             i++)
        {
            std::cout <<
                char_hex(output_buffer[i]);
        }

        std::cout << '\n';


        // -----------------------------------------------------
        // Create Git object path
        // -----------------------------------------------------

        // First 2 hex characters
        // become directory name.

        std::string dir_name =
            char_hex(output_buffer[0]);


        // Remaining 38 characters
        // become filename.

        std::string hash_file_name;

        for (size_t i = 1;
             i < SHA_DIGEST_LENGTH;
             i++)
        {
            hash_file_name +=
                char_hex(output_buffer[i]);
        }


        // Create:
        //
        // .git/objects/xx
        //

        std::filesystem::create_directories(
            ".git/objects/" + dir_name
        );


        // Full object path
        std::string hash_file_path =
            ".git/objects/" +
            dir_name +
            "/" +
            hash_file_name;


        // -----------------------------------------------------
        // Compress using zlib
        // -----------------------------------------------------

        uLong source_size =
            static_cast<uLong>(store.size());

        uLong compressed_size =
            compressBound(source_size);


        // Allocate buffer for compressed data
        std::string compressed_data(
            compressed_size,
            '\0'
        );


        // Compress the Git object
        int result = compress2(
            reinterpret_cast<Bytef*>(
                compressed_data.data()
            ),

            &compressed_size,

            reinterpret_cast<const Bytef*>(
                store.data()
            ),

            source_size,

            Z_DEFAULT_COMPRESSION
        );


        if (result != Z_OK)
        {
            std::cerr
                << "Failed to compress Git object.\n";

            return EXIT_FAILURE;
        }


        // Resize to actual compressed size
        compressed_data.resize(
            compressed_size
        );


        // -----------------------------------------------------
        // Write compressed object
        // -----------------------------------------------------

        std::ofstream output_file(
            hash_file_path,
            std::ios::binary
        );

        if (!output_file.is_open())
        {
            std::cerr
                << "Could not create Git object file.\n";

            return EXIT_FAILURE;
        }


        output_file.write(
            compressed_data.data(),
            compressed_data.size()
        );

        output_file.close();
    }

else if (command == "ls-tree") {
    bool name_only = false;
    if (argc == 4) {
      if (std::strcmp(argv[2], "--name-only") == 0) {
        name_only = true;
      }
    }

    std::string_view hash { argv[argc - 1] };
    list_tree::invoke(hash, name_only);
  }
    // =========================================================
    // UNKNOWN COMMAND
    // =========================================================

    else
    {
        std::cerr
            << "Unknown command "
            << command
            << '\n';

        return EXIT_FAILURE;
    }


    return EXIT_SUCCESS;
}

