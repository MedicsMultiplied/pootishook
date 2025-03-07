#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtCore/QProcess>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QLabel>
#include <QtCore/QTimer>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QInputDialog>
#include <QtCore/QTextStream>
#include <QtCore/QSettings>

class RainbowLabel : public QLabel
{
    Q_OBJECT
public:
    explicit RainbowLabel(const QString &text, QWidget *parent = nullptr);

private slots:
    void updateColor();

private:
    QTimer *colorTimer;
    float hue;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onAttachClicked();
    void onUpdateClicked();
    void onInstallAllClicked();
    void onInstallDataClicked();
    void onBuildClicked();
    void onStopClicked();
    void onProcessOutput();
    void onProcessError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onThemeToggled();

private:
    void executeScript(const QString &script);
    void appendToConsole(const QString &text, bool isError = false);
    void installDependencies();
    QString detectDistro();
    void updateStopButtonState();
    QString getSudoPassword();
    void applyTheme(bool isDark);
    void loadSettings();
    void saveSettings();
    
    RainbowLabel *titleLabel;
    QPushButton *attachButton;
    QPushButton *updateButton;
    QPushButton *installAllButton;
    QPushButton *installDataButton;
    QPushButton *buildButton;
    QPushButton *stopButton;
    QPushButton *themeButton;
    QWidget *centralWidget;
    QHBoxLayout *mainLayout;
    QVBoxLayout *leftLayout;
    QTextEdit *consoleOutput;
    QProcess *currentProcess;
    bool isDarkTheme;
    QSettings settings;
};

#endif // MAINWINDOW_H 