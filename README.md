# Introduction

**SyncManager** is a custom-made lightweight synchronization manager that helps you synchronize files and folders across different locations. SyncManager lets you synchronize your data in two ways: manually or automatically. Manual syncing mode lets you decide what and when to synchronize data, while automatic syncing mode synchronizes data based on the time elapsed since the previous synchronization. You can also decide how to handle old versions of files and folders: move them to the trash, delete them permanently, or version them in a special folder. The utility is mostly designed to run in the system tray, but it can also work without any system tray interactions. Written using Qt Framework for Windows/Linux.

![SyncManager](https://user-images.githubusercontent.com/5786770/207924637-b7baa56a-1426-4e6a-8d96-04e1b8379e26.png)

# How It Works
Here's a step-by-step overview of how it works:
#### 1. File and Folder Discovery
SyncManager starts by scanning the designated source and target locations for files and folders. This includes recursively traversing through subfolders to ensure all files are accounted for.
#### 2. Change Detection
SyncManager uses sophisticated algorithms to detect changes between the current and previous states to determine which files and folders must be synchronized in the following order:
1. **File attribute changes:** Changes to file attributes.
1. **Case-changed folders:** Folders that have been renamed solely by changing the case of the folder name (e.g., "MyFolder" to "myfolder").
1. **Moved or renamed files:** Files that have been moved or renamed, including case changes (e.g., "FILE.txt" to "file.txt").
1. **New additions and changes:** New files and folders that have been added to the source or target location, as well as existing files that have been modified in some way.
1. **Deleted files and folders:** Files and folders that have been removed from the source or target location.
#### 3. Conflict Resolution
In cases where conflicts arise, SyncManager resolves them according to the following rules:
- **Latest modification date wins:** If a file has been modified in both locations, SyncManager will synchronize the file with the latest modification date.
- **Modified files take precedence:** If a file has been modified in one location and deleted in another, SyncManager will synchronize the modified file, effectively ignoring the deletion.
- **Folder content changes take precedence:** If a folder has had new files added or existing files removed in one location, and the same folder has been deleted in another location, SyncManager will synchronize the folder with the updated content, effectively ignoring the deletion. Single changes to existing files within the folder do not trigger this precedence. 
#### 4. Synchronization Execution
Based on the changes detected, SyncManager performs the following synchronization operations in the following order:
1. **Synchronizes file attributes:** Updates file attributes to match the source location.
1. **Renames case-changed folders:** Renames folders that have undergone case-only changes.
1. **Renames and moves files:** Renames and relocates files to their new destinations.
1. **Removes deleted folders:** Deletes folders that have been removed from the source location.
1. **Removes deleted files:** Deletes files that have been removed from the source location.
1. **Creates new folders:** Creates new folders that have been added to the source location.
1. **Copies new or modified files:** Copies new or modified files from the source location to the target location.

# Building
Requires Qt 6 or newer. Buildable with Qt Creator.

# License
SyncManager is licensed under the GPL-3.0 license, see LICENSE.txt for more information.
