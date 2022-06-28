# CS144

This is my code for studying CS144.

## Environment setup

The original [environment install instruction](https://stanford.edu/class/cs144/vm_howto/vm-howto-image.html) uses virtualbox.
However, I would like to use kvm in ArchLinux

```sh
aria2c "https://mirrors.tuna.tsinghua.edu.cn/ubuntu-releases/bionic/ubuntu-18.04.6-live-server-amd64.iso"
```

Next, use qemu to create a new virtual machine.

In the Ubuntu server:

```sh
### update sources and get add-apt-repository
apt-get update
apt-get -y install software-properties-common

### add the extended source repos
add-apt-repository multiverse
add-apt-repository universe
add-apt-repository restricted

### make sure we're totally up-to-date now
apt-get update
apt-get -y dist-upgrade

### install the software we need for the VM and build env
apt-get -y install build-essential gcc gcc-8 g++ g++-8 cmake libpcap-dev htop jnettop screen   \
                   emacs-nox vim-nox automake pkg-config libtool libtool-bin git tig links     \
                   parallel iptables mahimahi mininet net-tools tcpdump wireshark telnet socat \
                   clang clang-format clang-tidy clang-tools coreutils bash doxygen graphviz   \
                   virtualbox-guest-utils netcat-openbsd

## make a sane set of alternatives for gcc, and make gcc-8 the default
# GCC
update-alternatives --remove-all gcc &>/dev/null
for ver in 7 8; do
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-${ver} $((10 * ${ver})) \
        $(for prog in g++ gcc-ar gcc-nm gcc-ranlib gcov gcov-dump gcov-tool; do
            echo "--slave /usr/bin/${prog} ${prog} /usr/bin/${prog}-${ver}"
        done)
done
```

Then, use VsCode `Remote-SSH`.
