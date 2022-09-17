TEMPLATE = aux

INSTALLER = SyncManagerInstaller

INPUT = $$PWD/config/config-win.xml $$PWD/config/config-linux.xml $$PWD/packages

installer.input = INPUT
installer.output = $$INSTALLER
installer.CONFIG += target_predeps no_link combine
win32: installer.commands = binarycreator -c $$PWD/config/config-win.xml -p $$PWD/packages ${QMAKE_FILE_OUT}
linux: installer.commands = binarycreator -c $$PWD/config/config-linux.xml -p $$PWD/packages ${QMAKE_FILE_OUT}

QMAKE_EXTRA_COMPILERS += installer

DISTFILES += \
    config/ControlScript.qs \
    packages/SyncManager/meta/InstallScript.qs \
    packages/SyncManager/meta/package.xml

FORMS += \
    packages/SyncManager/meta/TargetDirectory.ui
