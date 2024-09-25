make clean

rm -f /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-empty.disk
rm -f /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-empty2.disk
rm -f /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-maxfs.disk
rm -f /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-1file.disk
rm -f /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-3file.disk
rm -f /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-4file.disk
rm -f /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-42file.disk
rm -f /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-many.disk

cp /u/csc369h/fall/pub/a4/images/vsfs-empty.disk /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-empty.disk
cp /u/csc369h/fall/pub/a4/images/vsfs-empty2.disk /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-empty2.disk
cp /u/csc369h/fall/pub/a4/images/vsfs-maxfs.disk /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-maxfs.disk
cp /u/csc369h/fall/pub/a4/images/vsfs-1file.disk /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-1file.disk
cp /u/csc369h/fall/pub/a4/images/vsfs-3file.disk /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-3file.disk
cp /u/csc369h/fall/pub/a4/images/vsfs-4file.disk /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-4file.disk
cp /u/csc369h/fall/pub/a4/images/vsfs-42file.disk /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-42file.disk
cp /u/csc369h/fall/pub/a4/images/vsfs-many.disk /h/u11/c1/00/aroraa39/csc369/aroraa39/A4/vsfs-many.disk

make

# /u/csc369h/fall/pub/tester/bin/a4-tester-run.sh

# for ((i = 0; i <= 127; ++i)); do touch /tmp/aroraa39/file.$i; done

# gdb --args ./vsfs vsfs-empty.disk /tmp/aroraa39 -d
# ./vsfs vsfs-empty.disk /tmp/aroraa39 -d
# /u/csc369h/fall/pub/a4/tools/fsck.vsfs -i 64 vsfs-empty.disk -v

# gdb --args ./vsfs vsfs-empty2.disk /tmp/aroraa39 -d
# ./vsfs vsfs-empty2.disk /tmp/aroraa39 -d
# /u/csc369h/fall/pub/a4/tools/fsck.vsfs -i 256 vsfs-empty2.disk

# gdb --args ./vsfs vsfs-maxfs.disk /tmp/aroraa39 -d
# ./vsfs vsfs-maxfs.disk /tmp/aroraa39 -d
# /u/csc369h/fall/pub/a4/tools/fsck.vsfs -i 512 vsfs-maxfs.disk

# gdb --args ./vsfs vsfs-1file.disk /tmp/aroraa39 -d
# ./vsfs vsfs-1file.disk /tmp/aroraa39 -d
# /u/csc369h/fall/pub/a4/tools/fsck.vsfs -i 64 vsfs-1file.disk

# gdb --args ./vsfs vsfs-3file.disk /tmp/aroraa39 -d
# ./vsfs vsfs-3file.disk /tmp/aroraa39 -d
# /u/csc369h/fall/pub/a4/tools/fsck.vsfs -i 128 vsfs-3file.disk

gdb --args ./vsfs vsfs-4file.disk /tmp/aroraa39 -d
# ./vsfs vsfs-4file.disk /tmp/aroraa39 -d
/u/csc369h/fall/pub/a4/tools/fsck.vsfs -i 128 vsfs-4file.disk -v

# gdb --args ./vsfs vsfs-42file.disk /tmp/aroraa39 -d
# ./vsfs vsfs-42file.disk /tmp/aroraa39 -d
# /u/csc369h/fall/pub/a4/tools/fsck.vsfs -i 64 vsfs-many.disk

# gdb --args ./vsfs vsfs-many.disk /tmp/aroraa39 -d
# ./vsfs vsfs-many.disk /tmp/aroraa39 -d
# /u/csc369h/fall/pub/a4/tools/fsck.vsfs -i 256 vsfs-many.disk