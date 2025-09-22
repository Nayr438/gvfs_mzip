#pragma once

#include <span>
#include "MZip.h"

class MZipRecovery : public MZip
{

    public:

        MZipRecovery(std::string_view fileName) : MZip(fileName) {}
        bool openArchiveForced();   
        bool findData(std::span<char> inData, zip::CentralDirectoryFileHeader &header, std::string &fileName);


};







        
