#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QGuiApplication>

#include "configurator.h"
#include "../utils/networkproxy.h"
#include "../utils/utility.h"

Configurator &Configurator::instance()
{
    static Configurator configuratorInstance;
    return configuratorInstance;
}

QString Configurator::getAppFilePath()
{
    if (QProcessEnvironment::systemEnvironment().contains("APPIMAGE")) {
        return QProcessEnvironment::systemEnvironment().value("APPIMAGE");
    }
    return QGuiApplication::applicationFilePath();
}

QString Configurator::getClashConfigPath()
{
    QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return homePath + "/.config/clash/";
}

QString Configurator::getClashConfigPath(const QString& name)
{
    QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return homePath + "/.config/clash/" + name + ".yaml";
}

void Configurator::saveClashConfig(const QString& name, const QString& data)
{
    QString filePath = Configurator::getClashConfigPath(name);
    QString tmpPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString tmpFile = tmpPath + "/" + name + ".yaml";
    QFile configFile(tmpFile);
    QString content = data;
    if (QFile::exists(tmpFile))
        QFile::remove(tmpFile);
    if (!configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        // open file failed
        return;
    } else {
        if (Utility::isBase64(content)) {
            content = QString(QByteArray::fromBase64(content.toUtf8()));
        }
        configFile.write(content.toUtf8());
        if (QFile::exists(filePath))
            QFile::remove(filePath);
        QFile::copy(tmpFile, filePath);
    }
}

YAML::Node Configurator::loadClashConfig(const QString& name)
{
    QString configFile = Configurator::getClashConfigPath(name);
    try {
        root = YAML::LoadFile(configFile.toStdString());
        if (root["mixed-port"])
            isMixedPort = true;
        else
            isMixedPort = false;
    } catch (YAML::BadFile& error) {
        throw error;
    }
    return root;
}

QVariant Configurator::loadValue(const QString &key, const QVariant &defaultValue)
{
    QSettings config;
    return config.value(key, defaultValue);
}

void Configurator::saveValue(const QString &key, const QVariant &value)
{
    QSettings config;
    config.setValue(key, value);
    config.sync();
}

QString Configurator::getSecret()
{
    if (root["secret"])
        return root["secret"].as<std::string>().c_str();
    return QString("");
}

QDateTime Configurator::getUpdateTime()
{
    QList<Subscribe> subscribes = getSubscribes();
    QDateTime time = QDateTime::currentDateTime();
    for (auto & subscribe : subscribes) {
        if (subscribe.name != "config") {
            time = subscribe.updateTime;
            break;
        }
    }
    return time;
}

QList<Subscribe> Configurator::getSubscribes()
{
    auto data = loadValue("subscribes").value<QList<QString>>();
    QList<Subscribe> subscribes;
    if (data.isEmpty())
        subscribes.append(Subscribe("config"));
    for (auto & i : data) {
        subscribes.append(Subscribe(stringToJson(i)));
    }
    return subscribes;
}

void Configurator::setSubscribes(const QList<Subscribe> &subscribes)
{
    QList<QString> data;
    for (const auto & subscribe : subscribes) {
        data.append(jsonToString(subscribe.write()));
    }
    saveValue("subscribes", QVariant::fromValue(data));
}

Subscribe Configurator::getCurrentConfig()
{
    auto data = loadValue("currentConfig").value<QString>();
    if (data.isEmpty()) {
        return Subscribe("config");
    }
    return Subscribe(stringToJson(data));
}

void Configurator::setCurrentConfig(const Subscribe& subscribe)
{
    saveValue("currentConfig", QVariant::fromValue(jsonToString(subscribe.write())));
}

Subscribe Configurator::getSubscribeByName(const QString& name)
{
    QList<Subscribe> subscribes = getSubscribes();
    for (auto & subscribe : subscribes) {
        if (subscribe.name == name)
            return subscribe;
    }
    return Subscribe("config");
}

Subscribe Configurator::delSubscribeByIndex(int index)
{
    QList<Subscribe> subscribes = getSubscribes();
    Subscribe sub = subscribes[index];
    QString file = getClashConfigPath(sub.name);
    if (QFile::exists(file))
        QFile::remove(file);
    subscribes.removeAt(index);
    setSubscribes(subscribes);
    return sub;
}

QJsonObject Configurator::getProxyGroupsRule(const QString& name)
{
    proxyGroups = QJsonDocument::fromJson(loadValue("proxyGroupsRule").toString().toUtf8()).object();
    QJsonObject current;
    if (!proxyGroups.isEmpty())
        current = proxyGroups.value(name).toObject();
    // clean rule, remove same items in default config
    // saveValue("proxyGroupsRule", QVariant(proxyGroups));
    return current;
}

void Configurator::setProxyGroupsRule(const QString& name, const QString& group, const QString& proxy)
{
    QJsonObject oldRule = QJsonDocument::fromJson(loadValue("proxyGroupsRule").toString().toUtf8()).object();
    if (oldRule.isEmpty())
        oldRule.insert(name, QJsonObject());
    QJsonValueRef ref = oldRule.find(name).value();
    QJsonObject current = ref.toObject();
    current.insert(group, proxy);
    oldRule[name] = current;
    saveValue("proxyGroupsRule", QVariant(QJsonDocument(oldRule).toJson()));
}

void Configurator::setStartAtLogin(bool autoStart)
{
    saveValue("startAtLogin", autoStart);
    const QString APP_NAME = "qClash";
    const QString APP_PATH = QDir::toNativeSeparators(getAppFilePath());
#if defined(Q_OS_WIN)
    QSettings win_settings(
        "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat);
#elif defined(Q_OS_LINUX)
    QFile srcFile(":/tpl-linux-autostart.desktop");
    QFile dstFile(QString("%1/.config/autostart/qClash.desktop")
                      .arg(QDir::homePath()));
#elif defined(Q_OS_MAC)
    QFile srcFile(":/tpl-macos-autostart.plist");
    QFile dstFile(
        QString("%1/Library/LaunchAgents/com.clash.desktop.launcher.plist")
            .arg(QDir::homePath()));
#endif

#if defined(Q_OS_WIN)
    if (autoStart) {
        win_settings.setValue(APP_NAME, APP_PATH);
    } else {
        win_settings.remove(APP_NAME);
    }
#elif defined(Q_OS_LINUX) or defined(Q_OS_MAC)
    if (autoStart) {
        QString fileContent;
        if (srcFile.exists() && srcFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            fileContent = srcFile.readAll();
            srcFile.close();
        }
        QFileInfo dstFileInfo(dstFile);
        if (!dstFileInfo.dir().exists()) {
            dstFileInfo.dir().mkpath(".");
        }
        if (dstFile.open(QIODevice::WriteOnly)) {
            dstFile.write(fileContent.arg(APP_PATH).toUtf8());
            dstFile.close();
        }
    } else {
        if (dstFile.exists()) {
            dstFile.remove();
        }
    }
#endif
}

bool Configurator::isStartAtLogin()
{
    return loadValue("startAtLogin", false).toBool();
}

void Configurator::setAutoUpdate(bool autoUpdate)
{
    saveValue("autoUpdate", autoUpdate);
}

bool Configurator::isAutoUpdate()
{
    return loadValue("autoUpdate", true).toBool();
}

void Configurator::setSystemProxy(bool flag)
{
    saveValue("systemProxy", flag);
    if (flag) {
        NetworkProxy httpProxy("http", "127.0.0.1",
            getHttpPort(), NetworkProxyMode::GLOBAL_MODE);
        NetworkProxyHelper::setSystemProxy(httpProxy);
#ifndef Q_OS_WIN
        NetworkProxy socksProxy("socks", "127.0.0.1",
            getSocksPort(), NetworkProxyMode::GLOBAL_MODE);
        NetworkProxyHelper::setSystemProxy(socksProxy);
#endif
    } else {
        NetworkProxyHelper::resetSystemProxy();
    }
}

bool Configurator::isSystemProxy()
{
    return loadValue("systemProxy", false).toBool();
}

QMap<QString, QString> Configurator::diffConfigs()
{
    QMap<QString, QString> configs;

    QString modeSaved = loadValue("mode", "").toString();
    QString modeYaml = root["mode"].as<std::string>().c_str();
    if (!modeSaved.isEmpty() && modeSaved != modeYaml)
        configs["mode"] = modeSaved;

    int httpPortSaved = loadValue("port", -1).toInt();
    int httpPortYaml = root["port"].as<int>();
    if (httpPortSaved != -1 && httpPortSaved != httpPortYaml)
        configs["port"] = QString::number(httpPortSaved);

    int socksPortSaved = loadValue("socksPort", -1).toInt();
    int socksPortYaml = root["socks-port"].as<int>();
    if (socksPortSaved != -1 && socksPortSaved != socksPortYaml)
        configs["socks-port"] = QString::number(socksPortSaved);

    bool allowLanSaved = loadValue("allowLan", false).toBool();
    bool allowLanYaml = root["allow-lan"].as<bool>();
    if (allowLanSaved != allowLanYaml)
        configs["allow-lan"] = QString::number(allowLanSaved);

    QString logLevelSaved = loadValue("logLevel", "").toString();
    QString logLevelYaml = root["log-level"].as<std::string>().c_str();
    if (!logLevelSaved.isEmpty() && logLevelSaved != logLevelYaml)
        configs["log-level"] = logLevelSaved;

    return configs;
}

void Configurator::setMode(const QString& mode)
{
    saveValue("mode", mode);
}

QString Configurator::getMode()
{
    return loadValue("mode", root["mode"].as<std::string>().c_str()).toString();
}

void Configurator::setHttpPort(int port)
{
    saveValue("httpPort", port);
}

int Configurator::getHttpPort()
{
    if(isMixedPort)
        return loadValue("mixed-port", root["mixed-port"].as<int>()).toInt();
    return loadValue("port", root["port"].as<int>()).toInt();
}

void Configurator::setSocksPort(int port)
{
    saveValue("socksPort", port);
}

int Configurator::getSocksPort()
{
    if(isMixedPort)
        return loadValue("mixed-port", root["mixed-port"].as<int>()).toInt();
    return loadValue("socksPort", root["socks-port"].as<int>()).toInt();
}

void Configurator::setExternalControlPort(int port)
{
    saveValue("externalControlPort", port);
}

int Configurator::getExternalControlPort()
{
    return loadValue("externalControlPort", QString(root["external-controller"].as<std::string>().c_str()).split(":")[1].toInt()).toInt();
}

void Configurator::setAllowLan(bool flag)
{
    saveValue("allowLan", flag);
}

bool Configurator::getAllowLan()
{
    return loadValue("allowLan", root["allow-lan"].as<bool>()).toBool();
}

void Configurator::setLogLevel(const QString& level)
{
    saveValue("logLevel", level);
}

QString Configurator::getLogLevel()
{
    QString level;
    if (!root["log-level"])
        level = "info";
    else
        level = QString(root["log-level"].as<std::string>().c_str());
    return loadValue("logLevel", level).toString();
}
