# 原生输入编辑器

PenChat 0.1.17 默认使用原生 LVGL 编辑器，官方键盘保留在右下角。

## ORANGE 布局

左侧 584×266，三行字母键高 64px，底行高 44px。左下角循环切换
字母、数字、符号；空格宽 280px。右侧宽 376px，候选在最上方，
其下依次是拼音、取消/中英文/完成、80px 正文和底部工具。
候选展开只覆盖正文；编辑工具横排，密钥显隐为第五项，可横向滚动。

所有模式保持键位区域稳定。按键松手提交，滑出后返回仍取消，删除
长按从 450ms 开始，重复间隔从 160ms 递减，最低 60ms。
Shift 单击一次大写，400ms 内双击锁定，再点解除。

编辑是独立事务：取消丢弃本次修改；完成仅交付一次。中文组合未结束时，
完成仅选首候选；下一次完成才退出。官方确认直接完成事务，官方取消恢复
原生正文、光标和未上屏拼音。原生光标按 Unicode 字符计数，传官方时转换为
UTF-16 code units。聊天、系统提示词明确传 multiline，其余字段单行；
此属性与中英文偏好分开。密钥默认遮掩、英文，不写入诊断。

## 系统拼音桥接

薄启动器延迟加载固件 `im` 模块，由系统初始化及管理词库。桥接库只通过
`dlsym(RTLD_DEFAULT, name)` 获取导出的 C 接口，不使用固定地址，不打开或
关闭系统 decoder，不复制系统/用户词库，不修改系统键盘文件。

ORANGE 已核对的 ABI：

- `size_t im_search(const char*, size_t)`
- `const uint16_t* im_get_candidate(size_t, uint16_t*, size_t)`
- `size_t im_choose(size_t)`
- `size_t im_get_fixed_len()`
- `size_t im_get_spl_start_pos(const uint16_t**)`
- `const char* im_get_sps_str(size_t*)`
- `void im_reset_search()`

根据 fixed 长度和 spelling 起点计算已消费原文，将确认前缀上屏，保留未消费
拼音并重新查询。删除同样按剩余组合重新查询，避免复用错误的部分选词状态。
一次最多展示 48 个候选；超过 128 个拼音字节时保留组合并提示官方入口，
不截断输入。接口缺失、异常、超时会保留输入，绝不猜测候选或强制完成。

`ime.request/result` 带 generation、session、sequence 和 revision。
查询合并窗口 35ms，一次在途，候选选择校验查询版本；选词中按键排队。
隐藏页面、官方切换和结束编辑使在途结果失效，停止连续删除并重置引擎。
原生编辑期间扫描不具备启动资格，打开编辑器等待扫描资源释放，并注销 OCR。

## 验证

`tests/test_keyboard.py` 通过实际原生进程、socketpair 和真实 LVGL 触摸路径测试。
它不调用生产后门；候选服务由测试桥接模拟，真机另验证系统 decoder。
原业务测试的 `official_adapter` 显式点击可见官方入口，保留原有字段/网络
回归；专门的编辑器测试关闭该适配，检验原生行为。

截图属于本地生成产物，不提交 Git。`.work/keyboard/` 为主机各状态，
`.work/keyboard-asan/` 为独立 ASAN 产物，`.work/device-acceptance/` 为真机截图。

0.1.17 验证记录：主机完整 `npm test` 通过，专项 ASAN/UBSAN（含泄漏检查）
通过；AArch64、生产 AMR 身份/依赖/strip 检查通过。`aiot-cli check` 仅保留
环境原有的可选 cnpm 提示。

ORANGE 真机验证包含：英文/数字/符号、Shift、中文首选与分段选词、两次完成、
官方取消恢复与确认直返、同进程前后台恢复。测试使用未保存的新增 Provider
表单，结束丢弃，不修改用户 Provider 或会话。系统宿主 PID 全程为 3045。
两次验收记录的候选往返分别为 10ms 和 30ms（抽样观测，不作为稳定延迟承诺）；官方编辑器
已截图对照。没有给出未经测量的官方键盘延迟或中文词库覆盖率结论。
