make clean
make PLATFORM=generic FW_FDT_PATH=/home/guokai/ELF/nemu_board/dts/build/xiangshan.dtb TYCHE_SM_PATH=/mnt/ssd4t/guokai/tyche FW_PAYLOAD=y FW_PAYLOAD_PATH=/mnt/ssd4t/guokai/Image-qemu CROSS_COMPILE=riscv64-linux-gnu- -j $(nproc)
cd ..
