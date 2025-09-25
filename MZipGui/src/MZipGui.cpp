#include "MZipGui.h"
#include "MZipRecovery.h"
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
#include <unordered_map>

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

static std::unique_ptr<ZipTree> createFilteredTree(const ZipTree &original, const QString &searchText)
{
  auto filtered = std::make_unique<ZipTree>();
  auto filePaths = original.getRecursiveFilePaths("");

  for (const auto &path : filePaths)
  {
    bool match = searchText.isEmpty() || QString::fromStdString(path).contains(searchText, Qt::CaseInsensitive);
    if (match)
    {
      const ZipNode *node = original.lookup(path);
      if (node && !node->isDirectory)
      {
        filtered->insert(path, node->fileHeader);
      }
    }
  }
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
  model = new ZipTreeModel(nullptr, this);
  fileView->setModel(model);
  fileView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  fileView->setSortingEnabled(true);
  fileView->setEditTriggers(QAbstractItemView::NoEditTriggers);

  // Qt-specific performance optimizations
  fileView->setUniformRowHeights(true);    // Enable uniform row heights for better performance
  fileView->setRootIsDecorated(true);      // Show expand/collapse indicators
  fileView->setAnimated(false);            // Disable animations for better performance
  fileView->setAlternatingRowColors(true); // Visual separation without performance cost

  // Enable lazy loading - only load visible items
  fileView->setItemsExpandable(true);
  fileView->setExpandsOnDoubleClick(true);

  // Enable Qt's built-in optimizations
  fileView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  fileView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  fileView->setAutoScroll(false); // Disable auto-scroll for better performance

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
  if (!currentArchive->openArchive())
  {
    currentArchive = std::make_unique<MZipRecovery>(path.string());
    currentArchive->openArchiveForced();
  }
  updateFileList();
}

const ZipNode *MZipGui::nodeFromIndex(const QModelIndex &index) const
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
  model->setTree(currentArchive->getTree().get());
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
  std::vector<std::string> filesToExtract;
  std::vector<std::string> dirsToExtract;

  for (const auto &index : selected)
  {
    const ZipNode *node = nodeFromIndex(index);
    if (!node)
      continue;
    std::string path = getFullPath(index);
    if (node->isDirectory)
      dirsToExtract.emplace_back(path);
    else
      filesToExtract.emplace_back(path);
  }

  if (!filesToExtract.empty())
  {
    currentArchive->extractFiles(filesToExtract, dir.toStdString());
  }

  for (const auto &dirPath : dirsToExtract)
  {
    currentArchive->extractDirectory(dirPath, dir.toStdString());
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
  const ZipNode *node = nodeFromIndex(index);
  if (!node || node->isDirectory)
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
    const ZipNode *node = nodeFromIndex(selected.first());
    if (node)
    {
      if (!node->isDirectory)
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
    filteredTree.reset();
    model->setTree(currentArchive->getTree().get());
  }
  else
  {
    filteredTree = createFilteredTree(*currentArchive->getTree(), searchText);
    model->setTree(filteredTree.get());
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
      const ZipNode *node = nodeFromIndex(index);
      if (!node)
        continue;
      QString path = QString::fromStdString(getFullPath(index));
      try
      {
        if (node->isDirectory)
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

// --- ZipTreeModel implementation ---
ZipTreeModel::ZipTreeModel(const ZipTree *tree, QObject *parent) : QAbstractItemModel(parent), m_tree(tree) {}

void ZipTreeModel::setTree(const ZipTree *tree)
{
  beginResetModel();
  m_tree = tree;
  nodeDataCache.clear();
  parentCache.clear();
  if (m_tree)
    preloadData();
  endResetModel();
}

QModelIndex ZipTreeModel::index(int row, int column, const QModelIndex &parent) const
{
  if (!m_tree || row < 0 || column < 0 || column >= columnCount(parent))
    return QModelIndex();
  const ZipNode *parentNode = nodeFromIndex(parent);
  if (!parentNode)
    parentNode = &m_tree->getRoot();

  auto childrenData = getSortedChildrenData(parentNode);
  if (row >= static_cast<int>(childrenData.size()))
    return QModelIndex();
  return createIndex(row, column, (void *)childrenData[row].node);
}

QModelIndex ZipTreeModel::parent(const QModelIndex &child) const
{
  if (!child.isValid() || !m_tree)
    return QModelIndex();
  const ZipNode *node = nodeFromIndex(child);
  if (!node)
    return QModelIndex();

  auto it = parentCache.find(node);
  if (it == parentCache.end())
    return QModelIndex();

  const ZipNode *parentNode = it->second;
  if (!parentNode || parentNode == node)
    return QModelIndex();

  // If parent is root, return invalid index
  if (parentNode == &m_tree->getRoot())
    return QModelIndex();

  // Find parent's parent and row
  auto grandParentIt = parentCache.find(parentNode);
  if (grandParentIt == parentCache.end())
    return QModelIndex();

  const ZipNode *grandParent = grandParentIt->second;
  auto childrenData = getSortedChildrenData(grandParent);
  for (int i = 0; i < static_cast<int>(childrenData.size()); ++i)
  {
    if (childrenData[i].node == parentNode)
      return createIndex(i, 0, (void *)parentNode);
  }

  return QModelIndex();
}

int ZipTreeModel::rowCount(const QModelIndex &parent) const
{
  if (!m_tree)
    return 0;
  const ZipNode *node = nodeFromIndex(parent);
  if (!node)
    node = &m_tree->getRoot();
  auto childrenData = getSortedChildrenData(node);
  return static_cast<int>(childrenData.size());
}

int ZipTreeModel::columnCount(const QModelIndex &) const
{
  return 5; // Name, Size, Compressed, Modified, CRC
}

bool ZipTreeModel::hasChildren(const QModelIndex &parent) const
{
  if (!m_tree)
    return false;
  const ZipNode *node = nodeFromIndex(parent);
  if (!node)
    node = &m_tree->getRoot();
  return !node->children.empty();
}

Qt::ItemFlags ZipTreeModel::flags(const QModelIndex &index) const
{
  if (!index.isValid())
    return Qt::NoItemFlags;

  Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

  // Add drag support for files and directories
  const ZipNode *node = nodeFromIndex(index);
  if (node)
  {
    flags |= Qt::ItemIsDragEnabled;
  }

  return flags;
}

QVariant ZipTreeModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || !m_tree)
    return QVariant();
  const ZipNode *node = nodeFromIndex(index);
  if (!node)
    return QVariant();

  // Get parent to find the pre-loaded data
  const ZipNode *parentNode = nodeFromIndex(index.parent());
  if (!parentNode)
    parentNode = &m_tree->getRoot();

  auto childrenData = getSortedChildrenData(parentNode);
  int row = index.row();
  if (row >= static_cast<int>(childrenData.size()))
    return QVariant();

  const NodeData &nodeData = childrenData[row];

  if (role == Qt::DisplayRole)
  {
    switch (index.column())
    {
    case 0:
      return nodeData.name;
    case 1:
      return nodeData.size;
    case 2:
      return nodeData.compressedSize;
    case 3:
      return nodeData.date;
    case 4:
      return nodeData.crc;
    }
  }
  else if (role == Qt::DecorationRole && index.column() == 0)
  {
    if (!nodeData.isDirectory)
      return QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    else
      return QApplication::style()->standardIcon(QStyle::SP_DirIcon);
  }
  else if (role == Qt::UserRole)
  {
    return !nodeData.isDirectory ? 1 : 0;
  }
  return QVariant();
}

QVariant ZipTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
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

const ZipNode *ZipTreeModel::nodeFromIndex(const QModelIndex &index) const
{
  if (!index.isValid())
    return nullptr;
  return reinterpret_cast<const ZipNode *>(index.internalPointer());
}

void ZipTreeModel::preloadData()
{
  if (!m_tree)
    return;

  std::function<void(const ZipNode *, const ZipNode *)> preloadNode = [&](const ZipNode *current, const ZipNode *parent)
  {
    if (parent)
      parentCache[current] = parent;

    std::vector<NodeData> childrenData;
    childrenData.reserve(current->children.size());

    for (const auto &[name, childPtr] : current->children)
    {
      NodeData data;
      data.name = QString::fromStdString(name);
      data.isDirectory = childPtr->isDirectory;
      data.node = childPtr.get();

      if (!childPtr->isDirectory)
      {
        data.size = humanReadableSize(childPtr->fileHeader.UncompressedSize);
        data.compressedSize = humanReadableSize(childPtr->fileHeader.CompressedSize);
        data.date = QString::fromStdString(childPtr->fileHeader.LastModified.toString());
        data.crc = QString::number(childPtr->fileHeader.CRC32, 16).toUpper();
      }

      childrenData.push_back(data);
    }

    // Sort: directories first, then alphabetical
    std::sort(childrenData.begin(), childrenData.end(),
              [](const NodeData &a, const NodeData &b)
              {
                if (a.isDirectory != b.isDirectory)
                  return a.isDirectory > b.isDirectory;
                return a.name < b.name;
              });

    nodeDataCache[current] = childrenData;

    // Recursively preload children
    for (const auto &[name, childPtr] : current->children)
    {
      preloadNode(childPtr.get(), current);
    }
  };

  preloadNode(&m_tree->getRoot(), nullptr);
}

std::vector<ZipTreeModel::NodeData> ZipTreeModel::getSortedChildrenData(const ZipNode *node) const
{
  auto it = nodeDataCache.find(node);
  if (it != nodeDataCache.end())
    return it->second;

  // Fallback if not preloaded
  std::vector<NodeData> childrenData;
  childrenData.reserve(node->children.size());

  for (const auto &[name, childPtr] : node->children)
  {
    NodeData data;
    data.name = QString::fromStdString(name);
    data.isDirectory = childPtr->isDirectory;
    data.node = childPtr.get();

    if (!childPtr->isDirectory)
    {
      data.size = humanReadableSize(childPtr->fileHeader.UncompressedSize);
      data.compressedSize = humanReadableSize(childPtr->fileHeader.CompressedSize);
      data.date = QString::fromStdString(childPtr->fileHeader.LastModified.toString());
      data.crc = QString::number(childPtr->fileHeader.CRC32, 16).toUpper();
    }

    childrenData.push_back(data);
  }

  // Sort: directories first, then alphabetical
  std::sort(childrenData.begin(), childrenData.end(),
            [](const NodeData &a, const NodeData &b)
            {
              if (a.isDirectory != b.isDirectory)
                return a.isDirectory > b.isDirectory;
              return a.name < b.name;
            });

  return childrenData;
}

// --- End ZipTreeModel implementation ---