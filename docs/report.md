# xv6-riscv 改进：网络系统

## E1000 部分

E1000 是 QEMU 模拟出来的网卡，具体来说对应着 https://pdos.csail.mit.edu/6.828/2021/readings/8254x_GBe_SDM.pdf 中的 82540EM 网卡。

以下是这个网卡的结构图：

![](82540EM.jpg)

https://www.intel.cn/content/www/cn/zh/support/articles/000005480/ethernet-products.html


https://github.com/torvalds/linux/tree/master/drivers/net/ethernet/intel/e1000
