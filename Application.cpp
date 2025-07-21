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

Language defaultLanguage = { QLocale::English, QLocale::UnitedStates, ":/i18n/en_US.qm", "&English" };

Language languages[] =
    {
        {QLocale::Chinese,      QLocale::China,         ":/i18n/zh_CN.qm", "&Chinese"},
        {QLocale::English,      QLocale::UnitedStates,  ":/i18n/en_US.qm", "&English"},
        {QLocale::French,       QLocale::France,        ":/i18n/fr_FR.qm", "&French"},
        {QLocale::German,       QLocale::Germany,       ":/i18n/de_DE.qm", "&German"},
        {QLocale::Hindi,        QLocale::India,         ":/i18n/hi_IN.qm", "&Hindi"},
        {QLocale::Italian,      QLocale::Italy,         ":/i18n/it_IT.qm", "&Italian"},
        {QLocale::Japanese,     QLocale::Japan,         ":/i18n/ja_JP.qm", "&Japanese"},
        {QLocale::Portuguese,   QLocale::Portugal,      ":/i18n/pt_PT.qm", "&Portuguese"},
        {QLocale::Russian,      QLocale::Russia,        ":/i18n/ru_RU.qm", "&Russian"},
        {QLocale::Spanish,      QLocale::Spain,         ":/i18n/es_ES.qm", "&Spanish"},
        {QLocale::Ukrainian,    QLocale::Ukraine,       ":/i18n/uk_UA.qm", "&Ukrainian"},
        {QLocale::Korean,       QLocale::SouthKorea,    ":/i18n/ko_KR.qm", "&Korean"},
        {QLocale::Danish,       QLocale::Denmark,       ":/i18n/da_DK.qm", "&Danish"}
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
    setTranslator(static_cast<QLocale::Language>(settings.value("Language", systemLanguage).toInt()));
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
Application::setTranslator
===================
*/
void Application::setTranslator(QLocale::Language language)
{
    QCoreApplication::removeTranslator(&translator);
    bool result = false;

    for (int i = 0; i < languageCount(); i++)
    {
        if (languages[i].language != language)
            continue;

        result = translator.load(languages[i].path);
        locale = QLocale(languages[i].language, languages[i].country);

        if (!result)
            qWarning("Unable to load %s language", qPrintable(QLocale::languageToString(language)));

        break;
    }

    // Loads English by default if the specified language is not in the list
    if (!result)
    {
        result = translator.load(defaultLanguage.path);
        locale = QLocale(defaultLanguage.language, defaultLanguage.country);

        if (!result)
        {
            qWarning("Unable to load %s language", qPrintable(QLocale::languageToString(defaultLanguage.language)));
            return;
        }
    }

    QCoreApplication::installTranslator(&translator);
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
