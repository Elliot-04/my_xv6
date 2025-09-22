FROM archlinux:latest

RUN \
    pacman -Syyu --noconfirm && \
    \
    pacman -S --noconfirm \
        gcc \
        make \
        perl \
        wget \
        git \
        python \
        python-pygments \
        riscv64-elf-gdb \
        riscv64-elf-gcc \
        riscv64-elf-binutils \
        riscv64-elf-newlib \
        qemu-emulators-full \
        xxhash && \
    \
    pacman -Scc --noconfirm

RUN \
    wget -O ~/.gdbinit https://gitee.com/ftutorials/gdb-dashboard/raw/master/.gdbinit && \
    echo "set auto-load safe-path /" >> ~/.gdbinit
