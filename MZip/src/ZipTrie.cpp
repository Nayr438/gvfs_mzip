#include "ZipTrie.h"
#include <functional>
#include <stack>

NodeFileHeader toNodeFileHeader(const zip::CentralDirectoryFileHeader &hdr)
{
  return NodeFileHeader{hdr.UncompressedSize, hdr.CompressedSize, hdr.FileHeaderOffset, hdr.CRC32, hdr.LastModified};
}

ZipTrie::ZipTrie() : rootNode(std::make_unique<ZipTrieNode>()) {}

ZipTrieNode *ZipTrie::getOrCreate(const std::filesystem::path &path)
{
  ZipTrieNode *node = rootNode.get();
  for (const auto &part : path)
  {
    if (part.empty() || part == ".")
      continue;
    auto &children = node->children;
    auto it = children.find(part.string());
    if (it == children.end())
    {
      it = children.emplace(part.string(), std::make_unique<ZipTrieNode>()).first;
    }
    node = it->second.get();
  }
  return node;
}

bool ZipTrie::insert(const std::filesystem::path &path, std::optional<NodeFileHeader> fileData)
{
  if (path.empty())
    return false;
  auto parent = path.parent_path();
  auto name = path.filename().string();
  ZipTrieNode *parentNode = getOrCreate(parent);
  if (!parentNode)
    return false;
  auto &children = parentNode->children;
  auto it = children.find(name);
  if (it != children.end())
  {
    if (fileData)
    {
      it->second->fileData = std::move(fileData);
    }
    return true;
  }
  if (fileData)
  {
    children[name] = std::make_unique<ZipTrieNode>(std::move(*fileData));
  }
  else
  {
    children[name] = std::make_unique<ZipTrieNode>();
  }
  return true;
}

const ZipTrieNode *ZipTrie::lookup(const std::filesystem::path &path) const
{
  const ZipTrieNode *node = rootNode.get();
  for (const auto &part : path)
  {
    if (part.empty() || part == ".")
      continue;
    auto it = node->children.find(part.string());
    if (it == node->children.end())
      return nullptr;
    node = it->second.get();
  }
  return node;
}

ZipTrieNode *ZipTrie::lookup(const std::filesystem::path &path)
{
  return const_cast<ZipTrieNode *>(static_cast<const ZipTrie *>(this)->lookup(path));
}

bool ZipTrie::remove(const std::filesystem::path &path)
{
  if (path.empty())
    return false;
  auto parent = path.parent_path();
  auto name = path.filename().string();
  ZipTrieNode *parentNode = lookup(parent);
  if (!parentNode)
    return false;
  auto it = parentNode->children.find(name);
  if (it == parentNode->children.end())
    return false;
  parentNode->children.erase(it);
  return true;
}

void ZipTrie::traverse(const std::filesystem::path &path,
                       const std::function<void(const std::filesystem::path &, const ZipTrieNode &)> &func) const
{
  const ZipTrieNode *node = lookup(path);
  if (!node)
    return;
  std::stack<std::pair<std::filesystem::path, const ZipTrieNode *>> stack;
  stack.emplace(path, node);
  while (!stack.empty())
  {
    auto [curPath, curNode] = stack.top();
    stack.pop();
    func(curPath, *curNode);
    for (const auto &[name, child] : curNode->children)
    {
      stack.emplace(curPath / name, child.get());
    }
  }
}