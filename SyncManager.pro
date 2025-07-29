QT += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20 lrelease embed_translations

SOURCES += \
    Source/Application.cpp \
    Source/Common.cpp \
    Source/CpuUsage.cpp \
    Source/DecoratedStringListModel.cpp \
    Source/FolderListView.cpp \
    Source/FolderStyleDelegate.cpp \
    Source/Main.cpp \
    Source/MainWindow.cpp \
    Source/MenuProxyStyle.cpp \
    Source/RemovableListView.cpp \
    Source/SyncFile.cpp \
    Source/SyncFolder.cpp \
    Source/SyncManager.cpp \
    Source/SyncProfile.cpp \
    Source/UnhidableMenu.cpp

HEADERS += \
    Source/Application.h \
    Source/Common.h \
    Source/CpuUsage.h \
    Source/DecoratedStringListModel.h \
    Source/FolderListView.h \
    Source/FolderStyleDelegate.h \
    Source/MainWindow.h \
    Source/MenuProxyStyle.h \
    Source/RemovableListView.h \
    Source/SyncFile.h \
    Source/SyncFolder.h \
    Source/SyncManager.h \
    Source/SyncProfile.h \
    Source/UnhidableMenu.h

FORMS += \
    Source/MainWindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    Source/Icon.ico \
    Source/Resources.rc \
    LICENSE.txt \
    README.md

RESOURCES += \
    SyncManager.qrc

RC_FILE = Source/Resources.rc

# qml_debug flag for debug and profile build configurations
CONFIG(qml_debug): DEFINES += DEBUG

TRANSLATIONS += \
    Translations/da_DK.ts \
    Translations/de_DE.ts \
    Translations/en_US.ts \
    Translations/es_ES.ts \
    Translations/fr_FR.ts \
    Translations/hi_IN.ts \
    Translations/it_IT.ts \
    Translations/ja_JP.ts \
    Translations/ko_KR.ts \
    Translations/pt_PT.ts \
    Translations/ru_RU.ts \
    Translations/uk_UA.ts \
    Translations/zh_CN.ts
