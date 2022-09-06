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
        if (targetDirectory && targetDirectory.createShortcutOnDesktopCheckBox.checked)
		{
			component.addOperation("CreateShortcut", "@TargetDir@/SyncManager.exe",
								   "@DesktopDir@/Sync Manager.lnk",
								   "workingDirectory=@TargetDir@",
								   "iconPath=@TargetDir@/SyncManager.exe",
								   "iconId=0", "description=Sync Manager");
        }
    }
}

Component.prototype.currentPageChanged = function(page)
{
    try
	{
		if (installer.isInstaller())
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
