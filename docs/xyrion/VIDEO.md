# MP4 主菜单背景

设置 → Xyrion → 开启视频背景，填写本地 MP4 的完整路径（支持中文路径），可调整暗化程度。
背景按原比例填满窗口，循环播放，不播放音轨。进入游戏时停止并释放资源；视频失败时使用地图背景，并在设置页显示原因。

DDNet 原始依赖中的 FFmpeg 只用于录像输出，缺少 MP4 读取器和 H.264 解码器，会报 `Invalid data found when processing input`。
构建脚本自动准备完整播放依赖，并复制以下五个 DLL。分发或更新时，请保留整个程序文件夹，不能只替换 exe：

- avcodec-61.dll
- avformat-61.dll
- avutil-59.dll
- swresample-5.dll
- swscale-8.dll

依赖来自 [BtbN FFmpeg 7.1 shared](https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2025-07-31-14-15)，
与 DDNet 提供的 FFmpeg 7 头文件使用相同 ABI。构建脚本固定下载版本并验证 SHA256。
许可证见发行包 `LICENSE-FFMPEG.txt`（GPLv3）；构建脚本、构建配置和对应源码位置见
[FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) 与 [FFmpeg](https://github.com/FFmpeg/FFmpeg/tree/n7.1.1)。

解码在后台线程进行，只保留一个待上传帧；输出最长边限制为 1920 像素。
客户端不访问视频 URL；仅使用本地文件。
