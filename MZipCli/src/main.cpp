#include <iostream>
#include <string>
#include "MZip.h"

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <archive_file>" << std::endl;
        return 1;
    }

    std::string archivePath = argv[1];
    MZip archive(archivePath);
    
    if (!archive.openArchive()) {
        std::cerr << "Failed to open archive: " << archivePath << std::endl;
        return 1;
    }

    std::cout << "Successfully opened archive: " << archivePath << std::endl;
    return 0;
}
