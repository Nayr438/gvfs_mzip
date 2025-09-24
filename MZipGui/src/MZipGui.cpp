#include "MZipGui.h"
#include <QAbstractItemModel>
#include <QApplication>
#include <QDesktopServices>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTemporaryDir>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <set>

// Helper: format file size in human-readable form
static QString humanReadableSize(quint64 size)
{
  const char *suffixes[] = {"B", "KB", "MB", "GB", "TB"};
  double s = static_cast<double>(size);
  int i = 0;
  while (s >= 1024.0 && i < 4)
  {
    s /= 1024.0;
    ++i;
  }
  return QString::number(s, 'f', (i == 0) ? 0 : 2) + " " + suffixes[i];
}

// Expansion state helpers
static std::string indexToPath(const QAbstractItemModel *model, const QModelIndex &index)
{
  std::vector<std::string> parts;
  QModelIndex current = index;
  while (current.isValid())
  {
    parts.push_back(model->data(current, Qt::DisplayRole).toString().toStdString());
    current = current.parent();
  }
  std::string path;
  for (auto it = parts.rbegin(); it != parts.rend(); ++it)
  {
    if (!path.empty())
      path += '/';
    path += *it;
  }
  return path;
}

static void collectExpandedPaths(const QTreeView *view, const QAbstractItemModel *model, std::set<std::string> &out,
                                 const QModelIndex &parent = QModelIndex())
{
  int rowCount = model->rowCount(parent);
  for (int row = 0; row < rowCount; ++row)
  {
    QModelIndex idx = model->index(row, 0, parent);
    if (view->isExpanded(idx))
    {
      out.insert(indexToPath(model, idx));
      collectExpandedPaths(view, model, out, idx);
    }
  }
}

static void expandByPath(QTreeView *view, const QAbstractItemModel *model, const std::set<std::string> &paths,
                         const QModelIndex &parent = QModelIndex(), const std::string &parentPath = "")
{
  int rowCount = model->rowCount(parent);
  for (int row = 0; row < rowCount; ++row)
  {
    QModelIndex idx = model->index(row, 0, parent);
    std::string name = model->data(idx, Qt::DisplayRole).toString().toStdString();
    std::string fullPath = parentPath.empty() ? name : parentPath + "/" + name;
    if (paths.count(fullPath))
    {
      view->expand(idx);
    }
    if (model->data(idx, Qt::UserRole).toInt() == 0)
    {
      expandByPath(view, model, paths, idx, fullPath);
    }
  }
}

static std::set<std::string> lastExpansionState;

// Helper: filter a ZipTrie by search text (case-insensitive, matches any part of path)
static std::unique_ptr<ZipTrie> filterTrie(const ZipTrie &original, const QString &searchText)
{
  auto filtered = std::make_unique<ZipTrie>();
  std::function<bool(const std::filesystem::path &, const ZipTrieNode &)> filterNode;
  filterNode = [&](const std::filesystem::path &path, const ZipTrieNode &node) -> bool
  {
    bool match = searchText.isEmpty() || QString::fromStdString(path.string()).contains(searchText, Qt::CaseInsensitive);
    bool anyChildMatched = false;
    for (const auto &[name, childPtr] : node.children)
    {
      if (filterNode(path / name, *childPtr))
        anyChildMatched = true;
    }
    if ((match && node.isFile()) || anyChildMatched)
    {
      filtered->insert(path, node.isFile() ? node.fileData : std::nullopt);
      return true;
    }
    return false;
  };
  filterNode({}, original.root());
  return filtered;
}

MZipGui::MZipGui(QWidget *parent) : QMainWindow(parent), currentArchive(nullptr) { setupUi(); }

void MZipGui::setupUi()
{
  setWindowTitle("MZip Archive Manager");
  resize(850, 600);

  auto centralWidget = new QWidget(this);
  auto layout = new QVBoxLayout(centralWidget);

  fileView = new QTreeView(this);
  model = new ZipTrieModel(nullptr, this);
  fileView->setModel(model);
  fileView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  fileView->setSortingEnabled(true);
  fileView->setEditTriggers(QAbstractItemView::NoEditTriggers);

  connect(fileView, &QTreeView::doubleClicked, this, &MZipGui::handleDoubleClick);

  fileView->header()->resizeSection(0, 300);
  fileView->header()->resizeSection(1, 120);
  fileView->header()->resizeSection(2, 120);
  fileView->header()->resizeSection(3, 200);
  fileView->header()->resizeSection(4, 100);

  fileView->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(fileView, &QTreeView::customContextMenuRequested, this, &MZipGui::showContextMenu);

  layout->addWidget(fileView);
  setCentralWidget(centralWidget);

  fileView->sortByColumn(0, Qt::AscendingOrder);

  auto toolbar = addToolBar("Main Toolbar");
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

  auto openAction = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "Open Archive", this);
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MZipGui::openArchive);
  toolbar->addAction(openAction);

  toolbar->addSeparator();

  auto extractSelectedAction = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Extract Selected", this);
  connect(extractSelectedAction, &QAction::triggered, this, &MZipGui::extractSelected);
  toolbar->addAction(extractSelectedAction);

  auto extractAllAction = new QAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Extract All", this);
  connect(extractAllAction, &QAction::triggered, this, &MZipGui::extractAll);
  toolbar->addAction(extractAllAction);

  toolbar->addSeparator();

  findAction = new QAction(QIcon::fromTheme("edit-find", style()->standardIcon(QStyle::SP_FileDialogContentsView)),
                           "Find Files", this);
  findAction->setShortcut(QKeySequence::Find);
  toolbar->addAction(findAction);

  setupSearchBox();

  statusBar()->setStyleSheet("QStatusBar { border-top: 1px solid palette(mid); }");
  statusBar()->setSizeGripEnabled(false);

  fileView->setDragEnabled(true);
  fileView->setDragDropMode(QAbstractItemView::DragOnly);
  setupDragDrop();
}

void MZipGui::setupSearchBox()
{
  // Create search box but don't add to layout yet
  searchBox = new QLineEdit(this);
  searchBox->setPlaceholderText("Search files...");
  searchBox->setClearButtonEnabled(true);
  searchBox->setVisible(false); // Hidden by default

  // Add search box above tree view
  qobject_cast<QVBoxLayout *>(centralWidget()->layout())->insertWidget(0, searchBox);

  // Connect find action
  connect(findAction, &QAction::triggered, this,
          [this]()
          {
            searchBox->setVisible(!searchBox->isVisible());
            if (searchBox->isVisible())
            {
              searchBox->setFocus();
              searchBox->selectAll();
            }
            else
            {
              searchBox->clear(); // Clear search when hiding
            }
          });

  // Connect search functionality
  connect(searchBox, &QLineEdit::textChanged, this, &MZipGui::filterTree);

  // Add Escape key handler to hide search
  searchBox->installEventFilter(this);
}

void MZipGui::loadArchive(const std::filesystem::path &path)
{
  currentArchive = std::make_unique<MZip>(path.string());
  if(currentArchive->openArchive() == false)
  {
    currentArchive = std::make_unique<MZipRecovery>(path.string());
    currentArchive->openArchiveForced();
  }
  updateFileList();
}

const ZipTrieNode *MZipGui::nodeFromIndex(const QModelIndex &index) const
{
  if (!currentArchive)
    return nullptr;
  std::string path = getFullPath(index);
  if (path.empty())
    return nullptr;
  return currentArchive->getTree()->lookup(path);
}

void MZipGui::updateFileList()
{
  if (!currentArchive)
    return;
  model->setTrie(currentArchive->getTree().get());
  QFileInfo archiveInfo(QString::fromStdString(currentArchive->getPath().string()));
  setWindowTitle("MZip Archive Manager - " + archiveInfo.fileName());
  statusBar()->showMessage(QString::fromStdString(currentArchive->getPath().string()));
  fileView->sortByColumn(0, Qt::AscendingOrder);
}

void MZipGui::openArchive()
{
  QString path = QFileDialog::getOpenFileName(this, "Open Archive", QString(), "MRS Archives (*.mrs)");
  if (!path.isEmpty())
  {
    loadArchive(path.toStdString());
  }
  else
  {
    statusBar()->clearMessage(); // Clear path when no archive is loaded
  }
}

void MZipGui::extractSelected()
{
  if (!currentArchive)
    return;
  QString dir = QFileDialog::getExistingDirectory(this, "Extract To");
  if (dir.isEmpty())
    return;
  auto selected = fileView->selectionModel()->selectedRows();
  for (const auto &index : selected)
  {
    const ZipTrieNode *node = nodeFromIndex(index);
    if (!node)
      continue;
    std::string path = getFullPath(index);
    if (!node->isFile())
      currentArchive->extractDirectory(path, dir.toStdString());
    else
      currentArchive->extractFile(path, dir.toStdString());
  }
}

std::string MZipGui::getFullPath(const QModelIndex &index) const
{
  std::vector<std::string> parts;
  QModelIndex current = index;
  while (current.isValid())
  {
    parts.push_back(model->data(model->index(current.row(), 0, current.parent()), Qt::DisplayRole).toString().toStdString());
    current = current.parent();
  }
  std::string path;
  for (auto it = parts.rbegin(); it != parts.rend(); ++it)
  {
    if (!path.empty())
      path += '/';
    path += *it;
  }
  return path;
}

void MZipGui::extractAll()
{
  if (!currentArchive)
    return;
  QString dir = QFileDialog::getExistingDirectory(this, "Extract To");
  if (dir.isEmpty())
    return;
  currentArchive->extractArchive(dir.toStdString());
}

void MZipGui::handleDoubleClick(const QModelIndex &index)
{
  if (!currentArchive || !index.isValid())
    return;
  const ZipTrieNode *node = nodeFromIndex(index);
  if (!node || !node->isFile())
    return;
  QString filePath = QString::fromStdString(getFullPath(index));
  QTemporaryDir tempDir;
  if (!tempDir.isValid())
    return;
  QFileInfo fi(filePath);
  QString tempPath = tempDir.filePath(fi.fileName());
  currentArchive->extractFile(filePath.toStdString(), tempPath.toStdString());
  QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
  tempDir.setAutoRemove(false); // Optional: keep temp file for user
}

void MZipGui::showContextMenu(const QPoint &pos)
{
  if (!currentArchive)
    return;
  QMenu contextMenu(this);
  contextMenu.setStyleSheet(R"(
        QMenu {
            border: 1px solid palette(mid);
            border-radius: 5px;
            padding: 5px;
        }
        QMenu::item {
            padding: 8px 25px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: palette(highlight);
            color: palette(highlighted-text);
        }
    )");
  auto selected = fileView->selectionModel()->selectedRows();
  if (!selected.isEmpty())
  {
    const ZipTrieNode *node = nodeFromIndex(selected.first());
    if (node)
    {
      if (node->isFile())
      {
        auto openAction = contextMenu.addAction("Open");
        connect(openAction, &QAction::triggered, [this, index = selected.first()]() { handleDoubleClick(index); });
      }
      auto extractAction = contextMenu.addAction("Extract");
      connect(extractAction, &QAction::triggered, this, &MZipGui::extractSelected);
    }
  }
  contextMenu.exec(fileView->mapToGlobal(pos));
}

void MZipGui::filterTree(const QString &searchText)
{
  if (!currentArchive)
    return;
  bool clearingSearch = searchText.isEmpty();
  if (clearingSearch)
  {
    lastExpansionState.clear();
    collectExpandedPaths(fileView, model, lastExpansionState);
    filteredTrie.reset();
    model->setTrie(currentArchive->getTree().get());
  }
  else
  {
    filteredTrie = filterTrie(*currentArchive->getTree(), searchText);
    model->setTrie(filteredTrie.get());
  }
  fileView->collapseAll();
  fileView->sortByColumn(0, Qt::AscendingOrder);
  if (clearingSearch)
  {
    expandByPath(fileView, model, lastExpansionState);
  }
}

bool MZipGui::fuzzyMatch(const QString &pattern, const QString &text)
{
  int p = 0, t = 0;
  while (p < pattern.length() && t < text.length())
  {
    if (pattern[p] == text[t])
    {
      p++;
    }
    t++;
  }
  return p == pattern.length();
}

bool MZipGui::eventFilter(QObject *obj, QEvent *event)
{
  if (obj == searchBox && event->type() == QEvent::KeyPress)
  {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_Escape)
    {
      searchBox->clear();
      searchBox->setVisible(false);
      return true;
    }
  }
  else if (obj == fileView->viewport())
  {
    if (event->type() == QEvent::MouseMove)
    {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
      if (mouseEvent->buttons() & Qt::LeftButton)
      {
        auto indexes = fileView->selectionModel()->selectedRows();
        if (!indexes.isEmpty())
        {
          QDrag *drag = new QDrag(this);
          drag->setMimeData(createMimeData(indexes));
          if (indexes.size() == 1)
          {
            drag->setPixmap(fileView->model()->data(indexes[0], Qt::DecorationRole).value<QIcon>().pixmap(32, 32));
          }
          else
          {
            drag->setPixmap(style()->standardIcon(QStyle::SP_DirIcon).pixmap(32, 32));
          }
          drag->exec(Qt::CopyAction);
          return true;
        }
      }
    }
  }
  return QMainWindow::eventFilter(obj, event);
}

QMimeData *MZipGui::createMimeData(const QModelIndexList &indexes) const
{
  if (!currentArchive)
    return nullptr;
  QMimeData *mimeData = new QMimeData();
  QList<QUrl> urls;
  QTemporaryDir tempDir;
  if (!tempDir.isValid())
  {
    delete mimeData;
    return nullptr;
  }
  tempDir.setAutoRemove(false);
  try
  {
    for (const auto &index : indexes)
    {
      const ZipTrieNode *node = nodeFromIndex(index);
      if (!node)
        continue;
      QString path = QString::fromStdString(getFullPath(index));
      try
      {
        if (!node->isFile())
        {
          currentArchive->extractDirectory(path.toStdString(), tempDir.path().toStdString());
          QFileInfo dirInfo(path);
          QString dirPath = tempDir.filePath(dirInfo.fileName());
          urls << QUrl::fromLocalFile(dirPath);
        }
        else
        {
          QFileInfo fi(path);
          QString tempPath = tempDir.filePath(fi.fileName());
          currentArchive->extractFile(path.toStdString(), tempPath.toStdString());
          urls << QUrl::fromLocalFile(tempPath);
        }
      }
      catch (const std::exception &)
      {
        continue;
      }
    }
    if (urls.isEmpty())
    {
      delete mimeData;
      return nullptr;
    }
    mimeData->setUrls(urls);
    return mimeData;
  }
  catch (const std::exception &)
  {
    delete mimeData;
    return nullptr;
  }
}

void MZipGui::setupDragDrop()
{
  fileView->setDefaultDropAction(Qt::CopyAction);
  fileView->viewport()->installEventFilter(this);
}

// --- ZipTrieModel implementation ---
ZipTrieModel::ZipTrieModel(const ZipTrie *trie, QObject *parent) : QAbstractItemModel(parent), m_trie(trie) {}

void ZipTrieModel::setTrie(const ZipTrie *trie)
{
  beginResetModel();
  m_trie = trie;
  endResetModel();
}

QModelIndex ZipTrieModel::index(int row, int column, const QModelIndex &parent) const
{
  if (!m_trie || row < 0 || column < 0 || column >= columnCount(parent))
    return QModelIndex();
  const ZipTrieNode *parentNode = nodeFromIndex(parent);
  if (!parentNode)
    parentNode = &m_trie->root();
  // Build a sorted vector: directories first, then files, both alphabetically
  std::vector<std::pair<std::string, const ZipTrieNode *>> sortedChildren;
  for (const auto &[name, childPtr] : parentNode->children)
  {
    sortedChildren.emplace_back(name, childPtr.get());
  }
  std::sort(sortedChildren.begin(), sortedChildren.end(),
            [](const auto &a, const auto &b)
            {
              bool aDir = !a.second->isFile();
              bool bDir = !b.second->isFile();
              if (aDir != bDir)
                return aDir > bDir; // directories first
              return a.first < b.first;
            });
  if (row >= static_cast<int>(sortedChildren.size()))
    return QModelIndex();
  return createIndex(row, column, (void *)sortedChildren[row].second);
}

QModelIndex ZipTrieModel::parent(const QModelIndex &child) const
{
  if (!child.isValid() || !m_trie)
    return QModelIndex();
  const ZipTrieNode *node = nodeFromIndex(child);
  if (!node)
    return QModelIndex();
  // Find parent node and its row
  std::function<const ZipTrieNode *(const ZipTrieNode *, const ZipTrieNode *, int &)> findParent =
      [&](const ZipTrieNode *current, const ZipTrieNode *target, int &row) -> const ZipTrieNode *
  {
    int i = 0;
    for (const auto &[name, childPtr] : current->children)
    {
      if (childPtr.get() == target)
      {
        row = i;
        return current;
      }
      int subrow = 0;
      const ZipTrieNode *found = findParent(childPtr.get(), target, subrow);
      if (found)
        return found;
      ++i;
    }
    return nullptr;
  };
  int row = 0;
  const ZipTrieNode *parentNode = findParent(&m_trie->root(), node, row);
  if (!parentNode || parentNode == node)
    return QModelIndex();
  // Find parent's parent and row
  if (parentNode == &m_trie->root())
    return QModelIndex();
  int prow = 0;
  findParent(&m_trie->root(), parentNode, prow);
  return createIndex(prow, 0, (void *)parentNode);
}

int ZipTrieModel::rowCount(const QModelIndex &parent) const
{
  if (!m_trie)
    return 0;
  const ZipTrieNode *node = nodeFromIndex(parent);
  if (!node)
    node = &m_trie->root();
  return static_cast<int>(node->children.size());
}

int ZipTrieModel::columnCount(const QModelIndex &) const
{
  return 5; // Name, Size, Compressed, Modified, CRC
}

QVariant ZipTrieModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || !m_trie)
    return QVariant();
  const ZipTrieNode *node = nodeFromIndex(index);
  if (!node)
    return QVariant();
  // Find the name for this node, using sorted order
  const ZipTrieNode *parentNode = nodeFromIndex(index.parent());
  if (!parentNode)
    parentNode = &m_trie->root();
  std::vector<std::pair<std::string, const ZipTrieNode *>> sortedChildren;
  for (const auto &[name, childPtr] : parentNode->children)
  {
    sortedChildren.emplace_back(name, childPtr.get());
  }
  std::sort(sortedChildren.begin(), sortedChildren.end(),
            [](const auto &a, const auto &b)
            {
              bool aDir = !a.second->isFile();
              bool bDir = !b.second->isFile();
              if (aDir != bDir)
                return aDir > bDir;
              return a.first < b.first;
            });
  int row = index.row();
  if (row >= static_cast<int>(sortedChildren.size()))
    return QVariant();
  const std::string &name = sortedChildren[row].first;
  const ZipTrieNode *sortedNode = sortedChildren[row].second;
  if (role == Qt::DisplayRole)
  {
    if (index.column() == 0)
    {
      return QString::fromStdString(name);
    }
    else if (index.column() == 1)
    {
      if (sortedNode->isFile())
        return humanReadableSize(sortedNode->fileData->UncompressedSize);
      return QVariant();
    }
    else if (index.column() == 2)
    {
      if (sortedNode->isFile())
        return humanReadableSize(sortedNode->fileData->CompressedSize);
      return QVariant();
    }
    else if (index.column() == 3)
    {
      if (sortedNode->isFile())
        return QString::fromStdString(sortedNode->fileData->LastModified.toString());
      return QVariant();
    }
    else if (index.column() == 4)
    {
      if (sortedNode->isFile())
        return QString::number(sortedNode->fileData->CRC32, 16).toUpper();
      return QVariant();
    }
  }
  else if (role == Qt::DecorationRole && index.column() == 0)
  {
    if (sortedNode->isFile())
      return QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    else
      return QApplication::style()->standardIcon(QStyle::SP_DirIcon);
  }
  else if (role == Qt::UserRole)
  {
    return sortedNode->isFile() ? 1 : 0;
  }
  return QVariant();
}

QVariant ZipTrieModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
  {
    switch (section)
    {
    case 0:
      return "Name";
    case 1:
      return "Size";
    case 2:
      return "Compressed";
    case 3:
      return "Modified";
    case 4:
      return "CRC";
    }
  }
  return QVariant();
}

const ZipTrieNode *ZipTrieModel::nodeFromIndex(const QModelIndex &index) const
{
  if (!index.isValid())
    return nullptr;
  return reinterpret_cast<const ZipTrieNode *>(index.internalPointer());
}
// --- End ZipTrieModel implementation ---