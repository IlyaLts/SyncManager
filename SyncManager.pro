QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    Common.cpp \
    DecoratedStringListModel.cpp \
    FolderListView.cpp \
    Main.cpp \
    MainWindow.cpp \
    RemovableListView.cpp \
    SyncFile.cpp \
    SyncFolder.cpp \
    SyncManager.cpp \
    SyncProfile.cpp \
    UnhidableMenu.cpp

HEADERS += \
    Common.h \
    DecoratedStringListModel.h \
    FolderListView.h \
    MainWindow.h \
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
