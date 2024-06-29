TEMPLATE = aux

INSTALLER = SyncManagerInstaller

INPUT = $$PWD/config/config.xml $$PWD/packages

installer.input = INPUT
installer.output = $$INSTALLER
installer.commands = binarycreator -c $$PWD/config/config.xml -p $$PWD/packages ${QMAKE_FILE_OUT}
installer.CONFIG += target_predeps no_link combine

QMAKE_EXTRA_COMPILERS += installer

DISTFILES += \
    config/ControlScript.qs \
    packages/SyncManager/data/ChangeLog.txt \
    packages/SyncManager/meta/InstallScript.qs \
    packages/SyncManager/meta/package.xml

FORMS += \
    packages/SyncManager/meta/TargetDirectory.ui
