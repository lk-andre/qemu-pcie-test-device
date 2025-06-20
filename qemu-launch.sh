#!/bin/bash

set -e

BASEDIR=$(dirname $0)
SUBMODULE_PATH="external/qemu"
DEBUG_PARAM=()
MONITOR_PORT=7777

MACHINE="q35"
INIT_RD=$BASEDIR/initrd.img-6.1.0-34-amd64
KERNEL=$BASEDIR/vmlinuz-6.1.0-34-amd64
QCOW2=$BASEDIR/qemu.qcow2

while getopts ":d" opt; do
  case ${opt} in
    d )
      echo "----------------------------------------------------"
      echo "Connect to monitor @ localhost:$MONITOR_PORT to manually start"
      echo "----------------------------------------------------"
      DEBUG_PARAM+=(-monitor tcp:localhost:$MONITOR_PORT,server)
      DEBUG_OARAM+=(-s -S)
      ;;
    \? )
      echo "Usage: $0 [-d]"
      exit 1
      ;;
  esac
done

"./$SUBMODULE_PATH/build/qemu-system-x86_64" -machine "$MACHINE" -m 2G -kernel "$KERNEL" -initrd "$INIT_RD" -append "rootwait root=/dev/vda1 console=ttyS0" -drive file="$QCOW2",if=virtio,media=disk -nographic -enable-kvm -virtfs local,path="$BASEDIR",mount_tag=shared0,security_model=passthrough,id=share0 -device pcie-test-device "${DEBUG_PARAM[@]}"

