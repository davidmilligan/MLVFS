# MLV Filesystem (MLVFS)
Here is the beginning for a [Filesystem in UserSpace (FUSE)](http://en.wikipedia.org/wiki/Filesystem_in_Userspace) approach to interacting with Magic Lantern's MLV video files.  For discussion, here is the [forum thread](http://www.magiclantern.fm/forum/index.php?topic=13152.0).

## Linux
Install FUSE in the manner appropriate for your distribution.
You can compile `mlvfs` from the command line using `make`.

    mlvfs <mount point> --mlv_dir=<directory with MLV files>

To unmount the filesystem:

    fusermount -u <mount point>

## OS X
Install [OSXFUSE](http://osxfuse.github.io/).
Double click the MLVFS.workflow and select “Install” when prompted.

To Use:
1. Right-Click a folder containing MLV files in the finder and select "Services" > "MLVFS"
2. You will be prompted to select the mount point
3. Select an empty folder for the mount point
4. The MLV files in the source directory will now show up as folders containing DNGs in the mount directory
5. When finished you can unmount the directory by clicking the eject button that shows up next to it in the Finder


You can compile from source using either the Xcode application or using the command-line tools.

    mlvfs <mount point> --mlv_dir=<directory with MLV files>

Unmounting the filesystem is done through the typical methods.

## Windows
There is no FUSE solution for Windows, but an alternative approach that
functions in much the same way is
[Pismo File Mount Audit Package](http://www.pismotechnic.com/pfm/ap/).
Download and install PFMAP, and then register the MLV formatter DLL:

    pfm register mlvfs.dll

Unlike the FUSE approach on the other platforms, you mount each MLV file
individually: right-click on an MLV file and select "Quick Mount".

If you wish, you can build the MLV formatter DLL yourself by installing the
Windows SDK and running `nmake` using the SDK command prompt in the `mlvfs/win`
directory.
