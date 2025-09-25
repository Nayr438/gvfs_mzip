#pragma once
#include "ZipStructs.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ZipNode
{
  std::string name;
  bool isDirectory;
  std::unordered_map<std::string, std::unique_ptr<ZipNode>> children;
  zip::CentralDirectoryFileHeader fileHeader;

  ZipNode(const std::string &n, bool dir) : name(n), isDirectory(dir) {}
};

class ZipTree
{
public:
  ZipTree() : root(std::make_unique<ZipNode>("", true)) {}

  void insert(const std::string &path, const zip::CentralDirectoryFileHeader &header)
  {
    ZipNode *current = root.get();
    std::string part;
    size_t start = 0;

    while (start < path.length())
    {
      size_t end = path.find('/', start);
      if (end == std::string::npos)
        end = path.length();

      part = path.substr(start, end - start);
      if (!part.empty())
      {
        if (!current->children.count(part))
        {
          bool isLast = (end == path.length());
          current->children[part] = std::make_unique<ZipNode>(part, !isLast);
        }
        current = current->children[part].get();
      }
      start = end + 1;
    }

    if (!current->isDirectory)
    {
      current->fileHeader = header;
    }
  }

  const ZipNode *lookup(const std::string &path) const
  {
    const ZipNode *current = root.get();
    std::string part;
    size_t start = 0;

    while (start < path.length())
    {
      size_t end = path.find('/', start);
      if (end == std::string::npos)
        end = path.length();

      part = path.substr(start, end - start);
      if (!part.empty())
      {
        auto it = current->children.find(part);
        if (it == current->children.end())
          return nullptr;
        current = it->second.get();
      }
      start = end + 1;
    }
    return current;
  }

  // Debug printing methods (can be removed in production)
  void printTree() const;
  void print(const ZipNode *node = nullptr, int depth = 0, const std::vector<bool> &isLast = {}) const;

  // Simple helper to get children of a directory
  std::vector<std::string> getChildren(const std::string &path = "") const
  {
    const ZipNode *node = lookup(path);
    if (!node || !node->isDirectory)
      return {};

    std::vector<std::string> children;
    children.reserve(node->children.size());
    for (const auto &[name, child] : node->children)
    {
      children.push_back(name);
    }
    return children;
  }

  // Find a file node (returns nullptr if not found or is directory)
  const ZipNode *findFileNode(const std::string &path) const
  {
    const ZipNode *node = lookup(path);
    return (node && !node->isDirectory) ? node : nullptr;
  }

  // Recursively collect all file paths from directory tree
  std::vector<std::string> getRecursiveFilePaths(const std::string &path = "") const;

  const ZipNode &getRoot() const { return *root; }

private:
  std::unique_ptr<ZipNode> root;
};
