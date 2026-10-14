// Onglet Bibliothèque : barre latérale, grille de jaquettes, fiche, panneau Lunii.
#pragma once

#include <QTreeWidget>
#include <QWidget>

class DevicePanel;
class JobQueue;
class Library;
class LibraryDelegate;
class LibraryFilter;
class LibraryModel;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QTextBrowser;
class QToolButton;

// Barre latérale : accepte le dépôt d'histoires sur une collection
class SideBar : public QTreeWidget
{
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;
signals:
    void keysDroppedOnCollection(int collectionId, const QStringList &paths);
    void deviceStoriesDropped(const QStringList &uuids);
protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
};

class LibraryPage : public QWidget
{
    Q_OBJECT
public:
    LibraryPage(Library *lib, JobQueue *jobs, QWidget *parent = nullptr);
    DevicePanel *devicePanel() const { return m_device; }

signals:
    void openInEditor(const QString &path);
    void logMessage(int level, const QString &message);

public slots:
    void addSourceDialog();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void buildSidebar();
    void onSidebarClicked(QTreeWidgetItem *it);
    void sidebarMenu(const QPoint &pos);
    void gridMenu(const QPoint &pos);
    void updateDetails();
    void updateCounts();
    QStringList selectedPaths() const;
    QStringList selectedKeys() const;
    void exportDeviceStoriesToLibrary(const QStringList &uuids);

    Library *m_lib;
    JobQueue *m_jobs;
    LibraryModel *m_model;
    LibraryFilter *m_filter;
    LibraryDelegate *m_delegate;
    SideBar *m_side;
    QListView *m_view;
    QLineEdit *m_search;
    QComboBox *m_age, *m_sort;
    QToolButton *m_gridBtn, *m_listBtn;
    QLabel *m_status;
    // fiche
    QWidget *m_details;
    QLabel *m_cover, *m_title, *m_meta;
    QTextBrowser *m_desc;
    QLineEdit *m_tags;
    QPushButton *m_sendBtn, *m_editBtn, *m_folderBtn;
    DevicePanel *m_device;
};
