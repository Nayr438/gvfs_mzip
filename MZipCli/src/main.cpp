#include <iostream>
#include <string>
#include <filesystem>
#include "MZip.h"

void printUsage(const char* programName) {
    std::cout << "MZip CLI - MRS Archive Tool\n\n";
    std::cout << "Usage: " << programName << " <command> [options] <archive_file>\n\n";
    std::cout << "Commands:\n";
    std::cout << "  -e <archive>                    Extract entire archive to current directory\n";
    std::cout << "  -d <archive> <dir_path>             Extract specific directory from archive\n";
    std::cout << "  -f <archive> <file_path> [dest]    Extract specific file from archive\n";
    std::cout << "  -t <archive>                               Show archive directory tree structure\n";
    std::cout << "  -a [directory] [-ext extension] Extract all MRS files recursively from directory\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " -e data.mrs\n";
    std::cout << "  " << programName << " -d data.mrs textures/\n";
    std::cout << "  " << programName << " -f data.mrs textures/logo.png\n";
    std::cout << "  " << programName << " -t data.mrs\n";
    std::cout << "  " << programName << " -a\n";
    std::cout << "  " << programName << " -a -ext .zip\n";
    std::cout << "  " << programName << " -a /path/to/dir\n";
    std::cout << "  " << programName << " -a /path/to/dir -ext .zip\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    auto path = std::filesystem::current_path();


    std::string command = argv[1];
    std::string archivePath = (argc >= 3) ? argv[2] : "";
    
    if (command == "-a") {
        std::string searchPath = ".";
        std::string ext = ".mrs";
        
        for (int i = 2; i < argc; i++) {
            if (std::string(argv[i]) == "-ext" && i + 1 < argc) {
                ext = argv[++i];
            } else {
                searchPath = argv[i];
            }
        }
        if (ext[0] != '.') ext = "." + ext;

        if(!std::filesystem::is_directory(searchPath) || !std::filesystem::exists(searchPath)) {
            std::cerr << "Error: Directory '" << searchPath << "' does not exist" << std::endl;
            return 1;
        }
        
        for (const auto& entry : std::filesystem::recursive_directory_iterator(searchPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ext) {
                MZip archive(entry.path().string());
                if (archive.openArchive()) {
                    archive.extractArchive((path / entry.path().stem()).string());
                }
            }
        }
        return 0;
    }
    
    // Handle other commands that require a single archive
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }
    

    MZip archive(archivePath);
    if (!archive.openArchive()) {
        std::cerr << "Error: Failed to open archive '" << archivePath << "'" << std::endl;
        return 1;
    }
    
    if (command == "-e") {
        auto outPath = path / std::filesystem::path(archivePath).stem().string();
        archive.extractArchive(outPath.string());
        return 0;
    }
    else if (command == "-d") {
        std::string dirPath = argv[3];
        if (dirPath.empty()) {
            std::cerr << "Error: Directory path cannot be empty" << std::endl;
            return 1;
        }
        archive.extractDirectory(dirPath, path);
        return 0;
    }
    else if (command == "-f") {
        std::string filePath = argv[3];
        if (filePath.empty()) {
            std::cerr << "Error: File path cannot be empty" << std::endl;
            return 1;
        }

        std::filesystem::path filename = std::filesystem::path(filePath).filename();
        archive.extractFile(filePath, path/filename);
        return 0;
    }
    else if (command == "-t") {
        archive.getTree()->print();
        return 0;
    }
    else {
        std::cerr << "Error: Unknown command '" << command << "'" << std::endl;
        printUsage(argv[0]);
        return 1;
    }
}
