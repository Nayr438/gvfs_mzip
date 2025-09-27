#include "ZipTree.h"
#include <algorithm>
#include <functional>
#include <iostream>

void ZipTree::print(const ZipNode *node, int depth, const std::vector<bool> &isLast) const
{
  if (!node)
    node = root.get();

  // Print tree structure
  for (int i = 0; i < depth; ++i)
  {
    if (i == depth - 1)
    {
      std::printf("%s", (isLast[i] ? "+-- " : "+-- "));
    }
    else
    {
      std::printf("%s", (isLast[i] ? "    " : "|   "));
    }
  }

  // Print node name
  std::printf("%s\n", node->name.c_str());

  // Print children (directories first, then files)
  const auto &children = node->children;
  if (!children.empty())
  {
    // Create sorted vector: directories first, then files, both alphabetically
    std::vector<std::pair<std::string, const ZipNode *>> sortedChildren;
    for (const auto &[name, childPtr] : children)
    {
      sortedChildren.emplace_back(name, childPtr.get());
    }
    std::sort(sortedChildren.begin(), sortedChildren.end(),
              [](const auto &a, const auto &b)
              {
                bool aDir = a.second->isDirectory;
                bool bDir = b.second->isDirectory;
                if (aDir != bDir)
                  return aDir > bDir;     // directories first
                return a.first < b.first; // then alphabetically
              });

    for (size_t i = 0; i < sortedChildren.size(); ++i)
    {
      bool isLastChild = (i == sortedChildren.size() - 1);

      std::vector<bool> newIsLast = isLast;
      newIsLast.push_back(isLastChild);

      print(sortedChildren[i].second, depth + 1, newIsLast);
    }
  }
}

std::vector<std::string> ZipTree::getRecursiveFilePaths(const std::string &path) const
{
  std::vector<std::string> filePaths;

  std::function<void(const std::string &)> collectFiles = [&](const std::string &currentPath)
  {
    const ZipNode *currentNode = lookup(currentPath);
    if (!currentNode)
      return;

    if (!currentNode->isDirectory)
    {
      filePaths.push_back(currentPath);
    }
    else
    {
      auto children = getChildren(currentPath);
      for (const auto &child : children)
      {
        std::string childPath = currentPath.empty() ? child : currentPath + "/" + child;
        collectFiles(childPath);
      }
    }
  };

  collectFiles(path);
  return filePaths;
}
