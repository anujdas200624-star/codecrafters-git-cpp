
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <zstr.hpp>
#include <openssl/sha.h>

std::string char_hex(unsigned char data) {
    const std::string hex = "0123456789abcdef";

    size_t top_part = data >> 4;
    size_t bottom_part = data & 0x0f;

    return std::string{hex[top_part], hex[bottom_part]};
}

int main(int argc, char *argv[])
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // Debug logs
    std::cerr << "Logs from your program will appear here!\n";

    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }

    std::string command = argv[1];

    // ---------------------------------------------------------
    // git init
    // ---------------------------------------------------------
    if (command == "init") {
        try {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");

            std::ofstream headFile(".git/HEAD");

            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                std::cerr << "Failed to create .git/HEAD file.\n";
                return EXIT_FAILURE;
            }

            std::cout << "Initialized git directory\n";
        }
        catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    // ---------------------------------------------------------
    // git cat-file
    // ---------------------------------------------------------
    else if (command == "cat-file") {

        if (argc < 4) {
            std::cerr << "Usage: cat-file -p <hash>\n";
            return EXIT_FAILURE;
        }

        std::string file_name = argv[3];

        std::string file_location =
            std::string(".git/objects/") +
            file_name.substr(0, 2) +
            "/" +
            file_name.substr(2);

        zstr::ifstream file(file_location);

        if (!file.is_open()) {
            std::cerr << "Could not open object file.\n";
            return EXIT_FAILURE;
        }

        std::string contents(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        size_t header_size = contents.find('\0') + 1;

        std::cout << std::string_view(
            contents.begin() + header_size,
            contents.end()
        );
    }

    // ---------------------------------------------------------
    // git hash-object
    // ---------------------------------------------------------
    else if (command == "hash-object") {

        if (argc < 4) {
            std::cerr << "Usage: hash-object -w <file>\n";
            return EXIT_FAILURE;
        }

        std::string file_name = argv[3];

        // Read file contents
        std::ifstream file(file_name, std::ios::binary);

        if (!file.is_open()) {
            std::cerr << "Could not open file.\n";
            return EXIT_FAILURE;
        }

        std::string contents(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        file.close();

        // Git blob header:
        // "blob <size>\0"
        std::string header =
            "blob " + std::to_string(contents.size()) + '\0';

        // Complete Git object
        std::string store = header + contents;

        // -----------------------------------------------------
        // Calculate SHA-1
        // -----------------------------------------------------
        unsigned char output_buffer[SHA_DIGEST_LENGTH];

        unsigned char *input_buffer =
            reinterpret_cast<unsigned char *>(store.data());

        SHA1(
            input_buffer,
            store.size(),
            output_buffer
        );

        // Print SHA-1
        for (size_t i = 0; i < SHA_DIGEST_LENGTH; i++) {
            std::cout << char_hex(output_buffer[i]);
        }

        std::cout << '\n';

        // -----------------------------------------------------
        // Create Git object path
        // -----------------------------------------------------

        // First 2 characters of SHA-1
        std::string dir_name =
            char_hex(output_buffer[0]);

        // Remaining 38 characters
        std::string hash_file_name;

        for (int i = 1; i < SHA_DIGEST_LENGTH; i++) {
            hash_file_name += char_hex(output_buffer[i]);
        }

        // Create:
        // .git/objects/xx
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
        // Compress and write Git object
        // -----------------------------------------------------

        zstr::ofstream output_file(hash_file_path);

        if (!output_file.is_open()) {
            std::cerr << "Could not create Git object file.\n";
            return EXIT_FAILURE;
        }

        output_file << store;

        output_file.close();
    }

    // ---------------------------------------------------------
    // Unknown command
    // ---------------------------------------------------------
    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

