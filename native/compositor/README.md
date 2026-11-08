# S6 Falcon 共享内存显示后端

PenChat 的 S6/ARM32 目标使用这一后端；Rockchip/AArch64 继续使用 DRM。
LVGL 页面、字体、键盘和原生 evdev 输入共用原实现。本目录不调用 CVI VO、
不打开 framebuffer，不替换设备节点，不执行 shell。

## 输出与所有权

Falcon 原 `post_fb` 源图 → 两个共享系统槽 → PenChat 原生进程后台 BGRA 合成
→ 两个共享输出槽 → Falcon 适配器串行调用原 `post_fb`。
系统预乘 Alpha 图在上，原生 LVGL 图在下；保留系统 `<hole>` 及覆盖层。
几何由 Falcon 配置解析，显示旋转仍仅由原提交路径处理一次。

`Composite` 位于启动桥接，动态查找唯一已解析 ELF PLT 槽。只支持已核验
S6 Cv1826/Plum 与 libfalcon 完整 SHA-256；未知固件拒绝启动，不能绕过。
借用原提交调用槽需要短暂恢复 GOT 页可写权限，随后恢复原权限；不修改磁盘库、
指令或固定地址。其他所有者已接入时拒绝覆盖。

`Surface` 位于独立 PenChat 原生应用，复用 `DisplaySurface` 接口。它保留一份
最新 CPU 帧，两个输出槽由确认消息控制复用；渲染、系统帧和焦点代次均未变化时
不复制或提交。后台线程仅合成像素，所有 LVGL 操作仍在 UI 线程。
最高约30fps；共享输出被占用时合并为最新状态，不堆积逐帧队列。

固定40字节 SEQPACKET 消息检查版本、类型、序号和槽号；memfd 限制伸缩，
两侧只映射自己写入的区域为可写。生产启动通过 posix_spawn 仅传递 compositor
fd3、共享帧fd4和业务桥接fd5；标准输出重定向私有日志，其他继承FD关闭。

## 生命周期

- 现有 `$am.hasWindowFocus` 的 input.focus 心跳同时控制合成焦点；300ms过期
  即退回系统画面。输入仍由现有 NativeTouch/InputFocus 管理，不由合成线程读触摸。
- 控制中心、官方键盘和后台撤销合成；返回时通过新焦点代次重送最新画面。
  原生 Surface 隐藏不销毁 IPC，也不要求下次调用 LVGL 才能处理系统帧。
- 生产会话没有原型25秒期限，按应用/IPC生命周期结束。测试入口保留有界租约。
- S6 主应用不持有 VO/ION 显示资源，扫描 mediaHandoff 保留共享显示；采集进程
  及模型仍独立持有原有 CVI 资源。其他 DisplaySurface 的移交行为不变。
- IPC断开、非法帧或原生退出停止合成并恢复原函数。启动桥接负责回收应用进程，
  合成器只拥有自己的线程、帧映射和连接；独立原型可选择自己回收测试子进程。
- 为保护已经取得旧函数指针的在途调用，桥接中继保留至宿主自然退出；临时帧、
  工作线程和原生子进程必须释放，不为卸载中继重启宿主。

旧 `native/camera/src/cvi_display.cpp` 已不参与 S6 构建，不能作为自动回退。
它及此前文档中的视频层测量属于先前实现；当前运行以本目录和最新适配记录为准。

## 定向验证

`tests/device/falcon_submit_probe` 直接编译本目录实现，覆盖真实 ELF、共享槽、
焦点、Alpha、异常输出、断连和回退。`surface_fixture/surface_tests` 使用正式
Surface，覆盖隐藏/恢复、空闲不重复提交、无新LVGL帧时焦点恢复，以及外部拥有
的子进程退出。支持主机ASAN/UBSan和S6 ARM32独立进程运行。

实际设备验收、截图与边界见 `docs/S6-ARMV7-DEV.md` 最新章节。
