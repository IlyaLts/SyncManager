/*
===============================================================================
    Copyright (C) 2022-2025 Ilya Lyakhovets

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
===============================================================================
*/

#include "Application.h"
#include "Common.h"
#include <QString>
#include <QStandardPaths>
#include <QFile>
#include <QSettings>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QPushButton>
#include <QBoxLayout>
#include <QTextBrowser>
#include <QThread>

Language defaultLanguage = { QLocale::English, QLocale::UnitedStates, ":/i18n/en_US.qm", ":/Images/flags/us.svg", "&English" };

Language languages[] =
{
    {QLocale::Chinese,      QLocale::China,         ":/i18n/zh_CN.qm",  ":/Images/Flags/cn.svg", "&Chinese"},
    {QLocale::Danish,       QLocale::Denmark,       ":/i18n/da_DK.qm",  ":/Images/Flags/dk.svg", "&Danish"},
    {QLocale::English,      QLocale::UnitedStates,  ":/i18n/en_US.qm",  ":/Images/Flags/us.svg", "&English"},
    {QLocale::French,       QLocale::France,        ":/i18n/fr_FR.qm",  ":/Images/Flags/fr.svg", "&French"},
    {QLocale::German,       QLocale::Germany,       ":/i18n/de_DE.qm",  ":/Images/Flags/de.svg", "&German"},
    {QLocale::Hindi,        QLocale::India,         ":/i18n/hi_IN.qm",  ":/Images/Flags/in.svg", "&Hindi"},
    {QLocale::Italian,      QLocale::Italy,         ":/i18n/it_IT.qm",  ":/Images/Flags/it.svg", "&Italian"},
    {QLocale::Japanese,     QLocale::Japan,         ":/i18n/ja_JP.qm",  ":/Images/Flags/jp.svg", "&Japanese"},
    {QLocale::Korean,       QLocale::SouthKorea,    ":/i18n/ko_KR.qm",  ":/Images/Flags/kr.svg", "&Korean"},
    {QLocale::Polish,       QLocale::Poland,        ":/i18n/pl_PL.qm",  ":/Images/Flags/pl.svg", "&Polish"},
    {QLocale::Portuguese,   QLocale::Portugal,      ":/i18n/pt_PT.qm",  ":/Images/Flags/pt.svg", "&Portuguese"},
    {QLocale::Russian,      QLocale::Russia,        ":/i18n/ru_RU.qm",  ":/Images/Flags/ru.svg", "&Russian"},
    {QLocale::Spanish,      QLocale::Spain,         ":/i18n/es_ES.qm",  ":/Images/Flags/es.svg", "&Spanish"},
    {QLocale::Turkish,      QLocale::Turkey,        ":/i18n/tr_TR.qm",  ":/Images/Flags/tr.svg", "&Turkish"},
    {QLocale::Ukrainian,    QLocale::Ukraine,       ":/i18n/uk_UA.qm",  ":/Images/Flags/ua.svg", "&Ukrainian"}
};

/*
===================
Application::Application
===================
*/
Application::Application(int &argc, char **argv) : QApplication(argc, argv)
{
    QString localDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QSettings settings(localDataPath + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QLocale::Language systemLanguage = QLocale::system().language();
    setLanguage(static_cast<QLocale::Language>(settings.value("Language", systemLanguage).toInt()));
}

/*
===================
Application::~Application
===================
*/
Application::~Application()
{
    if (syncApp->m_syncThread->isRunning())
    {
        syncApp->m_syncThread->requestInterruption();
        syncApp->m_syncThread->quit();

        if (syncApp->m_syncThread->isRunning())
        {
            syncApp->m_syncThread->terminate();
            syncApp->m_syncThread->wait();
        }
    }
}

/*
===================
Application::init
===================
*/
void Application::init()
{
    m_netManager = new QNetworkAccessManager(this);
    connect(m_netManager, &QNetworkAccessManager::finished, this, &Application::onUpdateReply);
    connect(&m_updateTimer, &QTimer::timeout, this, &Application::checkForUpdates);

    m_syncThread = new QThread(this);
    m_tray = new SystemTray;
    m_manager.reset(new SyncManager);
    m_window.reset(new MainWindow);
    m_cpuUsage = new CpuUsage(this);

    m_manager->moveToThread(m_syncThread);
    connect(m_syncThread, &QThread::started, m_manager.data(), [this](){ m_manager->sync(); });
    connect(m_manager.data(), &SyncManager::finished, this, [this](){ m_syncThread->quit(); });

    connect(m_cpuUsage, &CpuUsage::cpuUsageUpdated, this, &Application::updateCpuUsage);
    m_cpuUsage->startMonitoring(CPU_UPDATE_TIME);

    loadSettings();
    m_initiated = true;
}

/*
===================
Application::setLanguage
===================
*/
void Application::setLanguage(QLocale::Language language)
{
    QCoreApplication::removeTranslator(&m_translator);
    bool result = false;

    for (int i = 0; i < languageCount(); i++)
    {
        if (languages[i].language != language)
            continue;

        result = m_translator.load(languages[i].filePath);
        m_locale = QLocale(languages[i].language, languages[i].country);

        if (!result)
            qWarning("Unable to load %s language", qPrintable(QLocale::languageToString(language)));

        break;
    }

    // Loads English by default if the specified language is not in the list
    if (!result)
    {
        result = m_translator.load(defaultLanguage.filePath);
        m_locale = QLocale(defaultLanguage.language, defaultLanguage.country);

        if (!result)
        {
            qWarning("Unable to load %s language", qPrintable(QLocale::languageToString(defaultLanguage.language)));
            return;
        }
    }

    QCoreApplication::installTranslator(&m_translator);

    m_language = language;

    if (initiated())
    {
        m_window->retranslate();
        m_tray->retranslate();
        saveSettings();

        emit languageChanged();
    }
}

/*
===================
Application::setTrayVisible
===================
*/
void Application::setTrayVisible(bool visible)
{
    if (QSystemTrayIcon::isSystemTrayAvailable() && visible)
    {
        m_trayVisible = visible;
        m_tray->show();
        setQuitOnLastWindowClosed(false);
    }
    else
    {
        m_trayVisible = false;

        QString title = syncApp->translate("MainWindow", "System Tray is not available!");
        QString text = syncApp->translate("MainWindow", "Your system does not support the system tray.");
        QMessageBox::warning(NULL, title, text);

        m_tray->hide();
        setQuitOnLastWindowClosed(true);

        if (initiated())
            m_window->QMainWindow::show();
    }
}

/*
===================
Application::setLaunchOnStartup
===================
*/
void Application::setLaunchOnStartup(bool enable)
{
    QString path;

#ifdef Q_OS_WIN
    path = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Startup/SyncManager.lnk";
#elif defined(Q_OS_LINUX)
    path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/SyncManager.desktop";
#endif

    if (enable)
    {
#ifdef Q_OS_WIN
        QFile::link(QCoreApplication::applicationFilePath(), path);
#elif defined(Q_OS_LINUX)

        // Creates autostart folder if it doesn't exist
        QDir().mkdir(QFileInfo(path).path());

        QFile::remove(path);
        QFile shortcut(path);
        if (shortcut.open(QIODevice::WriteOnly))
        {
            QTextStream stream(&shortcut);
            stream << "[Desktop Entry]\n";
            stream << "Type=Application\n";

            if (QFileInfo::exists(QFileInfo(QCoreApplication::applicationDirPath()).path() + "/SyncManager.sh"))
                stream << "Exec=" + QCoreApplication::applicationDirPath() + "/SyncManager.sh\n";
            else
                stream << "Exec=" + QCoreApplication::applicationDirPath() + "/SyncManager\n";

            stream << "Hidden=false\n";
            stream << "NoDisplay=false\n";
            stream << "Terminal=false\n";
            stream << "Name=Sync Manager\n";
            stream << "Icon=" + QCoreApplication::applicationDirPath() + "/SyncManager.png\n";
        }

// Somehow doesn't work on Linux
//QFile::link(QCoreApplication::applicationFilePath(), path);
#endif
    }
    else
    {
        QFile::remove(path);
    }
}

/*
===================
Application::setCheckForUpdates
===================
*/
void Application::setCheckForUpdates(bool enable)
{
    m_checkForUpdates = enable;

    if (enable)
        m_updateTimer.start(CHECK_FOR_UPDATE_TIME);
    else
        m_updateTimer.stop();
}

/*
===================
Application::setMaxCpuUsage
===================
*/
void Application::setMaxCpuUsage(float percentage)
{
    if (percentage <= 0.0f)
        percentage = 0.1f;

    if (percentage > 100.0f)
        percentage = 100.0f;

    m_maxCpuUsage = percentage;
}

/*
===================
Application::checkForUpdates
===================
*/
void Application::checkForUpdates()
{
    QNetworkRequest request;
    request.setUrl(QUrl(LATEST_RELEASE_API_URL));
    request.setRawHeader("User-Agent", "SyncManager");
    m_netManager->get(request);
}

/*
===================
Application::exec
===================
*/
int Application::exec()
{
    if (!m_initiated)
        return -1;

    if (!syncApp->trayVisible())
        m_window->show();

    return QApplication::exec();
}

/*
===================
Application::quit
===================
*/
void Application::quit()
{
    QString title(syncApp->translate("MainWindow", "Quit"));
    QString text(syncApp->translate("MainWindow", "Are you sure you want to quit?"));
    QString syncText(syncApp->translate("MainWindow", "Currently syncing. Are you sure you want to quit?"));

    if ((!m_manager->busy() && questionBox(QMessageBox::Question, title, text, QMessageBox::No, m_window.data())) ||
        (m_manager->busy() && questionBox(QMessageBox::Warning, title, syncText, QMessageBox::No, m_window.data())))
    {
        m_manager->shouldQuit();
        QApplication::quit();
    }
}

/*
===================
Application::loadSettings
===================
*/
void Application::loadSettings()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);

    setMaxCpuUsage(settings.value("MaximumCpuUsage", 100).toUInt());
    setLanguage(static_cast<QLocale::Language>(settings.value("Language", QLocale::system().language()).toInt()));
    setTrayVisible(settings.value("ShowInTray", QSystemTrayIcon::isSystemTrayAvailable()).toBool());
    setCheckForUpdates(settings.value("CheckForUpdates", true).toBool());

    if (syncApp->checkForUpdatesEnabled())
        syncApp->checkForUpdates();
}

/*
===================
Application::saveSettings
===================
*/
void Application::saveSettings() const
{
    if (!initiated())
        return;

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);

    settings.setValue("MaximumCpuUsage", maxCpuUsage());
    settings.setValue("Language", m_language);
    settings.setValue("ShowInTray", trayVisible());
    settings.setValue("CheckForUpdates", m_checkForUpdates);
}

/*
===================
Application::onUpdateReply
===================
*/
void Application::onUpdateReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
        return;

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());

    if (doc.isNull() || !doc.isObject())
        return;

    QJsonObject releaseObject = doc.object();
    QString lastVersion = releaseObject["tag_name"].toString();

    if (lastVersion.isEmpty())
        return;

    if (lastVersion.contains("Alpha", Qt::CaseInsensitive) || lastVersion.contains("Beta", Qt::CaseInsensitive))
        return;

    if (lastVersion != SYNCMANAGER_VERSION)
    {
        m_updateAvailable = true;
        emit updateFound();
    }
    else
    {
        m_updateAvailable = false;
    }
}

/*
===================
Application::languageCount
===================
*/
int Application::languageCount()
{
    return static_cast<int>(sizeof(languages) / sizeof(Language));
}

/*
===================
Application::textDialog
===================
*/
void Application::textDialog(const QString &title, const QString &text)
{
    QDialog dialog;
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QTextBrowser *textBrowser = new QTextBrowser();
    QPushButton *okButton = new QPushButton(translate("MainWindow", "OK"));

    dialog.setWindowTitle(title);
    textBrowser->setText(text);

    layout->addWidget(textBrowser);
    layout->addWidget(okButton);

    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

/*
===================
Application::questionBox
===================
*/
bool Application::questionBox(QMessageBox::Icon icon, const QString &title, const QString &text,
                              QMessageBox::StandardButton defaultButton, QWidget *parent)
{
    QMessageBox messageBox(icon, title, text, QMessageBox::NoButton, parent);

    QPushButton *yes = new QPushButton("&" + translate("MainWindow", "Yes"), parent);
    QPushButton *no = new QPushButton("&" + translate("MainWindow", "No"), parent);

    messageBox.addButton(yes, QMessageBox::YesRole);
    messageBox.addButton(no, QMessageBox::NoRole);
    messageBox.setDefaultButton(defaultButton == QMessageBox::Yes ? yes : no);

    messageBox.exec();
    return messageBox.clickedButton() == yes;
}

/*
===================
Application::intInputDialog
===================
*/
bool Application::intInputDialog(QWidget *parent, const QString &title, const QString &label,
                                 int &returnValue, int value, int minValue, int maxValue)
{
    QInputDialog *dialog = new QInputDialog(parent);
    dialog->setInputMode(QInputDialog::IntInput);
    dialog->setIntMinimum(minValue);
    dialog->setIntMaximum(maxValue);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    dialog->setIntValue(value);
    dialog->setOkButtonText("&" + translate("MainWindow", "OK"));
    dialog->setCancelButtonText("&" + translate("MainWindow", "Cancel"));
    dialog->deleteLater();

    if (dialog->exec())
    {
        returnValue = dialog->intValue();
        return true;
    }

    return false;
}

/*
===================
Application::doubleInputDialog
===================
*/
bool Application::doubleInputDialog(QWidget *parent, const QString &title, const QString &label,
                                    double &returnValue, double value, double minValue, double maxValue)
{
    QInputDialog *dialog = new QInputDialog(parent);
    dialog->setInputMode(QInputDialog::DoubleInput);
    dialog->setDoubleMinimum(minValue);
    dialog->setDoubleMaximum(maxValue);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    dialog->setDoubleValue(value);
    dialog->setOkButtonText("&" + translate("MainWindow", "OK"));
    dialog->setCancelButtonText("&" + translate("MainWindow", "Cancel"));
    dialog->deleteLater();

    if (dialog->exec())
    {
        returnValue = dialog->doubleValue();
        return true;
    }

    return false;
}

/*
===================
Application::textInputDialog
===================
*/
bool Application::textInputDialog(QWidget *parent, const QString &title, const QString &label,
                                  QString &returnText, const QString &text)
{
    QInputDialog *dialog = new QInputDialog(parent);
    dialog->setInputMode(QInputDialog::TextInput);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    dialog->setTextValue(text);
    dialog->setOkButtonText("&" + translate("MainWindow", "OK"));
    dialog->setCancelButtonText("&" + translate("MainWindow", "Cancel"));
    dialog->deleteLater();

    if (dialog->exec())
    {
        returnText = dialog->textValue();
        return true;
    }

    return false;
}

/*
===================
Application::updateCpuUsage
===================
*/
void Application::updateCpuUsage(float appPercentage, float systemPercentage)
{
    m_processUsage = appPercentage;
    m_systemUsage = systemPercentage;
}
