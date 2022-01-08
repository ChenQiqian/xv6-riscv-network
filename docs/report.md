# xv6-riscv 改进：网络系统

## E1000 部分

E1000 是 QEMU 模拟出来的网卡，具体来说对应着 https://pdos.csail.mit.edu/6.828/2021/readings/8254x_GBe_SDM.pdf 中的 82540EM 网卡。

以下是这个网卡的结构图：

![](82540EM.jpg)

https://www.intel.cn/content/www/cn/zh/support/articles/000005480/ethernet-products.html


https://github.com/torvalds/linux/tree/master/drivers/net/ethernet/intel/e1000

### E1000 网卡的初始化

首先，E1000 网卡是 PCI 设备，通过 PCI 的主线与其他设备以及 CPU 等相连接。因此，初始化的第一步是在 PCI 总线上寻找 E1000 网卡。

PCI 总线的结构是：8位地址、3位功能、5位设备，我们遍历总线上连接的所有设备，通过判断偏移位置最开始的 32 位存储的内容来判断这个设备的编号，`[100e:8086]` 就是 E1000 网卡的识别号。设置了基本的寄存器初始值后，转入 `e1000.c` 进行初始化