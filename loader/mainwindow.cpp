#include "mainwindow.h"
#include <QApplication>
#include <QDir>
#include <QColor>
#include <QScrollBar>
#include <QFileInfo>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QInputDialog>
#include <QLineEdit>

RainbowLabel::RainbowLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent), hue(0.0f)
{
    setAlignment(Qt::AlignCenter);
    QFont font = this->font();
    font.setPointSize(24);
    font.setBold(true);
    setFont(font);
    
    colorTimer = new QTimer(this);
    connect(colorTimer, &QTimer::timeout, this, &RainbowLabel::updateColor);
    colorTimer->start(50); // Update every 50ms
}

void RainbowLabel::updateColor()
{
    hue += 0.01f;
    if (hue >= 1.0f) hue = 0.0f;
    
    QColor color = QColor::fromHslF(hue, 1.0f, 0.5f);
    setStyleSheet(QString("QLabel { color: %1; }").arg(color.name()));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), currentProcess(nullptr), settings("cathook", "loader")
{
    setWindowTitle("Cathook Loader");
    setFixedSize(800, 500);

    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    mainLayout = new QHBoxLayout(centralWidget);
    leftLayout = new QVBoxLayout();
    mainLayout->addLayout(leftLayout);
    
    leftLayout->setSpacing(10);
    leftLayout->setContentsMargins(20, 20, 20, 20);

    titleLabel = new RainbowLabel("cathook", this);
    leftLayout->addWidget(titleLabel);
    
    leftLayout->addSpacing(20);

    attachButton = new QPushButton("Attach", this);
    updateButton = new QPushButton("Update", this);
    installAllButton = new QPushButton("Install All", this);
    installDataButton = new QPushButton("Install Data", this);
    buildButton = new QPushButton("Build", this);
    themeButton = new QPushButton(this);

    leftLayout->addWidget(attachButton);
    leftLayout->addWidget(updateButton);
    leftLayout->addWidget(installAllButton);
    leftLayout->addWidget(installDataButton);
    leftLayout->addWidget(buildButton);
    leftLayout->addStretch();

    // Console output on the right
    consoleOutput = new QTextEdit(this);
    consoleOutput->setReadOnly(true);
    consoleOutput->setStyleSheet("QTextEdit { background-color: #1c1c1c; color: #ffffff; border: none; font-family: 'Courier New'; }");
    mainLayout->addWidget(consoleOutput);

    // Add theme button and stop button at the bottom
    leftLayout->addWidget(themeButton);
    
    stopButton = new QPushButton("Stop Process", this);
    stopButton->setEnabled(false);
    leftLayout->addWidget(stopButton);

    // Connect all signals
    connect(attachButton, &QPushButton::clicked, this, &MainWindow::onAttachClicked);
    connect(updateButton, &QPushButton::clicked, this, &MainWindow::onUpdateClicked);
    connect(installAllButton, &QPushButton::clicked, this, &MainWindow::onInstallAllClicked);
    connect(installDataButton, &QPushButton::clicked, this, &MainWindow::onInstallDataClicked);
    connect(buildButton, &QPushButton::clicked, this, &MainWindow::onBuildClicked);
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(themeButton, &QPushButton::clicked, this, &MainWindow::onThemeToggled);

    // Load and apply theme settings
    loadSettings();

    // Get the directory where the loader executable is located
    QString loaderPath = QCoreApplication::applicationFilePath();
    QString cathookDir = QFileInfo(loaderPath).dir().absolutePath();
    appendToConsole("Cathook directory: " + cathookDir);
}

MainWindow::~MainWindow()
{
    if (currentProcess) {
        currentProcess->kill();
        delete currentProcess;
    }
}

void MainWindow::appendToConsole(const QString &text, bool isError)
{
    consoleOutput->setTextColor(isError ? Qt::red : (isDarkTheme ? Qt::white : Qt::black));
    consoleOutput->append(text);
    QScrollBar *sb = consoleOutput->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::executeScript(const QString &script)
{
    if (currentProcess) {
        appendToConsole("Another process is already running!", true);
        return;
    }

    currentProcess = new QProcess(this);
    
    // Get the directory where the loader executable is located
    QString loaderPath = QCoreApplication::applicationFilePath();
    QString cathookDir = QFileInfo(loaderPath).dir().absolutePath();
    currentProcess->setWorkingDirectory(cathookDir);
    
    connect(currentProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onProcessOutput);
    connect(currentProcess, &QProcess::readyReadStandardError, this, &MainWindow::onProcessError);
    connect(currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProcessFinished);

    appendToConsole("\nExecuting: " + script);
    appendToConsole("Working directory: " + cathookDir);
    
    // Set up environment for better output handling
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "xterm-256color");
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("FORCE_COLOR", "1");
    currentProcess->setProcessEnvironment(env);
    
    QString command = QString("cd '%1' && source ~/.bashrc && unbuffer ./%2").arg(cathookDir).arg(script);
    QString fullCommand = QString("unbuffer bash -c '%1' 2>&1").arg(command.replace("'", "'\\''"));
    currentProcess->start("bash", QStringList() << "-c" << fullCommand);

    updateStopButtonState();  // Enable stop button
}

void MainWindow::onProcessOutput()
{
    if (currentProcess) {
        QString output = QString::fromUtf8(currentProcess->readAllStandardOutput());
        // Remove ANSI escape sequences
        output.replace(QRegularExpression("\u001b\\[[0-9;]*[A-Za-z]"), "");
        appendToConsole(output);
    }
}

void MainWindow::onProcessError()
{
    if (currentProcess) {
        QString error = QString::fromUtf8(currentProcess->readAllStandardError());
        // Remove ANSI escape sequences
        error.replace(QRegularExpression("\u001b\\[[0-9;]*[A-Za-z]"), "");
        appendToConsole(error, true);
    }
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode == 0) {
        appendToConsole("\nProcess completed successfully!", false);
    } else {
        appendToConsole("\nProcess failed with exit code: " + QString::number(exitCode), true);
    }

    currentProcess->deleteLater();
    currentProcess = nullptr;
    updateStopButtonState();  // Disable stop button
}

QString MainWindow::getSudoPassword()
{
    bool ok;
    QString password = QInputDialog::getText(this, 
        "Sudo Password Required", 
        "Enter your sudo password:",
        QLineEdit::Password,
        QString(),
        &ok,
        Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);

    if (!ok) {
        appendToConsole("\nPassword input cancelled.", true);
        return QString();
    }

    return password;
}

void MainWindow::onAttachClicked()
{
    if (currentProcess) {
        appendToConsole("Another process is already running!", true);
        return;
    }

    QString password = getSudoPassword();
    if (password.isEmpty()) {
        return;
    }

    currentProcess = new QProcess(this);
    
    // Get the directory where the loader executable is located
    QString loaderPath = QCoreApplication::applicationFilePath();
    QString cathookDir = QFileInfo(loaderPath).dir().absolutePath();
    currentProcess->setWorkingDirectory(cathookDir);
    
    connect(currentProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onProcessOutput);
    connect(currentProcess, &QProcess::readyReadStandardError, this, &MainWindow::onProcessError);
    connect(currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProcessFinished);

    appendToConsole("\nExecuting attach with sudo...");
    appendToConsole("Working directory: " + cathookDir);
    
    // Set up environment for better output handling
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "xterm-256color");
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("FORCE_COLOR", "1");
    currentProcess->setProcessEnvironment(env);
    
    // Use printf to properly escape the password and pipe it to sudo
    QString command = QString("cd '%1' && source ~/.bashrc && printf '%2\\n' | sudo -S ./attach").arg(cathookDir).arg(password);
    QString fullCommand = QString("unbuffer bash -c '%1' 2>&1").arg(command.replace("'", "'\\''"));
    currentProcess->start("bash", QStringList() << "-c" << fullCommand);

    updateStopButtonState();  // Enable stop button
}

void MainWindow::onUpdateClicked()
{
    executeScript("./update");
}

void MainWindow::onInstallAllClicked()
{
    executeScript("./install-all");
}

void MainWindow::onInstallDataClicked()
{
    executeScript("./install-data");
}

void MainWindow::onStopClicked()
{
    if (currentProcess) {
        appendToConsole("\nStopping current process...", true);
        currentProcess->kill();
        // Process cleanup will happen in onProcessFinished
    }
}

void MainWindow::updateStopButtonState()
{
    stopButton->setEnabled(currentProcess != nullptr);
    stopButton->setStyleSheet(currentProcess 
        ? "QPushButton { background-color: #cc2222; } QPushButton:hover { background-color: #ee2222; } QPushButton:pressed { background-color: #aa2222; }"
        : "QPushButton { background-color: #444444; }");
}

void MainWindow::installDependencies()
{
    QString password = getSudoPassword();
    if (password.isEmpty()) {
        return;
    }

    QString distro = detectDistro();
    appendToConsole("\nDetected distribution: " + distro);
    
    QStringList commands;
    if (distro.contains("arch", Qt::CaseInsensitive) || distro.contains("manjaro", Qt::CaseInsensitive)) {
        commands << QString("printf '%1\\n' | sudo -S pacman -S --needed --noconfirm cmake make gcc gdb lib32-sdl2 lib32-glew lib32-freetype2 lib32-libcurl-gnutls boost boost-libs").arg(password);
    }
    else if (distro.contains("fedora", Qt::CaseInsensitive)) {
        commands << QString("printf '%1\\n' | sudo -S dnf install -y cmake make gcc-c++ gdb SDL2-devel.i686 glew-devel.i686 freetype-devel.i686 libcurl-devel.i686 boost-devel.i686").arg(password);
    }
    else if (distro.contains("ubuntu", Qt::CaseInsensitive) || distro.contains("debian", Qt::CaseInsensitive)) {
        commands << QString("printf '%1\\n' | sudo -S apt-get update").arg(password)
                << QString("printf '%1\\n' | sudo -S apt-get install -y build-essential cmake gcc-multilib g++-multilib gdb libsdl2-dev:i386 libfreetype6-dev:i386 libcurl4-gnutls-dev:i386 libboost-dev libboost-filesystem-dev libboost-system-dev libboost-program-options-dev").arg(password);
    }
    
    if (commands.isEmpty()) {
        appendToConsole("Unsupported distribution. Please install dependencies manually.", true);
        return;
    }

    QString script = commands.join(" && ");
    
    if (currentProcess) {
        appendToConsole("Another process is already running!", true);
        return;
    }

    currentProcess = new QProcess(this);
    currentProcess->setWorkingDirectory(QDir::homePath());
    
    connect(currentProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onProcessOutput);
    connect(currentProcess, &QProcess::readyReadStandardError, this, &MainWindow::onProcessError);
    connect(currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProcessFinished);

    appendToConsole("\nInstalling dependencies...");
    
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("DEBIAN_FRONTEND", "noninteractive");  // For Ubuntu/Debian
    currentProcess->setProcessEnvironment(env);
    
    currentProcess->start("bash", QStringList() << "-c" << script);

    updateStopButtonState();  // Enable stop button
}

QString MainWindow::detectDistro()
{
    QProcess process;
    process.start("bash", QStringList() << "-c" << "cat /etc/*-release | grep -i 'PRETTY_NAME'");
    process.waitForFinished();
    QString output = QString::fromUtf8(process.readAllStandardOutput());
    return output;
}

void MainWindow::onBuildClicked()
{
    if (currentProcess) {
        appendToConsole("Another process is already running!", true);
        return;
    }

    // First install dependencies
    installDependencies();
    
    currentProcess = new QProcess(this);
    
    // Get the directory where the loader executable is located
    QString loaderPath = QCoreApplication::applicationFilePath();
    QString cathookDir = QFileInfo(loaderPath).dir().absolutePath();
    currentProcess->setWorkingDirectory(cathookDir);
    
    connect(currentProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onProcessOutput);
    connect(currentProcess, &QProcess::readyReadStandardError, this, &MainWindow::onProcessError);
    connect(currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProcessFinished);

    appendToConsole("\nStarting Cathook build...");
    appendToConsole("Working directory: " + cathookDir);
    
    // Build commands with environment preservation
    QStringList commands;
    commands << QString("cd '%1'").arg(cathookDir)
             << "source ~/.bashrc"
             << "mkdir -p build"
             << "cd build"
             << "LANG=C cmake -DCMAKE_BUILD_TYPE=Release -DTextMode=OFF -DENABLE_TEXTMODE_STDIN=OFF -DENABLE_GUI=ON .. 2>&1 | cat"  // Use cat to prevent interactive output
             << "LANG=C cmake --build . -j$(nproc) 2>&1 | cat"  // Use cat to prevent interactive output
             << "cd ..";
    
    QString script = commands.join(" && ");
    
    // Set up environment for better output handling
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "dumb");  // Prevent terminal control sequences
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("CMAKE_COLOR_DIAGNOSTICS", "OFF");  // Disable CMake colors
    currentProcess->setProcessEnvironment(env);
    
    currentProcess->start("bash", QStringList() << "-c" << script);

    updateStopButtonState();  // Enable stop button
}

void MainWindow::loadSettings()
{
    isDarkTheme = settings.value("theme/isDark", true).toBool();
    applyTheme(isDarkTheme);
}

void MainWindow::saveSettings()
{
    settings.setValue("theme/isDark", isDarkTheme);
    settings.sync();
}

void MainWindow::applyTheme(bool isDark)
{
    isDarkTheme = isDark;
    
    QString buttonStyle = QString(
        "QPushButton {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: none;"
        "    padding: 15px;"
        "    border-radius: 5px;"
        "    font-size: 16px;"
        "    min-width: 200px;"
        "}"
        "QPushButton:hover {"
        "    background-color: %3;"
        "}"
        "QPushButton:pressed {"
        "    background-color: %4;"
        "}")
        .arg(isDark ? "#4C6798" : "#f0f0f0")  // Cathook blu background / light gray
        .arg(isDark ? "#FFFFFF" : "#000000")  // Cathook blu / black
        .arg(isDark ? "#3C5883" : "#e0e0e0")  // Lighter blu / lighter gray
        .arg(isDark ? "#1D2E3F" : "#d0d0d0"); // Darker blu / darker gray

    QString consoleStyle = QString(
        "QTextEdit {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: none;"
        "    border-radius: 10px;"
        "    font-family: 'Courier New';"
        "}")
        .arg(isDark ? "#141E2B" : "#ffffff")  // Slightly darker than #1B2838 for console
        .arg(isDark ? "#ffffff" : "#000000"); // White text in dark mode, black in light

    QString mainStyle = QString(
        "QMainWindow {"
        "    background-color: %1;"
        "}")
        .arg(isDark ? "#1B2838" : "#f0f0f0"); // Dark blue background / light gray

    setStyleSheet(mainStyle + buttonStyle);
    consoleOutput->setStyleSheet(consoleStyle);
    
    // Update theme button text
    themeButton->setText(isDark ? "Switch to Light Mode" : "Switch to Dark Mode");
    
    // Special handling for stop button when enabled
    if (stopButton->isEnabled()) {
        stopButton->setStyleSheet(
            "QPushButton { background-color: #ed2a2a; color: white; }"  // Cathook red
            "QPushButton:hover { background-color: #ff3a3a; }"          // Lighter red
            "QPushButton:pressed { background-color: #dd1a1a; }"        // Darker red
        );
    }
    
    // Update existing console text color
    QTextCursor cursor = consoleOutput->textCursor();
    consoleOutput->selectAll();
    consoleOutput->setTextColor(isDark ? Qt::white : Qt::black);
    consoleOutput->setTextCursor(cursor);
    
    saveSettings();
}

void MainWindow::onThemeToggled()
{
    applyTheme(!isDarkTheme);
} 