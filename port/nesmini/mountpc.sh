#!/bin/sh
# Run on the NES Mini (ssh root@169.254.1.1). Mounts the PC's D: share
# (made by share-d.ps1) at /media/pc over hakchi's USB network link.
# Usage: PCUSER=you PCPASS=secret sh mountpc.sh
# The PC's side of the USB link (Windows picks it; see share-d.ps1 output).
PC="${PC:-$(ip neigh | awk '/^169\.254\./{print $1; exit}')}"
MNT="${MNT:-/media/pc}"
modprobe cifs 2>/dev/null   # the hakchi kernel ships cifs.ko as a module
echo "kernel: $(uname -r), PC: $PC"
mkdir -p "$MNT"
if grep -qw cifs /proc/filesystems; then
    mount -t cifs "//$PC/D" "$MNT" -o "username=$PCUSER,password=$PCPASS,sec=ntlmssp,uid=0,gid=0,file_mode=0777,dir_mode=0777" \
        && { echo "mounted at $MNT"; exit 0; }
fi
echo "This kernel has no CIFS support, so it needs a kernel with CONFIG_CIFS=y."
exit 1
