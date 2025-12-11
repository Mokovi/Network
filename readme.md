# Linux网络编程知识点总结

[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.15+-green.svg)](https://cmake.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)](https://www.kernel.org/)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()

> **从基础到进阶全覆盖的Linux网络编程知识点总结项目**  
> 使用POSIX API编写，提供完整的网络编程学习路径和实践示例。

## 🏗️ 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    Linux网络编程知识体系                      │
├─────────────────────────────────────────────────────────────┤
│  📋 网络通信基础层                                           │
│  ├── OSI七层模型 & TCP/IP四层模型                           │
│  ├── 大小端序转换 (htonl/ntohl)                            │
│  ├── IP地址处理 (inet_pton/inet_ntop)                      │
│  └── 网络字节序与主机字节序                                 │
├─────────────────────────────────────────────────────────────┤
│  🔌 Socket编程层                                            │
│  ├── TCP通信实现                                            │
│  │   ├── 服务器端 (bind/listen/accept)                     │
│  │   ├── 客户端 (connect/send/recv)                        │
│  │   └── 双向通信 & 短写处理                               │
│  └── UDP通信实现                                            │
│      ├── 无连接通信 (sendto/recvfrom)                      │
│      ├── 地址记录与对等通信                                 │
│      └── 数据报边界处理                                     │
├─────────────────────────────────────────────────────────────┤
│  ⚡ I/O模型层                                               │
│  ├── 阻塞I/O (Blocking I/O)                                │
│  ├── 非阻塞I/O (Non-blocking I/O)                          │
│  ├── I/O多路复用 (select/poll/epoll)                       │
│  └── 异步I/O (aio_read/aio_write)                          │
├─────────────────────────────────────────────────────────────┤
│  🏢 服务器模型层                                            │
│  ├── 多进程模型 (fork/exec)                                │
│  ├── 多线程模型 (pthread)                                  │
│  ├── 事件驱动模型 (Reactor/Proactor)                       │
│  └── 混合模型 (进程池/线程池)                               │
└─────────────────────────────────────────────────────────────┘
```

## 🚀 快速开始

### 环境要求

| 组件 | 版本要求 | 说明 |
|------|----------|------|
| **操作系统** | Linux 2.6+ | 支持POSIX系统调用 |
| **编译器** | GCC 7.0+ | 支持C++17标准 |
| **CMake** | 3.15+ | 构建系统管理 |
| **线程库** | pthread | POSIX线程支持 |

### 安装步骤

1. **克隆项目**
   ```bash
   git clone <repository-url>
   cd network
   ```

2. **创建构建目录**
   ```bash
   mkdir build && cd build
   ```

3. **配置CMake**
   ```bash
   cmake ..
   ```

4. **编译项目**
   ```bash
   make -j$(nproc)
   ```

5. **运行示例**
   ```bash
   # 运行基础网络函数示例
   ./bin/app
   
   # 运行TCP通信示例
   ./bin/server_tcp    # 终端1
   ./bin/client_tcp    # 终端2
   ```

## 📄 许可证信息

本项目采用 [MIT许可证](LICENSE) 开源协议。

```
MIT License

Copyright (c) 2024 Linux网络编程项目

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
```

## 🙏 致谢和联系

### 项目状态
- **当前版本**：v1.0.0
- **维护状态**：积极维护中
- **最后更新**：2025年12月10日

### 联系方式
- **项目主页**：[GitHub Repository]
- **问题反馈**：[Issues页面]
- **技术讨论**：[Discussions页面]

### 特别感谢
感谢所有为Linux网络编程技术发展做出贡献的开发者们，以及提供优秀学习资源的开源社区。

---

> **💡 提示**：本项目适合Linux网络编程初学者到进阶开发者使用，建议按照文档顺序逐步学习，每个示例都可以独立运行和调试。
