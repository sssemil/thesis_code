#!/bin/bash

set -e
set -x

sudo apt update
sudo apt install -y cmake make gcc g++ liburing-dev

exit
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge
echo 1024 | sudo tee /proc/sys/vm/nr_hugepages
echo 'vm.nr_hugepages=1024' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p

ip a
sudo ip link set ens6 down
ip a

sudo apt-get update
sudo apt-get install -y git gcc openssl libssl-dev libnuma-dev make meson python3-pip python3-pyelftools zsh

#git clone https://github.com/DPDK/dpdk.git --depth 1

git clone git://dpdk.org/dpdk --depth 1
git clone http://dpdk.org/git/dpdk-kmods --depth 1

#wget https://fast.dpdk.org/rel/dpdk-23.11.1.tar.xz
#tar -xf dpdk-23.11.1.tar.xz
#mv dpdk-stable-23.11.1 dpdk

sudo apt-get install -y dpdk-dev libdpdk-dev dpdk
#cd dpdk
#meson setup -Dplatform=native build
#cd build
#ninja

cd ~/dpdk-kmods/linux/igb_uio
make
sudo modprobe uio
sudo insmod igb_uio.ko

lsmod | grep igb_uio

cd ~/
sudo ./dpdk/usertools/dpdk-devbind.py --status
sudo ip link set ens6 down
sudo ./dpdk/usertools/dpdk-devbind.py --bind=igb_uio 00:06.0
sudo ./dpdk/usertools/dpdk-devbind.py --status

echo "DPDK setup is complete."

cd ~/dpdk/examples/helloworld
make
sudo ./build/helloworld -l 0 -n 1
