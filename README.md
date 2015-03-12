# MLV Filesystem (MLVFS)
MLVFS is a [Filesystem in UserSpace (FUSE)](http://en.wikipedia.org/wiki/Filesystem_in_Userspace) approach to interacting with Magic Lantern's MLV video files.  For discussion, see the [forum thread](http://www.magiclantern.fm/forum/index.php?topic=13152.0).

## Linux
Install FUSE in the manner appropriate for your distribution.
You can compile `mlvfs` from the command line using `make`.

    mlvfs <mount point> --mlv_dir=<directory with MLV files>

To unmount the filesystem:

    fusermount -u <mount point>

NOTE: The webgui does not work in background mode, which is the FUSE default. So to use the webgui, specify the -f switch

To get to the webgui go to http://localhost:8000/

### Command Line Options

#### FUSE options:

    -d   -o debug          enable debug output (implies -f) 
    -f                     foreground operation
    -s                     disable multi-threaded operation

#### MLVFS options:

    --port=%s              webgui port (default is 8000)
    --resolve-naming       use DaVinci Resolve / BMD file naming convention (.MLV folders will show up as clips in Resolve)
    --cs2x2                2x2 chroma smoothing (to remove focus pixels on certain camera models and other artifacts)
    --cs3x3                3x3 chroma smoothing
    --cs5x5                5x5 chroma smoothing
    --bad-pix              hot/cold/bad pixel correction
    --really-bad-pix       very aggressive bad pixel correction
    --stripes              fixes vertical banding in highlights (present on some 5D3 and 7D cameras)
    --dual-iso-preview     preview mode for dual-ISO (very fast, but not very goold quality)
    --dual-iso             Full-blown dual-ISO conversion (quite slow)
    --amaze-edge           Dual-ISO interpolation method: use a temporary demosaic step (AMaZE) followed by edge-directed interpolation (default)
    --mean23               Dual-ISO interpolation method: average the nearest 2 or 3 pixels of the same color from the Bayer grid (faster)
    --no-alias-map         disable alias map, used to fix aliasing in deep shadows
    --alias-map            enable alias map, used to fix aliasing in deep shadows
    --prefetch=%d          when a particular frame is requested, start processing the next x frames in other threads
    --fps=%f               override the frame rate in the MLV metadata (for timelapse or slowmo footage)

Use the webgui to modify any of these options while mlvfs is running.

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
