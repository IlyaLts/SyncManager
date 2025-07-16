QT += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20 lrelease embed_translations

SOURCES += \
    Application.cpp \
    Common.cpp \
    DecoratedStringListModel.cpp \
    FolderListView.cpp \
    Main.cpp \
    MainWindow.cpp \
    MenuProxyStyle.cpp \
    RemovableListView.cpp \
    SyncFile.cpp \
    SyncFolder.cpp \
    SyncManager.cpp \
    SyncProfile.cpp \
    UnhidableMenu.cpp

HEADERS += \
    Application.h \
    Common.h \
    DecoratedStringListModel.h \
    FolderListView.h \
    MainWindow.h \
    MenuProxyStyle.h \
    RemovableListView.h \
    SyncFile.h \
    SyncFolder.h \
    SyncManager.h \
    SyncProfile.h \
    UnhidableMenu.h

FORMS += \
    MainWindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    Icon.ico \
    Resources.rc \
    LICENSE.txt \
    README.md

RESOURCES += \
    SyncManager.qrc

RC_FILE = Resources.rc

# qml_debug flag for debug and profile build configurations
CONFIG(qml_debug): DEFINES += DEBUG

TRANSLATIONS += \
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
