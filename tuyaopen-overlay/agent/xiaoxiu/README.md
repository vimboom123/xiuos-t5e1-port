# 云端智能体「小秀」

涂鸦开发者平台上的 AI 智能体，挂在端到端基线产品上，作为希秀泛在团队与 XiUOS 的语音讲解员。平台上的项目编码、产品 PID 不写进仓库。

| 项 | 取值 |
|---|---|
| 模式 | 自主规划（workflow-tag 0），不走工作流 |
| 模型 | 平台 LLM 值 275（Deepseek V4 Flash） |
| 音色 | 184（Mature Sister），语速 30 |
| 语音 | VAD 800 ms，极速打断，上下文 8 轮 |
| 数据中心 | AY |
| 部署 | 产品设备端 + 面板端；面板 AI 入口（下拉 + 悬浮按钮）开启 |
| 知识库 | 文档库 12 篇 + 问答库 34 条，1.0.2 起挂载，相似度 0.8 |

## 文件

- `system_prompt.md`：系统提示词
- `icon.png`：智能体图标
- `kb/`：本项目自写的两份知识库文档（问答精选、T5-E1 移植）。
  其余知识库文档由 xuos.io 官网、sysoul.com、robonix.ai 与公开报道整理而来，
  版权归原作者，不放进本仓库，用 `research/` 里的脚本可以重新生成。
- `research/xiuos_search.py`：Tavily、Brave、Perplexity 三路检索，key 从本地 .env 读取
- `research/crawl.py`：调用 scrapling CLI 抓 xuos.io 全站与筛选出的外部页面
- `research/build_kb.py`：清洗并按主题合并成知识库文档
- `research/sanitize_kb.py`：上传前脱敏（见下文「知识库」）

## CLI 流程（tuya-devplat-cli）

1. `agent icon-upload` → `project create --end-type 1`
2. `project update --workflow-tag 0 --child-mode 0`，`project update-regions --regions AY`
3. `project model-info` 取草稿 `modelVersionId`；`project model-save`（每次都要带齐 `--tts`、`--voice-id` 等，缺省值会覆盖）；`voice vad-save`；`voice welcome-save`
4. `project model-publish`，`project update-status --shelf-status 1`
5. `project-deploy add --end-type 3/2`；一个产品的设备端只能挂一个智能体，先 `project-deploy remove` 旧的
6. `product-agent ui-save --enabled 1 --float-enabled 1`
7. `panel bind --ui-id <公版智能体面板>`

## 知识库

当前 CLI token 没有知识库写权限：`knowledge create` 返回 `api key permission denied`，`knowledge upload` 返回
`Remote api run unknown failed`。所以知识库在开发者平台网页上创建和上传，再用 CLI 挂到智能体上。

| 库 | 类型 | 内容 |
|---|---|---|
| 文档知识库 | 文档 | `kb/` 两篇加上脚本生成的十篇，共 12 篇 Markdown，一次传一篇 |
| 问答知识库 | 问答 | 34 条问答，按平台中文模板导入 |

网页入口：智能体 → 内容管理 → 知识库。

- 文档库：「新增知识」进入三步向导（上传文件 → 解析和分段策略 → 完成），单文件不超过 8 MB。
- 问答库：「导入/导出 → 导入」。模板三列依次是 `知识ID(…)`（新增时留空）、`知识标题(必填)`、`答案(必填)`。
- 问答导入后状态是「未发布」，要全选后点「发布」。

xlsx 要用共享字符串（xlsxwriter 生成）。openpyxl 写出的 `inlineStr` 会被判成「标题、答案不能为空」，34 条全部报错。

平台对文档做敏感信息检测，一部分由模型判定，命中后该文档「处理失败」。实测命中过这些内容：

- 邮箱、电话；
- URL 查询串里像手机号的长数字、CSDN 的 `qq_` 用户号；
- J-Link 序列号；
- Kconfig 里示例 Wi-Fi 的 SSID 和密码；
- 替换后留下的「<编号>」占位符。

`sanitize_kb.py` 按这些规则处理：链接只保留域名，长数字直接删除，不留占位符。
用法是 `python sanitize_kb.py -o <输出目录> <源文件或目录>...`。

挂载时调用 `project model-save --raw-body '{"params":{...}}'`，在 `params` 里写：

```json
"libResource": [{"libCode": "<文档库>", "libCategories": []}, {"libCode": "<问答库>", "libCategories": []}],
"libConfig": {"newRecallStrategy": true, "showSource": false, "similarity": 0.8, "sourceRule": [0]}
```

其余字段（`llm`、`prompt`、`maxMessage`、`voiceId`、`tts`、`audioFormat`、`voiceSpeed`）照 `model-info` 原样带上。

`prompt` 要取 `model-info` 的 `prompt` 字段，不能取 `systemPrompt`。后者是平台在提示词后面拼接了「用户的实时变量」后的结果，
回写后这一段会重复。保存后再跑一次 `voice vad-save`，然后 `model-publish`。
