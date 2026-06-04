# 服务器连接测试与配置报告

> 生成日期：2026-05-27
> 最后更新：2026-05-27（更换服务器：从 220.205.16.24 迁移至 103.236.63.60）

---

## 1. 连接信息

| 项目 | 值 |
|------|-----|
| 主机地址 | 103.236.63.60 |
| SSH 端口 | 22 |
| 用户 | root |
| 密码 | `Abc49598488` |
| SSH Key 路径 | `c:\Users\Administrator\Documents\trae_projects\Int32_search_algorithm\.ssh\id_rsa` |
| SSH Key 指纹 | `ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDOWv...` |
| 密码连接状态 | **成功** |
| SSH Key 连接状态 | **成功** |
| SSH Config 别名 | `int32-search-server` |

### 1.1 SSH Config 配置（已写入 `.ssh\config`）

```ini
Host int32-search-server
    HostName 103.236.63.60
    Port 22
    User root
    IdentityFile "C:\Users\Administrator\Documents\trae_projects\Int32_search_algorithm\.ssh\id_rsa"
```

> 使用方式：`ssh int32-search-server`（无需密码）

### 1.2 公钥内容（已部署到服务器 `~/.ssh/authorized_keys`）

```
ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDOWvmdtugYeWx6cTCgtBooc3LL+M0kqKKAwfKweQzseJUN5vqM/ww+3KbO360kl1bVPAWtCerpteR7WZpRyjHsofavkeM8cCLZTrJjOEqeeMFnostOvEdM0ZfacwtMXov9AwtW7iTepaeBpKjN4c9xW1/L/k4I9Io2NRLTO0k+rvJ7pKEx27DuVgLwurIMJbmDLhst8TTIoMhkoLQIc+7RlKe8neJKzqOEmry8561u3WXVYwZh/kwqP39IAyTEMkQyHGEfWpqEbIi6U02vExREi1Xok+DWf5qPYJ+zosiwEdc9yU4hHLST4CLmCFP+L06oYJQnrdRbwQ+N194g+3hr administrator@00cdfffh-ENyefQ
```

---

## 2. 服务器硬件配置

### 2.1 基本系统信息

| 项目 | 值 |
|------|-----|
| 主机名 | ser822174119203 |
| 操作系统 | Ubuntu 22.04 LTS (Jammy Jellyfish) |
| 内核版本 | Linux 5.15.0-30-generic |
| 架构 | x86_64 (64-bit) |
| 虚拟化 | KVM 虚拟机 |

### 2.2 CPU 信息

| 项目 | 值 |
|------|-----|
| CPU 型号 | Intel Xeon Gold 6152 @ 2.10GHz |
| 物理核心数 | 16 |
| 每核心线程数 | 1 |
| 总逻辑 CPU | 16 |
| L2 缓存 | 8 MiB (8 实例) |
| L3 缓存 | 30.3 MiB (1 实例) |
| NUMA 节点 | 1 |

### 2.3 内存信息

| 项目 | 值 |
|------|-----|
| 总内存 | 15 GiB |
| 已用 | 238 MiB |
| 可用 | 15 GiB |
| Swap | 0 B (无交换分区) |

### 2.4 磁盘信息

| 项目 | 值 |
|------|-----|
| 根分区 (/) | 97 GB 总容量，1.5 GB 已用，96 GB 可用 |
| 磁盘类型 | /dev/vda1 (虚拟磁盘) |

---

## 3. SIMD 指令集支持

| 指令集 | 支持状态 |
|--------|----------|
| SSE4.2 | **支持** |
| AVX | **支持** |
| AVX2 | **支持** |
| AVX-512F | **支持** |
| AVX-512CD | **支持** |
| AVX-512BW | **支持** |
| AVX-512DQ | **支持** |
| AVX-512VL | **支持** |

---

## 4. 开发环境检查

### 4.1 已安装工具

| 工具 | 版本 | 状态 | 说明 |
|------|------|------|------|
| **GCC** | 11.4.0 (Ubuntu) | ✅ | C11 标准支持，含 AddressSanitizer/UBSan |
| **G++** | 11.4.0 (Ubuntu) | ✅ | C++ 兼容（如有需要） |
| **GNU Make** | 4.3 | ✅ | 项目主力构建系统 |
| **Git** | 2.34.1 | ✅ | 版本控制 |
| **CMake** | 3.22.1 | ✅ | 辅助构建系统 |
| **GDB** | 12.1 (Ubuntu) | ✅ | 调试器 |
| **Valgrind** | 3.18.1 | ✅ | 内存泄漏检测 |
| **Python 3** | 3.10.4 | ✅ | 辅助脚本 |
| **OpenSSL** | 3.0.2 | ✅ | 加密库 |

> perf：已安装但内核版本(5.15.0-30)与工具包版本(5.15.0-179)不匹配，无法直接使用。perf 为辅助分析工具，不影响项目开发。

### 4.2 编译验证结果

| 测试项 | 命令 | 结果 |
|--------|------|------|
| AVX2 编译 | `gcc -O3 -std=c11 -mavx2` | ✅ `AVX2 OK: 0` 输出正常 |
| Sanitizer 编译 | `gcc -O3 -std=c11 -fsanitize=address,undefined` | ✅ 编译运行正常 |

### 4.3 项目环境适配检查

基于项目文档 [docs/requirements/总需求文档.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/requirements/总需求文档.md) 和 [docs/architecture/技术路线.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/architecture/技术路线.md) 的要求：

| 需求编号 | 需求内容 | 检查结果 |
|----------|----------|----------|
| CR-01 | GCC ≥ 8.0 | ✅ **11.4.0 ≥ 8.0** |
| CR-02 | Linux 平台 | ✅ **Ubuntu 22.04** |
| CR-03 | C11 标准 | ✅ `-std=c11` 编译通过 |
| CR-04 | AVX2 指令集 | ✅ **CPU 支持 AVX2，编译测试通过** |
| ER-01 | Makefile + CMake | ✅ **make 4.3 + cmake 3.22.1** |
| SR-05 | ASan/UBSan 零告警 | ✅ **GCC 11.4.0 内置 sanitizer 支持** |

---

## 5. 已安装的工具（本次新增）

| 工具 | 安装方式 | 用途 |
|------|----------|------|
| GCC 11.4.0 | `apt install gcc g++` | C 编译器 (CR-01) |
| GNU Make 4.3 | `apt install make` | 主力构建系统 (ER-01) |
| CMake 3.22.1 | `apt install cmake` | 辅助构建系统 (ER-01) |
| GDB 12.1 | `apt install gdb` | 调试支持 |
| Valgrind 3.18.1 | `apt install valgrind` | 内存泄漏检测 (SR-04) |

---

## 6. 新旧服务器对比

| 指标 | 旧服务器 (220.205.16.24) | 新服务器 (103.236.63.60) | 提升 |
|------|--------------------------|--------------------------|------|
| CPU 核心 | 4 | **16** | **4x** |
| 内存 | 3.7 GB | **15 GB** | **4x** |
| 磁盘 | 26 GB | **97 GB** | **3.7x** |
| 操作系统 | Ubuntu 20.04 (GCC 9.4) | **Ubuntu 22.04 (GCC 11.4)** | 升级 |
| SIMD | AVX2 + AVX-512 | AVX2 + AVX-512 | 一致 |
| SSH 端口 | 62069 | **22 (标准端口)** | 更便捷 |

---

## 7. 后续工作建议

1. **代码同步**：将本地 `src/` 目录代码同步到服务器后进行编译测试
2. **16 核并行编译**：`make -j16` 可充分利用多核优势
3. **内存充足**：15GB 内存可轻松应对 1000 万 int32 数据及 benchmark 运行
4. **perf 修复**：如需使用 perf，可重启到新内核或另行安装匹配版本

---

*本文档由 AI 助手自动生成于连接测试完成后*