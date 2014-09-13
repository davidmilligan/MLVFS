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
You can compile either using the Xcode application or using the command-line tools.

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
