#pragma once
#include "ZipStructs.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct NodeFileHeader {
    std::uint32_t UncompressedSize;
    std::uint32_t CompressedSize;
    std::uint32_t FileHeaderOffset;
    std::uint32_t CRC32;
    DOSDateTime LastModified;
};

NodeFileHeader toNodeFileHeader(const zip::CentralDirectoryFileHeader& hdr);

class ZipTrieNode {
public:
    using Ptr = std::unique_ptr<ZipTrieNode>;
    std::unordered_map<std::string, Ptr> children;
    std::optional<NodeFileHeader> fileData;

    ZipTrieNode() = default;
    explicit ZipTrieNode(NodeFileHeader data) : fileData(std::move(data)) {}
    bool isFile() const { return fileData.has_value(); }
};

class ZipTrie {
public:
    ZipTrie();
    bool insert(const std::filesystem::path &path, std::optional<NodeFileHeader> fileData = std::nullopt);
    const ZipTrieNode *lookup(const std::filesystem::path &path) const;
    ZipTrieNode *lookup(const std::filesystem::path &path);
    bool remove(const std::filesystem::path &path);
    void traverse(const std::filesystem::path &path,
                 const std::function<void(const std::filesystem::path &, const ZipTrieNode &)> &func) const;
    const ZipTrieNode &root() const { return *rootNode; }
    ZipTrieNode &root() { return *rootNode; }
private:
    ZipTrieNode::Ptr rootNode;
    ZipTrieNode *getOrCreate(const std::filesystem::path &path);
};