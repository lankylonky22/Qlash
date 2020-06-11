#include "clashCore.h"
#include "configurator.h"

#include <QDebug>
#include <QDir>
#include <QFile>

ClashCore::ClashCore()
{
#ifdef Q_OS_WIN
    clashFilePath = "D:/PortableApp/clash/clash.exe";
#elif defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    clashFilePath = "clash";
#endif

    QString clashConfigPath = Configurator::getClashConfigPath();
    QDir dir(clashConfigPath);
    if (!dir.exists()) {
        dir.mkpath(clashConfigPath);
        QFile::copy("./config/clash.yaml", clashConfigPath + "config.yaml");
        QFile::copy("./config/Country.mmdb", clashConfigPath + "Country.mmdb");
    }

    clashProcess = new QProcess;
}

ClashCore& ClashCore::instance()
{
    static ClashCore clashManagerInstance;
    return clashManagerInstance;
}

ClashCore::~ClashCore()
{
    this->stop();
    delete clashProcess;
}

bool ClashCore::isRunning()
{
    return clashProcess->state() == QProcess::Running;
}

bool ClashCore::start()
{
    clashProcess->start(clashFilePath);
    clashProcess->waitForFinished(500);
    int exitCode = clashProcess->exitCode();
    if (exitCode != 0) {
        qCritical() << "Failed to start clash!";
    }

    return exitCode == 0;
}

bool ClashCore::stop()
{
    clashProcess->kill();
    clashProcess->waitForFinished();
    return clashProcess->state() == QProcess::NotRunning;
}

bool ClashCore::restart()
{
    stop();
    start();
    return isRunning();
}