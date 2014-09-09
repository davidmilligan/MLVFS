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
Unfortunately, there is no FUSE solution for Windows.  For the time being, it is possible to use a Linux VM to serve the MLVFS filesystem to a Windows host, although it will be inefficient.  Here are the steps:

In the Linux VM:

1. Make sure that the Linux VM is networked with the Windows host
2. Configure Samba to share the local folder that will be used as the MLVFS mount point
3. Configure FUSE to allow other users (namely `smbd`) to use your mount points by editing `/etc/fuse.conf` (requires `sudo`) and uncommenting the line `user_allow_other` (and also make sure that `fuse.conf` is world-readable)
4. Run the MLVFS mount command with the additional command-line option `-o allow_other`, e.g.:

    mlvfs <mount point> --mlv_dir=<directory with MLV files> -o allow_other

In the Windows host, connect to the shared folder, e.g.:

    \\<Linux hostname>\<mount point share>
