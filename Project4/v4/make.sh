echo "making!"
rm cs1550
gcc -Wall cs1550.c -o cs1550 -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse -lfuse -pthread

