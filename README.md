# Introduction

**SyncManager** is a custom-made lightweight synchronization manager that helps you synchronize files and folders across different locations. SyncManager lets you synchronize your data using three synchronization modes: manually, automatic (adaptive), or automatic (fixed). Manual syncing mode lets you decide what and when to synchronize data, while automatic (adaptive) mode synchronizes data based on its average synchronization time, and automatic (fixed) mode synchronizes data at predetermined intervals. It also offers three distinct synchronization types: two-way, one-way, and one-way update, allowing you to synchronize data between folders in different ways. Furthermore, it has filtering and customizable versioning settings. You can also decide how to handle old versions of files and folders: move them to the trash, delete them permanently, or version them in a special folder. The utility is primarily designed to run in the system tray, but it can also function without system tray interaction, operating as a fully automated solution that seamlessly launches at system startup. Written using Qt Framework for Windows/Linux.

![SyncManagerLight](https://github.com/user-attachments/assets/10558a09-0d79-4a14-9be5-627cf785b0f9)

# How It Works
Here's an overview of how it works:
1. Loads its databases from disk, if available, to restore prior file information.
1. Scans the designated folders for files and folders, looking for changes in file modified dates and file sizes.
1. Detects changes and the type of synchronization that should be performed between folders.
1. Based on the detected changes, synchronizes files across all folders.
1. Updates its databases on disk, saving the latest file information for the next synchronization.
### Syncing Modes
Synchronization can be triggered using the following modes:
- **Manual** - *(Lets you decide when to synchronize certain profiles)*
- **Automatic (Adaptive)** - *(Synchronizes data based on its average synchronization time, multiplied by the frequency multiplier, with a minimum interval of 1 second)*
- **Automatic (Fixed)** - *(Synchronizes data based on a fixed interval)*
### Synchronization Types
There are three synchronization types that determine how a folder should be synchronized.
- **Two-way** - *(Synchronizes files in both direction)*
- **One-way** - *(Basically, mirrors two-way folders, removing anything that doesn't exist in that folders)*
- **One-way update** - *(Synchronizes files from two-way folders only once, and they can be freely deleted from the folder. Files don't get deleted if they have been deleted from two-way folders)*
### Change Detection Order
SyncManager uses sophisticated algorithms to detect changes since the last synchronization to determine which files and folders must be synchronized in the following order:
1. **File attribute changes**
1. **Case-changed folders** - *(e.g., "MyFolder" to "myfolder"*)
1. **Moved or renamed files** - *(Including case changes (e.g., "FILE.txt" to "file.txt")*)
1. **New additions and file changes**
1. **Deleted files and folders**
### Detection of Case-Changed Folders
#### *(Only for case-insensitive systems)*
Since SyncManager doesn't store the original paths of files in a database, it relies on a filepath comparison-based approach between synchronization folders to detect case changes in folder names. It compares the current filename of a newly renamed folder in one location with the corresponding folder's filenames in other locations (if they exist), checking for differences in case naming. If a difference is found, SyncManager considers the case of the folder was changed and renames folders accordingly, matching the folder's filename in the source location.
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
- **Versioning** - *(Moves files to the versioning folder according to the designated versioning format)*
- **Delete Files Permanently** - *(Irreversible, frees storage immediately)*  
### Versioning Formats
Files can be versioned in the following formats:
- **File timestamp before extension** - *(The timestamp is added right before the file's extension (e.g., [Filename][Timestamp].[Extension]))*
- **File timestamp after extension** - *(The timestamp is added after the file's extension (e.g., [Filename].[Extension].[Timestamp].[Extension]))*
- **Folder timestamp** - *(Files are moved into a new folder named with a timestamp, while keeping their original filename and extension (e.g., [folder timestamp]/[filename].[Extension]))*
- **Last version** - *(Only the most recently deleted version of your files is kept, overwriting any older deleted versions)*
### Versioning Location
Files can be moved for versioning in the following locations:
1. Locally, in a separate folder next to a synchronization folder, that has the same name, but with the specified versioning postfix added.
2. In a custom location designated by a user, within folders that follow this pattern: [Versioning folder]/[Profile name]/[Folder name], without the specified versioning prefix.
### Filtering
Files can be filtered from synchronization using the following options:
- **Minimum file size**
- **Maximum file size**
- **Minimum size for a moved file** - *(The size at which files are allowed to be detected as moved or renamed)*
- **Include** - *(Whitelist)*
- **Exclude** - *(Blacklist)*

# Building
Requires Qt 6.9 or newer. Buildable with Qt Creator.

# License
SyncManager is licensed under the GPL-3.0 license, see LICENSE.txt for more information.
