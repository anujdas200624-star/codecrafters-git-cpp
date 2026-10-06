#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <ctime>
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

std::string hex_to_bytes(const std::string& hex)
{
    std::string bytes;
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        std::string byteString = hex.substr(i, 2);
        unsigned char byte = static_cast<unsigned char>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

// Helper to hash and write any Git object (blob, tree, or commit)
std::string hash_and_write_object(const std::string& type, const std::string& data)
{
    std::string header = type + " " + std::to_string(data.size()) + '\0';
    std::string store = header + data;

    unsigned char output_buffer[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(store.data()), store.size(), output_buffer);
    std::string hash = bytes_to_hex(output_buffer, SHA_DIGEST_LENGTH);

    std::string dir_name = hash.substr(0, 2);
    std::string hash_file_name = hash.substr(2);
    std::filesystem::create_directories(".git/objects/" + dir_name);
    std::string hash_file_path = ".git/objects/" + dir_name + "/" + hash_file_name;

    uLong source_size = static_cast<uLong>(store.size());
    uLong compressed_size = compressBound(source_size);
    std::string compressed_data(compressed_size, '\0');

    int result = compress(
        reinterpret_cast<Bytef*>(compressed_data.data()),
        &compressed_size,
        reinterpret_cast<const Bytef*>(store.data()),
        source_size
    );

    if (result != Z_OK)
    {
        return "";
    }
    
    compressed_data.resize(compressed_size);

    std::ofstream output_file(hash_file_path, std::ios::binary);
    if (!output_file.is_open())
    {
        return "";
    }
    output_file.write(compressed_data.data(), compressed_data.size());
    output_file.close();

    return hash;
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
        size_t mode_end = object.find(' ', pos);
        if (mode_end == std::string::npos) break;
        std::string mode = object.substr(pos, mode_end - pos);
        pos = mode_end + 1;

        size_t name_end = object.find('\0', pos);
        if (name_end == std::string::npos) break;
        std::string name = object.substr(pos, name_end - pos);
        pos = name_end + 1;

        if (pos + 20 > object.size()) break;
        const unsigned char* sha = reinterpret_cast<const unsigned char*>(object.data() + pos);
        std::string object_hash = bytes_to_hex(sha, 20);
        pos += 20;
        
        std::string type = (mode == "40000" || mode == "040000") ? "tree" : "blob";
        entries.push_back({mode, type, object_hash, name});
    }

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
            std::string padded_mode = entry.mode;
            while (padded_mode.size() < 6) padded_mode = "0" + padded_mode;
            
            std::cout << padded_mode << " " << entry.type << " " << entry.hash << "\t" << entry.name << '\n';
        }
    }
}

// =============================================================
// WRITE-TREE
// =============================================================
struct TreeObjectEntry {
    std::string mode;
    std::string name;
    std::string hash_hex;
};

std::string write_tree_recursive(const std::filesystem::path& dir_path)
{
    std::vector<TreeObjectEntry> entries;

    for (const auto& entry : std::filesystem::directory_iterator(dir_path))
    {
        std::string filename = entry.path().filename().string();
        if (filename == ".git")
        {
            continue;
        }

        if (entry.is_directory())
        {
            std::string sub_hash = write_tree_recursive(entry.path());
            if (sub_hash.empty()) return "";
            entries.push_back({"40000", filename, sub_hash});
        }
        else if (entry.is_regular_file())
        {
            std::ifstream file(entry.path(), std::ios::binary);
            if (!file.is_open()) continue;
            std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            std::string blob_hash = hash_and_write_object("blob", contents);
            if (blob_hash.empty()) return "";

            std::string mode = "100644";
            auto permissions = entry.status().permissions();
            if ((permissions & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)
            {
                mode = "100755";
            }

            entries.push_back({mode, filename, blob_hash});
        }
    }

    std::sort(entries.begin(), entries.end(), [](const TreeObjectEntry& a, const TreeObjectEntry& b) {
        return a.name < b.name;
    });

    std::string tree_content;
    for (const auto& entry : entries)
    {
        tree_content += entry.mode + " " + entry.name + '\0' + hex_to_bytes(entry.hash_hex);
    }

    return hash_and_write_object("tree", tree_content);
}

// =============================================================
// COMMIT-TREE
// =============================================================
void commit_tree(int argc, char* argv[])
{
    std::string tree_hash;
    std::string parent_hash;
    std::string message;

    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
            parent_hash = argv[i + 1];
            i++;
        }
        else if (std::strcmp(argv[i], "-m") == 0 && i + 1 < argc)
        {
            message = argv[i + 1];
            i++;
        }
        else if (tree_hash.empty())
        {
            tree_hash = argv[i];
        }
    }

    if (tree_hash.empty() || message.empty())
    {
        std::cerr << "Usage: commit-tree <tree_hash> [-p <parent_hash>] -m <message>\n";
        return;
    }

    std::string name = "Codewith C++";
    std::string email = "codecrafters@example.com";
    
    std::time_t now = std::time(nullptr);
    std::string timestamp = std::to_string(now);
    std::string timezone = " +0000";

    std::string commit_content = "tree " + tree_hash + "\n";
    if (!parent_hash.empty())
    {
        commit_content += "parent " + parent_hash + "\n";
    }

    std::string author_line = "author " + name + " <" + email + "> " + timestamp + timezone;
    std::string committer_line = "committer " + name + " <" + email + "> " + timestamp + timezone;

    commit_content += author_line + "\n";
    commit_content += committer_line + "\n\n";
    commit_content += message + "\n";

    std::string commit_hash = hash_and_write_object("commit", commit_content);
    if (!commit_hash.empty())
    {
        std::cout << commit_hash << '\n';
    }
    else
    {
        std::cerr << "Failed to write commit object.\n";
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

        std::string file_location = std::string(".git/objects/") + file_name.substr(0, 2) + "/" + file_name.substr(2);
        zstr::ifstream file(file_location);

        if (!file.is_open())
        {
            std::cerr << "Could not open object file.\n";
            return EXIT_FAILURE;
        }

        std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        size_t null_pos = contents.find('\0');

        if (null_pos == std::string::npos)
        {
            std::cerr << "Invalid Git object.\n";
            return EXIT_FAILURE;
        }

        std::cout << std::string_view(contents.data() + null_pos + 1, contents.size() - null_pos - 1);
    }

    // =========================================================
    // HASH-OBJECT
    // =========================================================
    else if (command == "hash-object")
    {
        if (argc < 4)
        {
            std::cerr << "Usage: hash-object -w <file>\n";
            return EXIT_FAILURE;
        }

        std::string file_name = argv[3];
        std::ifstream file(file_name, std::ios::binary);

        if (!file.is_open())
        {
            std::cerr << "Could not open file.\n";
            return EXIT_FAILURE;
        }

        std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        std::string hash = hash_and_write_object("blob", contents);
        if (hash.empty())
        {
            std::cerr << "Failed to hash/write object.\n";
            return EXIT_FAILURE;
        }

        std::cout << hash << '\n';
    }

    // =========================================================
    // LS-TREE
    // =========================================================
    else if (command == "ls-tree")
    {
        bool name_only = false;
        if (argc == 4 && std::strcmp(argv[2], "--name-only") == 0)
        {
            name_only = true;
        }

        std::string hash = argv[argc - 1];
        ls_tree(hash, name_only);
    }

    // =========================================================
    // WRITE-TREE
    // =========================================================
    else if (command == "write-tree")
    {
        std::string root_hash = write_tree_recursive(".");
        if (!root_hash.empty())
        {
            std::cout << root_hash << '\n';
        }
        else
        {
            std::cerr << "Failed to write tree object.\n";
            return EXIT_FAILURE;
        }
    }

    // =========================================================
    // COMMIT-TREE
    // =========================================================
    else if (command == "commit-tree")
    {
        commit_tree(argc, argv);
    }

    // =========================================================
    // UNKNOWN COMMAND
    // =========================================================
    else
    {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}