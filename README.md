# Introduction

**SyncManager** is a custom-made lightweight synchronization manager that helps you synchronize files and folders across different locations. SyncManager lets you synchronize your data in two ways: manually or automatically. Manual syncing mode lets you decide what and when to synchronize data, while automatic syncing mode synchronizes data based on its average synchronization time. You can also decide how to handle old versions of files and folders: move them to the trash, delete them permanently, or version them in a special folder. The utility is mostly designed to run in the system tray, but it can also work without any system tray interactions. Written using Qt Framework for Windows/Linux.

![SyncManagerLight](https://github.com/user-attachments/assets/c25d45ee-5051-463a-b183-8a6408cf3367)

# How It Works
Here's an overview of how it works:
### Syncing Modes
Synchronization can be triggered using the following modes:
- **Manual** - *(Lets you decide what and when to synchronize certain profiles)*
- **Automatic** - *(Synchronizes data based on its average synchronization time, multiplied by the SyncManager multiplier, with a minimum delay of 1 second and a maximum delay of around 25 days)*
### File and Folder Discovery
SyncManager starts by scanning the designated source and target locations for files and folders.
### Change Detection Order
SyncManager uses sophisticated algorithms to detect changes since the last synchronization to determine which files and folders must be synchronized in the following order:
1. **File attribute changes**
1. **Case-changed folders** - *(e.g., "MyFolder" to "myfolder"*)
1. **Moved or renamed files** - *(Including case changes (e.g., "FILE.txt" to "file.txt")*)
1. **New additions and file changes**
1. **Deleted files and folders**
### Detection of Case-Changed Folders
#### *(Only for case-insensitive systems)*
Since SyncManager doesn't store the original paths of files, it relies on a filepath comparison-based approach between synchronization folders to detect case changes in folder names. It compares the current filename of a newly renamed folder in one location with the corresponding folder's filenames in other locations (if they exist), checking for differences in case naming. If a difference is found, SyncManager assumes the case of the folder was changed and renames folders accordingly, matching the folder's filename in the source location.
### Detection of Moved and Renamed Files
SyncManager searches for matches between removed and new files based on their modified date and size. If a match is found, the file is considered to be the same, and SyncManager renames or moves the corresponding file to other locations to match the new location in the source. In cases where there are multiple matches with the same modified date and size, SyncManager falls back to the standard synchronization method, copying files from one location to another.
### Conflict Resolution
In cases where conflicts arise, SyncManager resolves them according to the following rules:
- **Latest modification date wins:** If a file has been modified in both locations, SyncManager will synchronize the file with the latest modification date.
- **Modified files take precedence:** If a file has been modified in one location and deleted in another, SyncManager will synchronize the modified file, effectively ignoring the deletion.
- **Folder content changes take precedence:** If a folder has had new files added or existing files removed in one location, and the same folder has been deleted in another, SyncManager will synchronize the folder with the updated content, effectively ignoring the deletion. Single changes to existing files within the folder do not trigger this precedence.
### Synchronization Order
Based on the changes detected, SyncManager performs the synchronization operations in the following order:
1. **Synchronizes file attributes**
1. **Renames case-changed folders**
1. **Renames and moves files**
1. **Removes deleted folders**
1. **Removes deleted files**
1. **Creates new folders**
1. **Copies new or modified files**
### Deletion Modes
When SyncManager needs to synchronize a file from one location to another but finds an older version of the file in the destination, it must delete the existing file first using the following methods:
- **Move files to Trash** - *(Allows recovery, does not free storage)*
- **Versioning** - *(Move files to a time-stamped folder within the sibling folder with "_[Deletions]" postfix)*
- **Delete Files Permanently** - *(Irreversible, frees storage immediately)*

# Building
Requires Qt 6.8 or newer. Buildable with Qt Creator.

# License
SyncManager is licensed under the GPL-3.0 license, see LICENSE.txt for more information.
