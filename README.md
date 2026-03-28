# Introduction

**SyncManager** is a custom-made lightweight synchronization manager that helps you synchronize files and folders across different locations. SyncManager lets you synchronize your data using three synchronization modes: manually, automatic (adaptive), or automatic (fixed). Manual syncing mode lets you decide what and when to synchronize data, while automatic (adaptive) mode synchronizes data based on its average synchronization time, and automatic (fixed) mode synchronizes data at predetermined intervals. It also offers three distinct synchronization types: two-way, one-way, and one-way update, allowing you to synchronize data between folders in different ways. Furthermore, it has filtering and customizable versioning settings. You can also decide how to handle old versions of files and folders: move them to the trash, delete them permanently, or version them in a special folder. The utility is primarily designed to run in the system tray, but it can also function without system tray interaction, operating as a fully automated solution that seamlessly launches at system startup. Written using Qt Framework for Windows/Linux.

![SyncManagerLight](https://github.com/user-attachments/assets/10558a09-0d79-4a14-9be5-627cf785b0f9)

# How It Works
SyncManager works by keeping track of changes to your files. It follows the following steps:
1. Loads its databases from disk to remember what your files looked like during the last synchronization.
1. Scans your specified folders for files and folders, checking for any changes in file modification dates or sizes.
1. Detects changes and the type of synchronization that should be performed between folders.
1. Based on the detected changes, synchronizes files across all folders.
1. Updates its databases on disk, saving the latest file information for the next synchronization.
### Synchronization Types
SyncManager offers three distinct synchronization types, which determine how files are synchronized between your folders.
- **Two-way** - *The default type. It keeps all files and folders identical in two-way folders. If you add, delete, or change a file in one folder, the same change will happen in the other.*
- **One-way** - *A mirror image. It copies all files and folders from two-way folders. Any files in the one-way folder that don't exist in two-way folders will be deleted.*
- **One-way update** - *This mode is for simple updates. Files are copied only once from two-way folders to the one-way update folder. Unlike the one-way type, files that are deleted from two-way folders will not be deleted from the one-way update folder. This is sometimes useful, for example, for synchronizing only new photos from a camera's SD card to a designated folder.*
### Syncing Modes
You can control when SyncManager performs its synchronization using one of three modes:
- **Manual** - *This mode gives you complete control. SyncManager will only sync your files when you tell it to.*
- **Automatic (Adaptive)** - *This is a smart, automatic mode. SyncManager learns the average time it takes to synchronize your files and uses that to determine the next synchronization interval. This interval is multiplied by a user-defined frequency multiplier. The minimum interval is 1 second.*
- **Automatic (Fixed)** - *This is a simple, scheduled mode. SyncManager will synchronize your files at a specific, regular time interval that you set.*
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
### File Delta Copying
While standard file synchronization replaces a destination file with an entirely new copy of the source file, delta synchronization utilizes a more efficient block-level approach. It only synchronizes the specific blocks of data that were actually changed. Once the synchronization is complete, the destination file is overwritten, and older versions of that file cannot be recovered. This could be ideal for synchronizing large files, as it significantly reduces wear and tear on your storage drives.
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
When SyncManager needs to synchronize a file from one location to another but finds an older version of the file in the destination, it must delete the existing file first. You can choose how it handles these deletions:
- **Move files to Trash** - *(The file is sent to your computer's Recycle Bin or Trash, allowing you to recover it later. Note that this doesn't immediately free up storage space)*
- **Versioning** - *(The old file is saved in a special versioning folder. This is a great way to keep a history of your files)*
- **Delete Files Permanently** - *(The file is deleted immediately and cannot be recovered. This option frees up storage space right away)*  
### Versioning Formats and Locations
If you choose to use the versioning deletion mode, you can customize how and where your old files are stored.
#### Versioning Formats
- **File timestamp before extension** - *(A timestamp is added right before the file's extension. For example, document.txt becomes document_2025_08_08_08_15_412.txt)*
- **File timestamp after extension** - *(The timestamp is added after the file's extension. For example, document.txt becomes document.txt_2025_08_08_08_15_412.txt)*
- **Folder timestamp** - *(Old files are moved into a new folder named with a timestamp, while keeping their original filename. For example, document.txt would be moved to a folder named 2025_08_08_08_15_412/document.txt)*
- **Last version** - *(This option only keeps the most recently deleted version of a file. Any older versions are overwritten)*
#### Versioning Location
1. **Locally** - Old files are stored in a new folder, next to your synchronization folder. This new folder will have the same name as the sync synchronization folder, plus a special designated postfix.
2. **Custom Location** - You can specify a separate location for all your versioned files. SyncManager will then organize them into folders like [Custom location]/[Profile name].
### Filtering
Files can be filtered from synchronization using the following options:
- **Minimum file size**
- **Maximum file size**
- **Minimum size for a moved file** - *(The size at which files are allowed to be detected as moved or renamed)*
- **Minimum size for delta copying** - *(The size at which files start being delta copied. Files lower than that size will be deleted and fully recopied)*
- **Include** - *(Whitelist)*
- **Exclude** - *(Blacklist)*
### Database Location
SyncManager stores a database file to keep track of your files for future synchronizations. You can choose where this database is saved:
- **Locally** - *The database is saved on your computer in a dedicated application data folder. It keeps your synchronization data separate from your actual synchronized files.*
- **Decentralized** - *The database is stored inside each of your synchronization folders, within a special hidden folder named .SyncManager. This is useful if you need to move your entire synchronization setup to another computer, as the database files will travel with your folders.*
### Conflict Resolution
Sometimes, changes are made to the same file in two different locations before a synchronization. SyncManager has clear rules to resolve these conflicts:
- **Latest modification date wins:** If a file has been modified in both locations, SyncManager will synchronize the file with the latest modification date.
- **Modified files take precedence:** If a file is modified in one folder but deleted in the other, SyncManager will prioritize the modified file. The deletion will be ignored, and the modified file will be copied.
- **Folder content changes take precedence:** If a folder has had files added or removed in one location but was completely deleted in another, the folder with the updated content will be copied.
# Building
Requires Qt 6.9 or newer. Buildable with Qt Creator.

# License
SyncManager is licensed under the GPL-3.0 license, see LICENSE.txt for more information.



