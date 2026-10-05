#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <zstr.hpp>

std::string compree_string(cnst std::string &input){
    std::ostringstream compressed_stream;
    {
        zstr::ostream zs(compree_string,ztrs::default_buff_size,
                            Z_DEFAULT_COMPRESSION,15);
        zs<<input;
    }
    return compressed_stream.str();
}

std::string char_hex(using char data){
    const std:: string hex = "0123456789abcdef";
    size_t top_part = data>>4;
    size_t bottom_part = data & 0x0f;
    return std::string{heax[top_part],hex[bottom_part]};
}
int main(int argc, char *argv[])
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cerr << "Logs from your program will appear here!\n";

    // TODO: Uncomment the code below to pass the first stage
    
    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }
    
    std::string command = argv[1];
    
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
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } 
    else if(command == "cat-file"){
        std:: string file_name = argv[3];
        std::string file_location = std::string(".git/objects/")+std::string(file_name.substr(0,2))+'/'+std::string(file_name.substr(2));
        zstr::ifstream file(file_location);

        std::string contents((std::istreambuf_iterator<char>(file)),(std::istreambuf_iterator<char>()));

        size_t header_size = contents.find('\0')+1;

        std::cout<< std::string_view(contents.begin()+header_size,contents.end());
    }
    else if(command == "hash-object"){
        std::string file_name = srgv[3];

        std::ifstream file(file_name);
        std::string contents((std::istreambuf_iterator<char>(file)),(std::istreambuf_iterator<char>()));

        std::string header = "blob" + std::to_string(content.size()) + '\0';
        std::string store = header + content;

        unsigned char output_buffer[SHA_DIGEST_LENGTH];
        unsigned char *input _buffer =       reinterpret_cast<unsigned char *>(store.data());

    SHA1(input_bufffer, store.size(), output_buffer);

    for (size_t i = 0; i < SHA_DIGEST_LENGTH; i++) {

      std::cout << char_hex(output_buffer[i]);

    }
     std::string dir_name = char_hex(output_buffer[0]);

    std::string hash_file_name = "";

    for (int i = 1; i < SHA_DIGEST_LENGTH; i++) {

      hash_file_name += char_hex(output_buffer[i]);

    }
 std::filesystem::create_directories(".git/objects/" + dir_name);

    std::string hash_file_path =

        ".git/objects/" + dir_name + "/" + hash_file_name;

    // 压缩写入内容

    // zstr::ofstream md_file(hash_file_path);

    // md_file << store;

    // md_file.close();

    // output buf

    uLong output_buffer_size_dest = compressBound(store.size());

    std::string output_buffer_dest(output_buffer_size_dest, '\0');

    // input buf

    const Bytef *input_buffer_store =

        reinterpret_cast<const Bytef *>(store.data());

    uLong input_buffer_size_store = store.size();

    int ret = compress(reinterpret_cast<Bytef *>(output_buffer_dest.data()),

                       &output_buffer_size_dest, input_buffer_store,

                       input_buffer_size_store);

   std::ofstream fout(hash_file_path, std::ios::binary);

    fout.write(output_buffer_dest.c_str(), output_buffer_dest.size());

    fout.close();

  }



    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
