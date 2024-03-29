function Component()
{
    installer.setDefaultPageVisible(QInstaller.Introduction, false);
    installer.setDefaultPageVisible(QInstaller.TargetDirectory, true);
    installer.setDefaultPageVisible(QInstaller.ComponentSelection, false);
    installer.setDefaultPageVisible(QInstaller.StartMenuSelection, false);
    installer.setDefaultPageVisible(QInstaller.ReadyForInstallation, false);
    
    installer.currentPageChanged.connect(this, Component.prototype.currentPageChanged);
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    
    if (installer.isInstaller())
    {
        var targetDirectory = component.userInterface("targetDirectoryForm");

        if (installer.value("os") == "win")
        {
            if (targetDirectory && targetDirectory.createShortcutOnDesktopCheckBox.checked)
            {
                component.addOperation("CreateShortcut", "@TargetDir@/SyncManager.exe",
                                       "@DesktopDir@/Sync Manager.lnk",
                                       "workingDirectory=@TargetDir@",
                                       "iconPath=@TargetDir@/SyncManager.exe",
                                       "iconId=0", "description=Sync Manager");
            }

            component.addOperation("CreateShortcut", "@TargetDir@/SyncManager.exe",
                                   "@UserStartMenuProgramsPath@/@StartMenuDir@/Sync Manager.lnk",
                                   "workingDirectory=@TargetDir@",
                                   "iconPath=@TargetDir@/SyncManager.exe",
                                   "iconId=0", "description=Sync Manager");

            component.addOperation("CreateShortcut", "@TargetDir@/maintenancetool.exe",
                                   "@UserStartMenuProgramsPath@/@StartMenuDir@/Maintenance Tool.lnk",
                                   "workingDirectory=@TargetDir@",
                                   "iconPath=@TargetDir@/SyncManager.exe",
                                   "iconId=0", "description=Sync Manager");
        }
        else if (installer.value("os") == "x11")
        {
            if (targetDirectory && targetDirectory.createShortcutOnDesktopCheckBox.checked)
            {
                component.addElevatedOperation("CreateDesktopEntry",
                                               "@HomeDir@/Desktop/SyncManager.desktop",
                                               "Version=1.0\nType=Application\nName=Sync Manager\nTerminal=false\nExec=@TargetDir@/SyncManager\nIcon=@TargetDir@/Icon.png\nCategories=Utilities");
            }

            component.addElevatedOperation("CreateDesktopEntry",
                                           "/usr/share/applications/SyncManager.desktop",
                                           "Version=1.0\nType=Application\nName=Sync Manager\nTerminal=false\nExec=@TargetDir@/SyncManager\nIcon=@TargetDir@/Icon.png\nCategories=Utilities");
        }
    }
}

Component.prototype.currentPageChanged = function(page)
{
    try
    {
        if (installer.isInstaller() && installer.value("os") == "win")
        {
            if (page == QInstaller.TargetDirectory)
                installer.addWizardPageItem(component, "targetDirectoryForm", QInstaller.TargetDirectory);
        }
    }
    catch(e)
    {
        console.log(e);
    }
}
