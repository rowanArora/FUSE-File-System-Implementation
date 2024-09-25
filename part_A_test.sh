make clean
rm -f empty.disk
make
truncate -s 128M empty.disk
./mkfs.vsfs -i 512 empty.disk
/u/csc369h/fall/pub/a4/tools/fsck.mkfs -a -r -i 512 empty.disk