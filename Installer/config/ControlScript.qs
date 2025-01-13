function Controller()
{
    if (installer.value("os") == "win")
    {
        installer.setValue("ProductUUID", "SyncManager");

        var previous = installer.value("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SyncManager\\InstallLocation");
        if(previous !== "")
        {
            installer.setValue("TargetDir", previous);
            installer.setDefaultPageVisible(QInstaller.TargetDirectory, false);
        }
    }
}

Controller.prototype.IntroductionPageCallback = function()
{
    gui.clickButton(buttons.NextButton);
}
