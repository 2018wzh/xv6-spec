# 课程历史审计规则

课程标签是累计快照，不是仅审查当次 diff。`tools/course_history_audit.py` 对每个标签读取
完整 Git tree，校验允许出现的课程路径、规格 ID、公开 check ID、课程术语以及未来标识。

- Lab 1 不允许 `kernel/`、`user/`、`tests/`、占位实现或未来测试名称。
- Lab 2–7 只允许新增当期教学模块；未来实现文件即使未被 Makefile 引用也算泄露。
- Lab 8 允许完整 QEMU xv6 行为面，但禁止 VisionFive 2、FDT、FIT、SDIO、GPT 与硬件
  runner 标识。
- Lab 9 首次允许硬件平台、固定 BSP、vendored libfdt、FIT/SD 镜像和 hardware runner。
- Lab 10 增加累计 `vos.yaml`、审计工具和提交说明，不把 `.vos/` 证据纳入 Git。

每次重建或移动标签后都应在 clean clone 运行：

```sh
python3 tools/course_history_audit.py
```

脚本失败代表标签不能发布。禁止通过缩小扫描范围、忽略未引用文件或给未来词汇增加例外来
掩盖泄露；应修正对应历史 tree 的根因。
