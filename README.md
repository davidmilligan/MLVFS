# MLV Filesystem (MLVFS)
Here is the beginning for a [Filesystem in UserSpace (FUSE)](http://en.wikipedia.org/wiki/Filesystem_in_Userspace) approach to interacting with Magic Lantern's MLV video files.  For discussion, here is the [forum thread](http://www.magiclantern.fm/forum/index.php?topic=13152.0).

## Linux
Install fuse in the manner appropriate for your distribution.
You can compile `mlvfs` from the command line using `make`.

    mlvfs <mount point> --mlv_dir=<directory with MLV files>

To unmount the filesystem:

    fusermount -u <mount point>

## OS X
Install [OSXFUSE](http://osxfuse.github.io/).
You can compile either using Xcode application or using the command-line tools.

    mlvfs <mount point> --mlv_dir=<directory with MLV files>

Unmounting the filesystem is done through the normal manner.
