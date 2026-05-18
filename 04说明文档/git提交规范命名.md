二、常用的 type 前缀含义
表格
前缀	全称	适用场景
feat	Features	新增功能、模块、接口（对应版本号的 minor 升级）
fix	Bug Fixes	修复 bug（对应版本号的 patch 升级）
docs	Documentation	仅修改文档、注释、README 等
style	Styles	代码格式调整（空格、缩进、分号等，不影响逻辑）
refactor	Refactoring	重构代码（既不新增功能，也不修复 bug）
perf	Performance	优化性能的修改
test	Tests	新增 / 修改测试用例
build	Build System	构建系统、依赖配置修改（如 Makefile、CMake、package.json）
ci	CI/CD	持续集成配置修改（如 GitHub Actions、GitLab CI）
chore	Chores	其他杂项修改（如更新 .gitignore、清理临时文件）

三、完整示例
bash
运行
# 1. 新增功能
feat(mqtt): 实现 MQTT 心跳上报与重连机制

# 2. 修复 bug
fix(esp8266): 解决 AT 指令发送时的 UART 锁冲突问题

# 3. 重构代码
refactor(task): 统一 FreeRTOS 任务创建与队列处理逻辑

# 4. 文档修改
docs(readme): 更新工程结构与编译说明

# 5. 性能优化
perf(serial): 优化串口帧解析效率，降低丢包率