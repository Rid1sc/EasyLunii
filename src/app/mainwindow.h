#pragma once

#include <QMainWindow>

class JobQueue;
class Library;
class LibraryPage;
class EditorPage;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QTabWidget;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    LibraryPage *libraryPage() const { return m_libPage; }

public slots:
    void log(int level, const QString &message);
    void openInEditor(const QString &path);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenus();
    void about();

    Library *m_lib;
    JobQueue *m_jobs;
    QTabWidget *m_tabs;
    LibraryPage *m_libPage;
    EditorPage *m_editor;
    QPlainTextEdit *m_log;
    QLabel *m_jobLabel;
    QProgressBar *m_jobProgress;
    QToolButton *m_cancel;
};
