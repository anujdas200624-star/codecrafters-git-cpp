#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <zstr.hpp>
#include <openssl/sha.h>
#include <zlib.h>

std::string char_hex(unsigned char data)
{
    const std::string hex = "0123456789abcdef";
    return std::string{
        hex[data >> 4],
        hex[data & 0x0f]
    };
}

std::string bytes_to_hex(const unsigned char* data, size_t size)
{
    std::string result;
    for (size_t i = 0; i < size; i++)
    {
        result += char_hex(data[i]);
    }
    return result;
}

// =============================================================
// LS-TREE
// =============================================================
struct TreeEntry {
    std::string mode;
    std::string type;
    std::string hash;
    std::string name;
};

void ls_tree(const std::string& hash, bool name_only)
{
    if (hash.size() != 40)
    {
        std::cerr << "Invalid tree hash.\n";
        return;
    }
    
    std::string file_location =
        std::string(".git/objects/") +
        hash.substr(0, 2) +
        "/" +
        hash.substr(2);

    zstr::ifstream file(file_location);

    if (!file.is_open())
    {
        std::cerr << "Could not open tree object.\n";
        return;
    }

    std::string object(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    size_t null_pos = object.find('\0');

    if (null_pos == std::string::npos)
    {
        std::cerr << "Invalid tree object.\n";
        return;
    }

    size_t pos = null_pos + 1;
    std::vector<TreeEntry> entries;

    while (pos < object.size())
    {
        // Find mode
        size_t mode_end = object.find(' ', pos);
        if (mode_end == std::string::npos) break;
        std::string mode = object.substr(pos, mode_end - pos);
        pos = mode_end + 1;

        // Find filename
        size_t name_end = object.find('\0', pos);
        if (name_end == std::string::npos) break;
        std::string name = object.substr(pos, name_end - pos);
        pos = name_end + 1;

        // Read 20-byte SHA-1
        if (pos + 20 > object.size()) break;
        const unsigned char* sha = reinterpret_cast<const unsigned char*>(object.data() + pos);
        std::string object_hash = bytes_to_hex(sha, 20);
        pos += 20;
        
        std::string type = (mode == "40000" || mode == "040000") ? "tree" : "blob";
        entries.push_back({mode, type, object_hash, name});
    }

    // Git strictly sorts tree entries alphabetically by name
    std::sort(entries.begin(), entries.end(), [](const TreeEntry& a, const TreeEntry& b) {
        return a.name < b.name;
    });

    for (const auto& entry : entries)
    {
        if (name_only)
        {
            std::cout << entry.name << '\n';
        }
        else
        {
            // Git normally pads the mode to 6 digits (e.g., 40000 -> 040000)
            std::string padded_mode = entry.mode;
            while (padded_mode.size() < 6) padded_mode = "0" + padded_mode;
            
            std::cout << padded_mode << " " << entry.type << " " << entry.hash << "\t" << entry.name << '\n';
        }
    }
}

int main(int argc, char* argv[])
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

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
                std::cerr << "Failed to create .git/HEAD file.\n";
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
            std::cerr << "Usage: cat-file -p <hash>\n";
            return EXIT_FAILURE;
        }

        std::string file_name = argv[3];
        if (file_name.size() != 40)
        {
            std::cerr << "Invalid object hash.\n";
            return EXIT_FAILURE;
        }