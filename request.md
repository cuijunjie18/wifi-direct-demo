# 需求文档

阅读docs/architecture.md文档。了解整个项目的架构。

当前的windows部分的实现使用的是C#，希望使用c++实现，不需要图形界面，仅提取逻辑(即改写为cli)，完成demo中的Wi‑Fi Direct 链路层部分，即实现下面的功能
   - 发现附近设备
   - 进行连接 / 入组
   - 获得组内可达 IP

将最后的代码输出到windows_cpp/