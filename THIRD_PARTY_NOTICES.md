# PenChat 许可与第三方声明

PenChat 自有代码、文档、测试及原创图形采用 [Apache-2.0](LICENSE)。版权为
`Copyright 2026 ls-jl and PenChat contributors`。第三方代码、字体、模型及运行库
保留原许可，不因根目录的 LICENSE 而重新授权。具体范围同时见 [NOTICE](NOTICE)。

`licenses/manifest.json` 记录所保存许可原文的来源和 SHA-256；版本、源码下载地址
及归档校验值以 `native/lvgl-app/dependencies.lock.json`、
`native/scanner/dependencies.lock.json`、`native/fonts/lock.json` 和
`package-lock.json` 为准。构建目录中的依赖与设备资源不进入 Git。

## 原生依赖

| 组件 | 本项目使用的许可 | 原文 / 说明 |
|---|---|---|
| LVGL 9.6.0 | MIT；内含组件各有许可 | `licenses/lvgl.txt`、`licenses/lvgl-COPYRIGHTS.md`；当前配置使用 C 库分配、字符串和 printf，不启用内置 TLSF/printf |
| MD4C | MIT | `licenses/md4c.txt` |
| MicroTeX 代码 | MIT | `licenses/microtex.txt`；C++14 与安全适配见 `scripts/prepare-microtex.py`，PenChat Cairo 后端为自有代码 |
| tinyxml2 9.0.0 | Zlib | `licenses/tinyxml2.txt` |
| nlohmann/json 3.11.3 | MIT | `licenses/json.txt` |
| Cairo 1.18.4 | **选择 MPL-1.1 分支** | `licenses/cairo.txt`、`licenses/cairo-MPL-1.1.txt`；同时保留上游 LGPL 备选原文，不把整个 PenChat 改为 LGPL |
| pixman 0.44.2 | MIT 等上游宽松许可 | `licenses/pixman.txt`，保留其中全部作者及许可 |
| FreeType 2.13.3 | **选择 FTL 分支** | `licenses/freetype.txt`、`licenses/freetype-LICENSE.txt`；产品署名在 NOTICE 中 |
| libwebp 1.5.0 | BSD-3-Clause 及专利授权 | `licenses/webp.txt`、`licenses/webp-PATENTS.txt` |
| miniz 3.0.2 | MIT | `licenses/miniz.txt` |
| stb_image / stb_image_write | **选择 MIT 分支** | `native/camera/third_party/stb/` 保留内嵌许可；JPEG 位移使用无符号运算的本地修复及摘要见 `native/camera/stb-adaptation.json`；许可副本为 `licenses/stb-*.txt` |
| OpenCV 3.4.3（扫描运行库） | BSD-3-Clause | `licenses/opencv-3.4.3.txt`；不使用不对应版本的 Debian OpenCV 4.6 声明代替它 |
| Carotene resize 参考实现 | BSD-3-Clause | `native/scanner/NOTICE` 与 `licenses/scanner-NOTICE.txt`；保留 NVIDIA 声明 |
| libpng 1.6.37 / zlib 1.2.11 | Libpng / Zlib | `licenses/libpng.txt`、`licenses/zlib.txt`；实际二进制哈希在扫描锁文件 |
| libdrm 开发头文件 | MIT 等文件内许可 | `licenses/libdrm.txt`；使用 SDK 开发头文件，不打包 libdrm 动态库 |
| libstdc++ / libgcc | GPL-3.0-or-later WITH GCC-exception-3.1（适用文件） | `licenses/GPL-3.0.txt`、`licenses/GCC-Runtime-Library-Exception-3.1.txt`；见下方二进制来源限制 |

Cairo 当前静态链接，源码未被 PenChat 修改。随二进制分发时，须向接收者提供对应的
MPL 覆盖源码；锁文件中的 Cairo 1.18.4 官方归档及哈希是源码定位，构建命令在
`scripts/build-render-deps.sh`。正式发布二进制时同时提供该归档或确认可持续获取的
对应源码下载入口；修改 Cairo 时还须提供修改后的覆盖源码与修改声明。

## 字体与公式资源

- DejaVuSans.ttf：Bitstream Vera 许可，DejaVu 修改为公有领域；见
  `native/fonts/LICENSE.txt` 和 `licenses/DejaVu.txt`。其中 Debian 打包脚本的 GPL
  声明不等于 TTF 本身采用 GPL；本项目不分发这些打包脚本。
- MicroTeX 的 AMS/Euler 字体：保留 `licenses/math-OFL.txt` 的 OFL-1.1 及保留名称。
- Computer Modern、dsrom 等资源：保留上游 `math-Knuth.txt`、`math-dsrom.txt` 的
  独立条款，不把 MicroTeX 代码的 MIT 许可套用到字体。
- Greek / Cyrillic 资源目录各带 GPLv3 原文，见 `licenses/math-greek-GPL-3.0.txt`
  和 `licenses/math-cyrillic-GPL-3.0.txt`。它们作为独立资源保留原许可，不宣称
  获得额外字体例外。锁定的 MicroTeX 源码归档包含所用资源，但本次核对没有确认
  它包含这些字体完整的首选修改形式；发布含这些字体的二进制前需补齐对应字体源码，
  或以授权明确且源码义务已满足的资源替换。单独附上 GPL 文本不代表此项已完成。
- 中文正文字体从设备读取，不在 Git 或 AMR 中分发；设备字体许可不由 PenChat 授予。

## Falcon 与系统接口

`native/vendor/iot-miniapp-sdk/` 是外部 SDK，不属于 PenChat 自有代码：

- QuickJS 头文件为 MIT，许可及原作者见 `licenses/quickjs.txt`。
- jqbson 中 json11 派生部分保留 Dropbox MIT 声明，见 `licenses/json11.txt`。
- 明确带 Apache-2.0 的 Android/AOSP 派生文件保留原头部，汇总见
  `licenses/sdk-Apache-NOTICES.txt`；完整 Apache 文本见根目录 LICENSE。
- **其余 JQ/Falcon 文件未确认有覆盖其复制、修改和再分发的许可**。部分只有 Alibaba
  版权声明或没有许可声明，不能因 SDK 可下载或其他文件为 Apache/MIT 就推断授权。
  在取得相应授权或用独立实现替换前，不将它们标为 Apache-2.0，也不宣称整个仓库
  已完成开源许可核验。源码中的现有版权声明全部保留。

`falcon-ui@2.0.2` 的 npm 元数据声明 MIT，`bindingx-parser@0.0.3` 声明 Apache-2.0。
当前启动器没有导入这些组件，生产构建只编译薄启动器；npm 依赖不提交到仓库。
falcon-ui 发布包未附完整 MIT 版权文本，本项目不虚构其版权所有人。若后续开始打包
该组件，须先补齐其实际许可声明及传递依赖声明。

libcurl、SQLite、glibc、系统输入法/OCR 与 Falcon 宿主由设备提供；PenChat 不打包
这些系统动态库。SQLite 主项目为公有领域、curl 有自己的宽松许可、glibc 有 LGPL
义务；设备提供者的分发义务不由本项目的 Apache 声明替代。添加新的运行库副本时
应另行登记对应版本、源码与完整声明。

## RKNN 与扫描资源：未获授权

**本项目未取得 Rockchip RKNN SDK 头文件、RKNN 运行库，以及扫描模型和校准资源的
使用/再分发授权。PenChat 的 Apache-2.0 不覆盖也不授予这些材料的任何权利。**

- `native/scanner/engine/include/rknn/rknn_api.h` 原文明确声明专有、保密，要求
  Rockchip 明确书面许可。保留该原文不是获得授权；Git 可见也不等于可自由使用。
- RKNN 运行库由设备提供，不在 AMR 中复制；仅凭设备上已安装不能推断可再分发。
- `pintu_32.rknn`、`demo_gain.json`、`demo_gain_160x288.f32` 从本地外部资源包准备，
  只记录来源路径和校验值，未取得开源或再分发授权。它们不进入 Git。
- 本地生产构建仍会包含扫描模型与校准资源。**这样的 AMR 不应作为已获授权的公开
  安装包发布**；须先取得授权或移除/替换相关内容。许可证文件不会补授缺失的权利。

## 二进制发布前仍需处理的事项

本次工作明确许可范围并补齐已有原文，不把“能构建”当作“获得全部分发授权”：

1. RKNN、扫描资源和未明确授权的 Falcon/JQ SDK：取得有效授权或替换/移除。
2. 扫描所用板端 `libstdc++.so.6.0.29` 的精确构建来源及对应源码尚未确认；旧脚本
   使用的 Debian GCC 12 copyright 不能证明该库的来源，本次不再采用该错误归属。
   GCC Runtime Exception 允许符合条件的应用链接，不免除分发运行库本身的源码义务。
   正式分发该运行库前，应提供对应源码及任何补丁/构建资料，或改用来源完整的构建。
3. 补齐上述 GPL 字体的对应源码；为 Cairo 覆盖源码提供可靠下载。

`python3 scripts/stage-licenses.py` 校验许可副本哈希并将 LICENSE、NOTICE、本清单及
许可原文放入 AMR 的 `assets/licenses/`。`npm run check:licenses` 检查来源副本、
许可哈希和打包结果；这些自动检查只验证材料完整性，不宣称未知授权已解决。
