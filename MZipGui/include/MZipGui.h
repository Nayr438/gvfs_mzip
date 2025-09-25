#pragma once
#include <QMainWindow>
#include <QTreeView>
#include <QLineEdit>
#include <QAction>
#include <memory>
#include <string>
#include <filesystem>
#include "MZip.h"
#include <QAbstractItemModel>

// Custom model for ZipTree
class ZipTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit ZipTreeModel(const ZipTree* tree, QObject* parent = nullptr);
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool hasChildren(const QModelIndex& parent) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    // For updating the model when the tree changes
    void setTree(const ZipTree* tree);
    // Helper for mapping QModelIndex to ZipNode
    const ZipNode* nodeFromIndex(const QModelIndex& index) const;
private:
    const ZipTree* m_tree;
    
    // Pre-loaded data structures for performance
    struct NodeData {
        QString name;
        QString size;
        QString compressedSize;
        QString date;
        QString crc;
        bool isDirectory;
        const ZipNode* node;
    };
    
    mutable std::unordered_map<const ZipNode*, std::vector<NodeData>> nodeDataCache;
    mutable std::unordered_map<const ZipNode*, const ZipNode*> parentCache;
    
    void preloadData();
    std::vector<NodeData> getSortedChildrenData(const ZipNode* node) const;
};

class MZipGui : public QMainWindow {
    Q_OBJECT

public:
    explicit MZipGui(QWidget *parent = nullptr);
    void loadArchive(const std::filesystem::path& path);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QTreeView* fileView;
    ZipTreeModel* model;
    std::unique_ptr<MZip> currentArchive;
    std::unique_ptr<ZipTree> filteredTree; // for search filtering
    QLineEdit* searchBox;
    QAction* findAction;

    void setupUi();
    void setupMenus();
    void updateFileList();
    void filterTree(const QString& searchText);
    bool fuzzyMatch(const QString& pattern, const QString& text);
    void setupSearchBox();
    void setupDragDrop();
    QMimeData* createMimeData(const QModelIndexList& indexes) const;
    // Added helper for tree access
    const ZipNode* nodeFromIndex(const QModelIndex& index) const;

private slots:
    void openArchive();
    void extractSelected();
    void extractAll();
    void handleDoubleClick(const QModelIndex& index);
    void showContextMenu(const QPoint& pos);

    std::string getFullPath(const QModelIndex& index) const;
};