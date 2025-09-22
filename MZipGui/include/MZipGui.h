#pragma once
#include <QMainWindow>
#include <QTreeView>
#include <QLineEdit>
#include <QAction>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include "MZip.h"
#include <QAbstractItemModel>

// Custom model for ZipTrie
class ZipTrieModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit ZipTrieModel(const ZipTrie* trie, QObject* parent = nullptr);
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    // For updating the model when the trie changes
    void setTrie(const ZipTrie* trie);
    // Helper for mapping QModelIndex to ZipTrieNode
    const ZipTrieNode* nodeFromIndex(const QModelIndex& index) const;
private:
    const ZipTrie* m_trie;
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
    ZipTrieModel* model;
    std::unique_ptr<MZip> currentArchive;
    std::unique_ptr<ZipTrie> filteredTrie; // for search filtering
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
    // Added helper for trie access
    const ZipTrieNode* nodeFromIndex(const QModelIndex& index) const;

private slots:
    void openArchive();
    void extractSelected();
    void extractAll();
    void handleDoubleClick(const QModelIndex& index);
    void showContextMenu(const QPoint& pos);

    std::string getFullPath(const QModelIndex& index) const;
};