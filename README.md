# Introduction

**SyncManager** is a custom-made lightweight synchronization manager that helps synchronize files and folders across different locations. The utility is primarily designed to run in the background in the system tray, silently launched at system startup, to automatically synchronize files at maximum speed and use as little memory as possible.

<img width="49%" height="auto" alt="SyncManager" src="https://github.com/user-attachments/assets/38b46008-c0dc-4a8c-93d1-467935cf6d3c" />
<img width="49%" height="auto" alt="SyncManager_2 3" src="https://github.com/user-attachments/assets/83d4dd12-540a-4eaa-bddd-f4005a4e670c" />
<br><br>

| Features | Features |
| --- | --- |
| Manual and automatic synchronization | Two-way, one-way, and one-way update synchronization types |
| Can synchronize multiple folders | Can move files to the trash |
| Can delete files permanently | Can version all deletions |
| Can detect renamed and moved files | Filtering by name or file size |
| Works in the system tray | Launches on startup |
| Easy to set up | Fast synchronization |
| Fast synchronization | Lightweight and portable |
| Keeps memory usage as low as possible | Can detect corrupted files |

# Why This Was Created
Files are priceless and can easily be lost due to disk failure, sudden file corruption, or other causes. Yes, it is possible to store backups on some cloud services, but there are restrictions on how many files you can store, and there's no guarantee those files won't get modified or deleted. So, the only solution here is to take care of business yourself by storing files on several disks and constantly synchronizing them.

Unfortunately, there was no open-source software application that  could meet all my requirements. Although there are some interesting tools out there on the internet, all of them had cons and lacked something:
- Not free or not fully open source
- Requires attention and interaction from you
- Cannot synchronize more than two folders
- No delta copying for large files
- Cannot detect renamed or moved files to avoid recopying
- Doesn't run in the background and synchronize files automatically
- Cannot detect corrupted files and notify the user to fix the disk
- No GUI, or the GUI is complicated and hard to understand
- Uses more memory than needed, and synchronization takes a long time

# Documentation
For detailed information on how it works or how to use it, please see the documentation [here](Docs/README.md).

# Dependencies
- [xxHash](https://github.com/cyan4973/xxhash)

# Building
Requires Qt 6.9 or newer. Buildable with Qt Creator.

# License
SyncManager is licensed under the GPL-3.0 license, see LICENSE.txt for more information.
