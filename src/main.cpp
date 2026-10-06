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
#include <curl/curl.h>

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

// Helper to safely read a raw decompressed Git object from the filesystem
std::string read_object(const std::string& hash)
{
    if (hash.size() != 40) return "";
    
    std::string file_location = ".git/objects/" + hash.substr(0, 2) + "/" + hash.substr(2);
    
    // Prevent strict_fstream::Exception by checking if the file actually exists
    if (!std::filesystem::exists(file_location)) {
        return "";
    }

    try {
        zstr::ifstream file(file_location, std::ios::binary);
        if (!file.is_open()) return "";
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    } catch (...) {
        return "";
    }
}

// Helper to recursively checkout a tree into the working directory
void checkout_tree_recursive(const std::string& tree_hash, const std::filesystem::path& base_path)
{
    std::string object_data = read_object(tree_hash);
    if (object_data.empty()) return;

    size_t null_pos = object_data.find('\0');
    if (null_pos == std::string::npos) return;

    size_t pos = null_pos + 1;
    while (pos < object_data.size())
    {
        size_t mode_end = object_data.find(' ', pos);
        if (mode_end == std::string::npos) break;
        std::string mode = object_data.substr(pos, mode_end - pos);
        pos = mode_end + 1;

        size_t name_end = object_data.find('\0', pos);
        if (name_end == std::string::npos) break;
        std::string name = object_data.substr(pos, name_end - pos);
        pos = name_end + 1;

        if (pos + 20 > object_data.size()) break;
        const unsigned char* sha = reinterpret_cast<const unsigned char*>(object_data.data() + pos);
        std::string obj_hash = bytes_to_hex(sha, 20);
        pos += 20;

        std::filesystem::path full_path = base_path / name;

        if (mode == "40000" || mode == "040000") // Tree (Directory)
        {
            std::filesystem::create_directories(full_path);
            checkout_tree_recursive(obj_hash, full_path);
        }
        else // Blob (File)
        {
            std::string blob_data = read_object(obj_hash);
            if (blob_data.empty()) continue; // Skip gracefully if blob isn't found
            
            size_t blob_null = blob_data.find('\0');
            if (blob_null != std::string::npos)
            {
                std::ofstream out(full_path, std::ios::binary);
                out.write(blob_data.data() + blob_null + 1, blob_data.size() - blob_null - 1);
                out.close();

                // Set executable permissions if needed
                if (mode == "100755") {
                    std::filesystem::permissions(full_path, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec, std::filesystem::perm_options::add);
                }
            }
        }
    }
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
    
    std::string object = read_object(hash);
    if (object.empty())
    {
        std::cerr << "Could not open tree object.\n";
        return;
    }

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

// =============================================================
// CLONE (Libcurl HTTP callback helper)
// =============================================================
size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void clone_repo(const std::string& repo_url, const std::string& target_dir)
{
    // 1. Create target directory and standard git structure
    std::filesystem::create_directories(target_dir + "/.git/objects");
    std::filesystem::create_directories(target_dir + "/.git/refs");

    std::ofstream head_file(target_dir + "/.git/HEAD");
    if (head_file.is_open())
    {
        head_file << "ref: refs/heads/main\n";
        head_file.close();
    }

    // Change directory so hash_and_write_object writes relative to the target directory
    std::filesystem::current_path(target_dir);

    CURL* curl = curl_easy_init();
    if (!curl) return;

    // 2. Fetch refs via libcurl to find HEAD hash
    std::string info_refs_url = repo_url + "/info/refs?service=git-upload-pack";
    std::string read_buffer;

    curl_easy_setopt(curl, CURLOPT_URL, info_refs_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); 
    curl_easy_perform(curl);

    // Reliable fallback extraction for the HEAD hash
    std::string head_hash;
    size_t head_pos = read_buffer.find(" HEAD");
    if (head_pos != std::string::npos && head_pos >= 40) {
        head_hash = read_buffer.substr(head_pos - 40, 40);
    } else {
        curl_easy_cleanup(curl);
        return;
    }

    // 3. Post to /git-upload-pack to fetch the PACK file
    std::string pack_url = repo_url + "/git-upload-pack";
    std::string req = "0032want " + head_hash + "\n00000009done\n";
    std::string pack_response;

    curl_easy_setopt(curl, CURLOPT_URL, pack_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, req.size());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pack_response);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-git-upload-pack-request");
    headers = curl_slist_append(headers, "Accept: application/x-git-upload-pack-result");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // 4. Parse the PACK file and extract the objects
    size_t pack_idx = pack_response.find("PACK");
    if (pack_idx != std::string::npos) {
        const unsigned char* pack_data = (const unsigned char*)pack_response.data() + pack_idx;
        size_t total_size = pack_response.size() - pack_idx;

        if (total_size >= 12) {
            uint32_t num_objects = (pack_data[8] << 24) | (pack_data[9] << 16) | (pack_data[10] << 8) | pack_data[11];
            size_t offset = 12;

            for (uint32_t i = 0; i < num_objects; ++i) {
                unsigned char c = pack_data[offset++];
                int type = (c >> 4) & 7;
                size_t size = c & 15;
                int shift = 4;
                while (c & 0x80) {
                    c = pack_data[offset++];
                    size |= (size_t(c & 0x7f) << shift);
                    shift += 7;
                }

                if (type >= 1 && type <= 4) {
                    std::string obj_data;
                    obj_data.reserve(size);

                    z_stream strm = {};
                    inflateInit(&strm);
                    strm.next_in = (Bytef*)(pack_data + offset);
                    strm.avail_in = total_size - offset;

                    char out_buf[32768];
                    int ret;
                    do {
                        strm.next_out = (Bytef*)out_buf;
                        strm.avail_out = sizeof(out_buf);
                        ret = inflate(&strm, Z_NO_FLUSH);
                        
                        if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) break;
                        
                        obj_data.append(out_buf, sizeof(out_buf) - strm.avail_out);
                    } while (ret != Z_STREAM_END);

                    offset += strm.total_in;
                    inflateEnd(&strm);

                    std::string obj_type;
                    if (type == 1) obj_type = "commit";
                    else if (type == 2) obj_type = "tree";
                    else if (type == 3) obj_type = "blob";
                    else if (type == 4) obj_type = "tag";

                    hash_and_write_object(obj_type, obj_data);
                } 
                else if (type >= 6) {
                    if (type == 6) {
                        c = pack_data[offset++];
                        while (c & 0x80) c = pack_data[offset++];
                    } else if (type == 7) {
                        offset += 20;
                    }
                    
                    z_stream strm = {};
                    inflateInit(&strm);
                    strm.next_in = (Bytef*)(pack_data + offset);
                    strm.avail_in = total_size - offset;
                    
                    char dummy[32768];
                    int ret;
                    do {
                        strm.next_out = (Bytef*)dummy;
                        strm.avail_out = sizeof(dummy);
                        ret = inflate(&strm, Z_NO_FLUSH);
                    } while (ret != Z_STREAM_END && ret != Z_DATA_ERROR);
                    
                    offset += strm.total_in;
                    inflateEnd(&strm);
                }
            }
        }
    }

    // 5. Check out the root tree to the working directory
    std::string commit_data = read_object(head_hash);
    size_t null_pos = commit_data.find('\0');
    if (null_pos != std::string::npos)
    {
        size_t tree_pos = commit_data.find("tree ", null_pos);
        if (tree_pos != std::string::npos)
        {
            std::string root_tree_hash = commit_data.substr(tree_pos + 5, 40);
            checkout_tree_recursive(root_tree_hash, ".");
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

        std::string contents = read_object(file_name);
        if (contents.empty())
        {
            std::cerr << "Could not open object file.\n";
            return EXIT_FAILURE;
        }

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
    // CLONE
    // =========================================================
    else if (command == "clone")
    {
        if (argc < 4)
        {
            std::cerr << "Usage: clone <repo_url> <target_dir>\n";
            return EXIT_FAILURE;
        }
        clone_repo(argv[2], argv[3]);
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