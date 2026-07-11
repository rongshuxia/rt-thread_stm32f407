================================================================================
  STM32F407 以太网网络摄像头 — RT-Thread 工程说明
================================================================================

一、项目简介
--------------------------------------------------------------------------------

本工程基于 RT-Thread 实时操作系统，运行在正点原子探索者 STM32F407 开发板上，
实现一套以太网 JPEG 视频推流方案：

  - 通过 ATK-MC2640（OV2640）摄像头模块采集 320×240 JPEG 图像
  - 板端作为 TCP Server，向连接的 PC 客户端连续发送 JPEG 裸流
  - ILI9341 液晶屏运行 LVGL 待机界面，实时显示网络状态、IP 地址与推流状态
  - 支持 SEGGER RTT 日志输出，便于无串口干扰地调试

典型应用场景：局域网视频监控、嵌入式图像采集演示、RT-Thread + LVGL + LwIP 综合例程。


二、硬件平台
--------------------------------------------------------------------------------

  MCU       : STM32F407ZGT6（Cortex-M4，168 MHz，1 MB Flash，192 KB SRAM）
  开发板    : 正点原子探索者 F407
  摄像头    : ATK-MC2640 模块（OV2640 传感器，DCMI 接口采集 JPEG）
  显示屏    : ILI9341 TFT LCD（240×320，16-bit FSMC 并口）
  网络      : 板载以太网 PHY（RMII，静态 IP）
  调试日志  : SEGGER RTT（经 USB 虚拟串口或 J-Link 查看）

  分辨率    : 320 × 240（JPEG）
  视频端口  : TCP 8001（定义于 applications/eth_camera.h）


三、软件架构
--------------------------------------------------------------------------------

  ┌─────────────────────────────────────────────────────────────┐
  │                        main 线程                            │
  │  lv_port_init() → 等待网络就绪 → eth_camera_capture()       │
  └─────────────────────────────────────────────────────────────┘
         │                              │
         ▼                              ▼
  ┌──────────────┐              ┌──────────────────┐
  │  lvgl 线程   │              │  cam_srv 线程    │
  │  LVGL 渲染   │              │  TCP Server      │
  │  ILI9341 刷屏│              │  JPEG 采集与发送 │
  │  app_ui 界面 │              │  eth_camera.c    │
  └──────────────┘              └──────────────────┘
         │                              │
         ▼                              ▼
  ┌──────────────┐              ┌──────────────────┐
  │  lv_port.c   │              │  atk_mc2640 驱动 │
  │  ili9341.c   │              │  LwIP netconn    │
  └──────────────┘              └──────────────────┘

  主要技术栈：
    - RT-Thread 3.1（内核 + FinSH/MSH Shell）
    - LwIP 2.1.2（静态 IP，TCP 推流）
    - LVGL（独立 GUI 线程，弱符号 lv_user_app 由 app_ui.c 覆盖）
    - STM32 HAL + RT-Thread HAL 驱动框架
    - SEGGER RTT 日志（board/rtt/）


四、目录结构
--------------------------------------------------------------------------------

  rt-thread_stm32f407/
  ├── applications/          应用层代码
  │   ├── main.c             程序入口
  │   ├── eth_camera.c/h     以太网摄像头 TCP 服务
  │   ├── app_ui.c/h         LVGL 待机界面（网络/推流状态）
  │   └── net_throughput.c/h 网络吞吐率测试（可选）
  ├── board/                 板级支持包（BSP）
  │   ├── rtconfig.h         RT-Thread 功能与网络配置
  │   ├── board.c/h          时钟、引脚等板级初始化
  │   ├── ATK_MC2640/        OV2640 摄像头驱动
  │   ├── ILI9341/           LCD 显示驱动（FSMC）
  │   ├── lvgl/              LVGL 移植层（lv_port.c/h）
  │   ├── rtt/               SEGGER RTT 日志
  │   └── libraries/         STM32 HAL 与 RT-Thread HAL 驱动
  ├── components/            RT-Thread 组件（LwIP、FinSH、libc 等）
  ├── os/                    RT-Thread 内核源码
  ├── libcpu/                CPU 移植层（Cortex-M4）
  ├── project/               Keil MDK 工程
  │   └── project.uvprojx    主工程文件（目标：STM32F407ZGTx）
  ├── tool/                  PC 端配套工具
  │   ├── recv_camera_view.py  实时预览 JPEG 流
  │   └── netperf_pc.py        网络吞吐率测试
  ├── .clangd                Cursor/VSCode C 语言智能提示配置
  └── readme.txt             本文件


五、功能说明
--------------------------------------------------------------------------------

  1. 网络摄像头推流（eth_camera.c）
     - 板端监听 TCP 端口 8001，等待 PC 客户端连接
     - 客户端连接后初始化摄像头，循环采集 JPEG 帧并通过 TCP 发送
     - 协议：无自定义帧头，连续发送标准 JPEG 数据（SOI: FF D8 ~ EOI: FF D9）
     - 客户端断开后自动回到监听状态，等待下一次连接

  2. LVGL 待机界面（app_ui.c）
     - 显示设备名称、分辨率、端口号
     - 状态卡片实时反映三种状态：
         Offline  — 网线未连接或网络未就绪
         Ready    — 网络已连接，等待客户端
         Live     — 客户端已连接，正在推流
     - 状态变化时弹出 Toast 提示（联网、推流开始、断网等）
     - 界面每 500 ms 刷新一次

  3. 网络吞吐率测试（net_throughput.c，可选）
     - 在 main.c 中将 NETPERF_TEST_IN_MAIN 置 1 可启用
     - 板端作为 TCP Server（端口 5002），PC 端运行 netperf_pc.py 测速

  4. 日志系统（rtt_log）
     - 使用 RTT_LOG_I / RTT_LOG_W / RTT_LOG_E 宏输出日志
     - 通过 J-Link RTT Viewer 或 Ozone 查看，不占用 UART 资源


六、网络配置
--------------------------------------------------------------------------------

  默认静态 IP（board/rtconfig.h）：

    IP 地址  : 192.168.188.18
    子网掩码 : 255.255.255.0
    网关     : 192.168.188.1

  修改 IP 后需同步更新 PC 端工具中的地址（tool/recv_camera_view.py 的 HOST 变量）。

  确保 PC 与开发板在同一网段，防火墙放行 TCP 8001 端口。


七、编译与烧录
--------------------------------------------------------------------------------

  工具链  : Keil MDK-ARM V5（ARM Compiler 5/6）
  工程文件: project/project.uvprojx
  目标芯片: STM32F407ZGTx

  步骤：
    1. 用 Keil 打开 project/project.uvprojx
    2. 确认已安装 Keil STM32F4xx 器件包（Pack）
    3. 编译（F7）→ 下载（F8）到开发板
    4. 连接网线，上电启动

  PC 端串口 Shell（FinSH/MSH）仍可通过 UART1 使用（rtconfig.h 中配置）。


八、使用方法
--------------------------------------------------------------------------------

  1. 正常摄像头模式（默认）

     a) 烧录固件，连接网线，开发板上电
     b) 等待约 3 秒（main 中等待 LwIP 与 PHY 链路就绪）
     c) 液晶屏显示 "Ready" 状态及设备 IP
     d) PC 端安装依赖并运行预览工具：

          pip install pillow
          python tool/recv_camera_view.py

     e) 脚本连接 192.168.188.18:8001，弹窗实时显示 JPEG 画面
     f) 关闭窗口或 Ctrl+C 退出；板端自动回到监听状态

  2. 网络吞吐率测试（可选）

     a) 修改 applications/main.c：#define NETPERF_TEST_IN_MAIN 1
     b) 重新编译烧录
     c) PC 端运行：

          python tool/netperf_pc.py

     d) 默认向板子 192.168.188.18:5002 发送数据并统计速率


九、关键源文件速查
--------------------------------------------------------------------------------

  文件                          说明
  ─────────────────────────────────────────────────────────────
  applications/main.c           启动流程：LVGL → 等待网络 → 摄像头
  applications/eth_camera.c     TCP 推流服务线程 cam_srv
  applications/app_ui.c         LVGL 用户界面（覆盖 lv_user_app）
  applications/eth_camera.h       JPEG 分辨率、缓冲区大小、TCP 端口
  board/lvgl/lv_port.c            LVGL 线程、显示缓冲、tick 源
  board/ATK_MC2640/atk_mc2640.c   OV2640 初始化与 DCMI 帧采集
  board/ILI9341/ili9341.c         FSMC 驱动与区域刷新
  board/rtconfig.h                内核、LwIP、BSP 开关与 IP 配置
  tool/recv_camera_view.py        PC 端 JPEG 实时预览


十、IDE 开发提示
--------------------------------------------------------------------------------

  本工程已配置 .clangd 与 .vscode/c_cpp_properties.json，
  可在 Cursor / VSCode 中获得代码跳转与补全支持。

  主要 include 路径和宏定义与 Keil 工程保持一致（ARM Cortex-M4，
  STM32F407xx，USE_HAL_DRIVER 等）。修改 rtconfig.h 或工程配置后，
  建议同步更新 .clangd 中的编译参数。


十一、常见问题
--------------------------------------------------------------------------------

  Q: PC 连不上摄像头？
  A: 检查网线、IP 是否同网段、板子界面是否显示 Ready/Live、
     防火墙是否拦截 8001 端口。

  Q: 连接成功但无画面？
  A: 确认摄像头模块接插正确；查看 RTT 日志是否有 JPEG 帧大小输出；
     recv_camera_view.py 会提示 "waiting for JPEG frame"。

  Q: 液晶屏无显示？
  A: 检查 ILI9341 排线、背光（PF10）；lvgl 线程是否正常启动。

  Q: 如何修改视频分辨率或端口？
  A: 修改 applications/eth_camera.h 中的 JPEG_WIDTH_320、
     JPEG_HEIGHT_240、ETH_PORT，并同步更新 PC 端工具配置。


================================================================================
  License: 基于 RT-Thread（Apache-2.0）及正点原子开源驱动，仅供学习参考。
================================================================================
