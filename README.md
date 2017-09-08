Blue Coat FS Fuse Driver
========================

Build
-----

Requirements: fuse2 (tested on Arch Linux with 2.9.7)

$ make


Use
---

1. get a BCFS image file e.g. system1
2. create mount dir (e.g. mnt_bluecoat)
3. mount: ./bcfs_fuse --imagefile=system1 mnt_bluecoat
4. unmount: fusermount -u mnt_bluecoat
