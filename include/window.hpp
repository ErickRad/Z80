#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <QMainWindow>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTableWidget>
#include <QTimer>


#include "cpu.hpp"
#include "assembler.hpp"
#include "linker.hpp"
#include "macro.hpp"
#include "objfmt.hpp"

#include <memory>
#include <fstream>
#include <sstream>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private Q_SLOTS:
    void onOpenFile();
    void onSaveFile();

    void onRunMacro();
    void onAssemble();
    void onLink();
    void onLoad();

    void onRun();
    void onStep();
    void onStop();
    void onReset();

    void onTimerTick();
    void onLinkModeChanged(int index);

private:
    void buildUi();
    void buildMenus();

    QString tempPath(const QString &suffix) const;

    void appendLog(const QString &text);
    void appendOutput(const QString &text);

    bool runPipelineUpTo(const QString &stage);

    void refreshRegisters();
    void refreshFlags();
    void refreshMemoryView();
    void refreshStack();

private:
    std::unique_ptr<Z80CPU> cpu_;

    bool hasExe_;
    uint16_t loadOrigin_;

    QString currentFilePath_;
    QString objPath_;
    QString exePath_;

    QTimer *runTimer_;

    QLabel *statusLabel_;

    QPlainTextEdit *sourceEdit_;
    QPlainTextEdit *expandedEdit_;

    QPlainTextEdit *logEdit_;
    QTextEdit *consoleOutput_;

    QTabWidget *tabs_;

    QTableWidget *symbolsTable_;
    QTableWidget *regTable_;
    QTableWidget *flagsTable_;
    QTableWidget *stackTable_;
    QTableWidget *memTable_;

    QSpinBox *memAddrSpin_;
    QComboBox *linkModeCombo_;
};

#endif