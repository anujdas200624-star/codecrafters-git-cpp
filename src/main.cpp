
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <cstring>

#include "list_tree.h"

#include <zstr.hpp>
#include <openssl/sha.h>
#include <zlib.h>


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
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    std::cerr << "Logs from your program will appear here!\n";


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
            std::filesystem::create_directories(".git/objects");
            std::filesystem::create_directories(".git/refs");

            std::ofstream headFile(".git/HEAD");

            if (!headFile.is_open())
            {
                std::cerr
                    << "Failed to create .git/HEAD file.\n";

                return EXIT_FAILURE;
            }

            headFile << "ref: refs/heads/main\n";
            headFile.close();

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

        std::string file_location =
            std::string(".git/objects/") +
            file_name.substr(0, 2) +
            "/" +
            file_name.substr(2);

        zstr::ifstream file(file_location);

        if (!file.is_open())
        {
            std::cerr
                << "Could not open object file.\n";

            return EXIT_FAILURE;
        }

        std::string contents(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        size_t header_size =
            contents.find('\0') + 1;

        if (header_size == 0)
        {
            std::cerr
                << "Invalid Git object.\n";

            return EXIT_FAILURE;
        }

        std::cout <<
            std::string_view(
                contents.data() + header_size,
                contents.size() - header_size
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

        std::string file_name = argv[3];

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


        // Git blob:
        //
        // blob <size>\0<contents>

        std::string header =
            "blob " +
            std::to_string(contents.size()) +
            '\0';

        std::string store =
            header + contents;


        // -----------------------------------------------------
        // SHA-1
        // -----------------------------------------------------

        unsigned char output_buffer[SHA_DIGEST_LENGTH];

        SHA1(
            reinterpret_cast<const unsigned char*>(
                store.data()
            ),
            store.size(),
            output_buffer
        );


        // Print SHA-1

        for (size_t i = 0;
             i < SHA_DIGEST_LENGTH;
             i++)
        {
            std::cout <<
                char_hex(output_buffer[i]);
        }

        std::cout << '\n';


        // -----------------------------------------------------
        // Git object path
        // -----------------------------------------------------

        std::string dir_name =
            char_hex(output_buffer[0]);

        std::string hash_file_name;

        for (size_t i = 1;
             i < SHA_DIGEST_LENGTH;
             i++)
        {
            hash_file_name +=
                char_hex(output_buffer[i]);
        }


        std::filesystem::create_directories(
            ".git/objects/" + dir_name
        );


        std::string hash_file_path =
            ".git/objects/" +
            dir_name +
            "/" +
            hash_file_name;


        // -----------------------------------------------------
        // Zlib compression
        // -----------------------------------------------------

        uLong source_size =
            static_cast<uLong>(store.size());

        uLong compressed_size =
            compressBound(source_size);

        std::string compressed_data(
            compressed_size,
            '\0'
        );


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


    // =========================================================
    // LS-TREE
    // =========================================================

    else if (command == "ls-tree")
    {
        bool name_only = false;

        if (argc == 4)
        {
            if (
                std::strcmp(
                    argv[2],
                    "--name-only"
                ) == 0
            )
            {
                name_only = true;
            }
        }


        std::string_view hash =
            argv[argc - 1];


        list_tree::invoke(
            hash,
            name_only
        );
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

