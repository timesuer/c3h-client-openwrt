c3h-client
===========

**这是一个为 OpenWrt 深度优化的 802.1X (iNode) 客户端**。
在前人的基础上，本次更新重点优化了路由器上的运行体验，实现了**完全无人值守**的拨号与网络维护。

编译相关的说明文件请点击[BUILD.md](https://github.com/mcdona1d/c3h-client/blob/master/BUILD.md)查看

最新特性 (Enhanced)
-----
* **一键认证与自动 DHCP**：剥离了原本依赖的外部 `c3h-refresh` 脚本，在 802.1X 认证通过后，客户端会自动调用 `udhcpc` 获取 IP，不再需要分成两步。
* **全天候网络守护 (Network Monitor)**：程序在后台会每隔 30 分钟默默 `ping` 检测网络（百度服务器），一旦发现网络中断，会自动触发重新认证与 DHCP 续租流程。
* **开机自启无感知**：专门去除了 `stdout` 缓冲阻塞，优化了后台作为守护进程运行的表现。
* **原生 ARM 静态编译支持**：新增了 Dockerfile 方案，提供无需依赖 `libpcap.so` 的纯静态链接编译方案，生成的二进制文件极小巧，且不会报错 "missing libpcap"。

继承的原有特性
-----
* 基于 njit8021xclient 代码进行修改
* 基于 iNode V7.00-0102 版本的 EAP 报文分析进行修改
* 集成 MD5 算法，不需要再依赖 openssl
* 增加断线重连机制

用法与后台启动
-----
```bash
# 测试启动
c3h-client [username] [password] [adapter] [reconnect]

# 示例：
c3h-client admin 123456 eth0 5
```
参数说明：
* `[Username]` 用户名
* `[password]` 密码
* `[adapter]` 认证网卡（如 `eth0`、`eth1`）
* `[reconnect]` 认证失败后的重连最大次数。参数为 0 时禁用重连。

### 如何设置开机自启并后台守护

在您的 OpenWrt 路由器中，编辑 `/etc/rc.local` 文件，在 `exit 0` 之前插入以下命令（将账号密码换成您自己的）：

```bash
/usr/bin/c3h-client username password eth0 5 > /tmp/c3h-client.log 2>&1 &
```

重启路由器后，程序会自动后台驻留，您可以随时通过 `cat /tmp/c3h-client.log` 监控它为您自动拨号和断网重连的日志。

鸣谢与历史信息
---------
该项目代码源于以下项目：
njit8021xclient: https://github.com/liuqun/njit8021xclient
以及 bitdust 的 fork: https://github.com/bitdust/njit8021xclient
感谢 KiritoA 的付出与努力。

License
---------
本项目继承并遵循 GPLv3 协议。