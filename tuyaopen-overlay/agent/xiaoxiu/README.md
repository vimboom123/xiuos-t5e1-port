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

## 文件

- `system_prompt.md`：系统提示词
- `icon.png`：智能体图标
- `kb/`：本项目自写的两份知识库文档（问答精选、T5-E1 移植）。
  其余知识库文档由 xuos.io 官网、sysoul.com、robonix.ai 与公开报道整理而来，
  版权归原作者，不放进本仓库，用 `research/` 里的脚本可以重新生成。
- `research/xiuos_search.py`：Tavily、Brave、Perplexity 三路检索，key 从本地 .env 读取
- `research/crawl.py`：调用 scrapling CLI 抓 xuos.io 全站与筛选出的外部页面
- `research/build_kb.py`：清洗并按主题合并成知识库文档

## CLI 流程（tuya-devplat-cli）

1. `agent icon-upload` → `project create --end-type 1`
2. `project update --workflow-tag 0 --child-mode 0`，`project update-regions --regions AY`
3. `project model-info` 取草稿 `modelVersionId`；`project model-save`（每次都要带齐 `--tts`、`--voice-id` 等，缺省值会覆盖）；`voice vad-save`；`voice welcome-save`
4. `project model-publish`，`project update-status --shelf-status 1`
5. `project-deploy add --end-type 3/2`；一个产品的设备端只能挂一个智能体，先 `project-deploy remove` 旧的
6. `product-agent ui-save --enabled 1 --float-enabled 1`
7. `panel bind --ui-id <公版智能体面板>`

当前 CLI token 没有知识库写权限（`knowledge create` 返回 `api key permission denied`），知识库需要在开发者平台网页创建并上传，
再通过模型保存接口的 `libResource` 挂到智能体上。
