#include "MZFile.h"


std::uint32_t MZFile::getSignature() noexcept
{
    std::uint32_t signature = 0;
    seek(0);
    read(&signature);
    return signature;
}