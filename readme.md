### CERE：TI-84 Plus CE 中文阅读器
![alt text](assets/home.png)
![alt text](assets/texts.png)
#### 运行时 AppVar

- 支持加载多个文章 AppVar。
- CERE 会扫描数据头为 CART 的 AppVar，并在主菜单展示。
- 每个文章 AppVar 在头部保存对应字形 AppVar 名称。
- 阅读器采用流式读取，仅在 RAM 中保留当前页缓存，以避免内存爆炸，也推荐开启 Caesium 中的 “Backup RAM before executing programs”，如果程序卡死，请按计算器背面的 Reset 重启。

运行 CERE 前，请先将 Release 中给定的 CERE.8xp 和对应的基础字形 .8xv 文件发送到计算器。

#### 构建计算器程序

执行 `build.bat`，会在 `bin/` 目录下生成 `CERE.8xp`。

#### 在电脑上生成文章 + 字形 AppVar

1. 安装 Python 依赖：
- python -m pip install -r tools/requirements.txt

2. 准备 UTF-8 编码的文章文本文件（例：assets/article.txt）。

3. 运行生成器：
- generate_assets.bat --article assets/article.txt --font C:\Windows\Fonts\msyh.ttc --out-dir out --article-name DOCA001 --font-name FNTA001

GUI 模式（Tkinter）：
- python tools/generate_appvars.py
- python tools/generate_appvars.py --gui
- 在界面中填写文章/字体/输出目录/应用变量名称，或从下拉列表选择系统字体并查看实时预览。
- 点击“仅生成基础 UI 字形”可只生成独立的基础 UI 字形 AppVar（默认名称为 CEREFNT）。

你可以重复执行命令并更换 --article-name / --font-name，以添加更多文档。

生成文件：
- out/<ARTICLE_NAME>.bin
- out/<ARTICLE_NAME>.8xv
- out/<FONT_NAME>.bin
- out/<FONT_NAME>.8xv

4. 将 CERE.8xp 和所有生成的 .8xv 文件发送到计算器。

#### 生成器行为说明

- 自动提取文章中的全部非 ASCII 字符。
- 将每个所需字符栅格化为 16x16 黑白位图。
- 将排序后的 codepoint+bitmap 条目打包到目标字形 AppVar。
- 将 UTF-8 文章字节和字形 AppVar 名称打包到文章 AppVar。
- 支持章节嵌入：
	- !#Chapter Title#!：创建带标题章节条目。
	- !##!：创建无标题章节条目（显示为 (untitled)）。
- 文章字形 AppVar 始终只包含文章字形。
- 启用 --separate-base-font 时，CERE UI 所需中文字形会输出到独立基础字形 AppVar。
- UI 字形会从源程序字符串中自动收集。

这种方案可显著减小程序体积，仅存储当前文章实际需要的字形。
阅读器按页流式读取 AppVar，仅保留当前页缓存到 RAM。

#### 阅读状态持久化

- 每篇文档的最后阅读位置保存在 AppVar CEREPOS。
- 每篇文档的书签保存在 AppVar CEREBM。
- 重新打开文档时会自动恢复到上次阅读位置。

#### 控制

- 主菜单：上/下选择，2nd/Enter 打开，Alpha 刷新列表，Clear 退出
- 关于页：在菜单选 About 后按 2nd/Enter 进入，任意键返回
- 阅读页：左/右/上/下翻页
- 阅读页：打开后 5 秒内，2nd/Enter 连按两次跳到第一页
- 阅读页：X,T,theta,n 在当前位置切换书签
- 阅读页：STAT 打开跳转面板（书签/章节）
- 阅读页：2nd/Alpha/Clear 返回菜单（快速回首页窗口结束后）
- 跳转面板：上/下选择，左/右切换标签，2nd/Enter 跳转，Clear/Alpha/STAT 返回
- 阅读进度：右下角显示进度条和按文本字节计算的百分比

#### 待办事项

- [x] 流式分页
- [x] 阅读进度显示
- [x] 阅读位置自动记忆（CEREPOS）
- [x] 每文档书签增删（CEREBM）
- [x] 章节目录读取与跳转面板（STAT）
- [x] 生成器图形化
- [ ] 按页码跳转
- [ ] 最近阅读文档列表
- [ ] 书签重命名/删除确认/排序
- [ ] 生成器字体下拉搜索过滤
- [ ] 生成器产物缺字/超限/章节标记校验
