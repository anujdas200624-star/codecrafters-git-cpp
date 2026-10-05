#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <cstring>
#include <vector>
#include <cstdint>

#include <zstr.hpp>
#include <openssl/sha.h>
#include <zlib.h>

std::string char_hex(unsigned char data)
{
const std::string hex = "0123456789abcdef";

```
return std::string{
    hex[data >> 4],
    hex[data & 0x0f]
};
```

}

std::string bytes_to_hex(
const unsigned char* data,
size_t size
)
{
std::string result;

```
for (size_t i = 0; i < size; i++)
{
    result += char_hex(data[i]);
}

return result;
```

}

// =============================================================
// LS-TREE
// =============================================================

void ls_tree(
const std::string& hash,
bool name_only
)
{
if (hash.size() != 40)
{
std::cerr << "Invalid tree hash.\n";
return;
}

```
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

while (pos < object.size())
{
    // -----------------------------------------------------
    // Find mode
    // -----------------------------------------------------

    size_t mode_end = object.find(' ', pos);

    if (mode_end == std::string::npos)
    {
        break;
    }

    std::string mode =
        object.substr(
            pos,
            mode_end - pos
        );

    pos = mode_end + 1;


    // -----------------------------------------------------
    // Find filename
    // -----------------------------------------------------

    size_t name_end = object.find('\0', pos);

    if (name_end == std::string::npos)
    {
        break;
    }

    std::string name =
        object.substr(
            pos,
            name_end - pos
        );

    pos = name_end + 1;


    // -----------------------------------------------------
    // Read 20-byte SHA-1
    // -----------------------------------------------------

    if (pos + 20 > object.size())
    {
        break;
    }

    const unsigned char* sha =
        reinterpret_cast<const unsigned char*>(
            object.data() + pos
        );

    std::string object_hash =
        bytes_to_hex(sha, 20);

    pos += 20;


    // -----------------------------------------------------
    // Output
    // -----------------------------------------------------

    if (name_only)
    {
        std::cout << name << '\n';
    }
    else
    {
        std::string type;

        if (mode == "40000")
        {
            type = "tree";
        }
        else
        {
            type = "blob";
        }

        std::cout
            << mode
            << " "
            << type
            << " "
            << object_hash
            << "\t"
            << name
            << '\n';
    }
}
```

}

int main(int argc, char* argv[])
{
std::cout << std::unitbuf;
std::cerr << std::unitbuf;

```
std::cerr
    << "Logs from your program will appear here!\n";


if (argc < 2)
{
    std::cerr
        << "No command provided.\n";

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
        std::filesystem::create_directories(
            ".git/objects"
        );

        std::filesystem::create_directories(
            ".git/refs"
        );

        std::ofstream headFile(
            ".git/HEAD"
        );

        if (!headFile.is_open())
        {
            std::cerr
                << "Failed to create .git/HEAD file.\n";

            return EXIT_FAILURE;
        }

        headFile
            << "ref: refs/heads/main\n";

        headFile.close();

        std::cout
            << "Initialized git directory\n";
    }
    catch (
        const std::filesystem::filesystem_error& e
    )
    {
        std::cerr
            << e.what()
            << '\n';

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

    std::string file_name =
        argv[3];

    if (file_name.size() != 40)
    {
        std::cerr
            << "Invalid object hash.\n";

        return EXIT_FAILURE;
    }

    std::string file_location =
        std::string(".git/objects/") +
        file_name.substr(0, 2) +
        "/" +
        file_name.substr(2);

    zstr::ifstream file(
        file_location
    );

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

    size_t null_pos =
        contents.find('\0');

    if (null_pos == std::string::npos)
    {
        std::cerr
            << "Invalid Git object.\n";

        return EXIT_FAILURE;
    }

    std::cout <<
        std::string_view(
            contents.data() + null_pos + 1,
            contents.size() - null_pos - 1
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

    std::string file_name =
        argv[3];

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
    // Git blob format
    //
    // blob <size>\0<contents>
    // -----------------------------------------------------

    std::string header =
        "blob " +
        std::to_string(contents.size()) +
        '\0';

    std::string store =
        header + contents;


    // -----------------------------------------------------
    // SHA-1
    // -----------------------------------------------------

    unsigned char output_buffer[
        SHA_DIGEST_LENGTH
    ];

    SHA1(
        reinterpret_cast<const unsigned char*>(
            store.data()
        ),
        store.size(),
        output_buffer
    );

    std::string hash =
        bytes_to_hex(
            output_buffer,
            SHA_DIGEST_LENGTH
        );


    // -----------------------------------------------------
    // Print hash
    // -----------------------------------------------------

    std::cout
        << hash
        << '\n';


    // -----------------------------------------------------
    // Object path
    // -----------------------------------------------------

    std::string dir_name =
        hash.substr(0, 2);

    std::string hash_file_name =
        hash.substr(2);

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
        static_cast<uLong>(
            store.size()
        );

    uLong compressed_size =
        compressBound(source_size);

    std::string compressed_data(
        compressed_size,
        '\0'
    );

    int result =
        compress2(
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
    // Write object
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

    std::string hash =
        argv[argc - 1];
```
