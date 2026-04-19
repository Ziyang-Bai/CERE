#ifndef TEXT_RESOURCES_H
#define TEXT_RESOURCES_H

#include <stdint.h>

#define RES_TXT_MENU_TITLE "CERE 文档"
#define RES_TXT_MENU_ABOUT "关于"
#define RES_TXT_MENU_INVALID "<无效>"
#define RES_TXT_STATUS_READY "就绪"
#define RES_TXT_COPYRIGHT "版权所有(c) 2026 ziyangbai保留所有权利"

#define RES_TXT_ABOUT_TITLE "关于 CERE"
static const char *const RES_TXT_ABOUT_LINES[] = {
    "功能与键位：",
    "主菜单: 上/下选择,2nd/Enter打开",
    "主菜单: Alpha刷新,Clear退出",
    "阅读: 左/右/上/下翻页",
    "阅读: 打开文章五秒内2nd或Enter双击回第一页",
    "阅读: VARS键打书签",
    "阅读: STAT打开章节/书签面板",
    "面板: 左右切换,上下选择,2nd跳转",
    "阅读返回主菜单: Alpha/Clear",
    "安装: CERE.8xp+文档.8xv+字形.8xv",
    "都要一起传入计算器",
    "返回键: Alpha 或 Clear 版本：v1.1",
};
static const uint8_t RES_ABOUT_LINE_Y[] = {
    26u, 44u, 60u, 76u, 92u, 108u, 124u, 140u, 156u, 172u, 188u, 220u,
};
#define RES_ABOUT_LINE_COUNT ((uint8_t)(sizeof(RES_TXT_ABOUT_LINES) / sizeof(RES_TXT_ABOUT_LINES[0])))

#define RES_TXT_PANEL_FOOTER_DEFAULT "2nd/Enter跳转  左右切换  Clear返回"

#define RES_TXT_OPEN_FAIL_PREFIX "打开失败: "
#define RES_TXT_STATUS_NO_DOC_OPEN "没有可打开的文档"
#define RES_TXT_ERR_MISSING_GLYPH_SUFFIX ": 缺少字形 "
#define RES_TXT_ERR_READER_INIT_SUFFIX ": 阅读器初始化失败"
#define RES_TXT_STATUS_NO_BOOKMARKS "当前文档没有书签"
#define RES_TXT_LABEL_BOOKMARK "书签"
#define RES_TXT_LABEL_PAGE " 页码"
#define RES_TXT_LABEL_PROGRESS " 进度"
#define RES_TXT_STATUS_NO_CHAPTERS "当前文档没有章节"
#define RES_TXT_UNTITLED "(untitled)"
#define RES_TXT_STATUS_NO_CART_DOC "未找到 CART 文档"
#define RES_TXT_STATUS_BACK_TO_MENU "已返回主菜单"
#define RES_TXT_STATUS_SELECT_ABOUT_OR_DOC "请选择 关于 或文档"
#define RES_TXT_STATUS_DOC_LIST_REFRESHED "已刷新文档列表"
#define RES_TXT_STATUS_RESTORED_TO "已恢复到 "
#define RES_TXT_STATUS_BOOKMARK_ADDED "已添加书签"
#define RES_TXT_STATUS_BOOKMARK_REMOVED "已取消书签"
#define RES_TXT_STATUS_BOOKMARK_SAVE_FAIL "书签保存失败"
#define RES_TXT_STATUS_JUMPED_FIRST_PAGE "已跳转到第一页"
#define RES_TXT_STATUS_HINT_DOUBLE_TAP_HOME "打开后5秒内再按2nd/Enter回第一页"
#define RES_TXT_STATUS_TOC_READ_FAIL "章节读取失败"
#define RES_TXT_STATUS_BOOKMARK_INDEX_INVALID "书签索引无效"
#define RES_TXT_STATUS_JUMPED_SELECTED "已跳转到选中位置"
#define RES_TXT_STATUS_JUMP_FAIL "跳转失败"
#define RES_TXT_PANEL_TITLE "跳转"
#define RES_TXT_TAB_TOC "章节"
#define RES_TXT_TAB_BOOKMARK "书签"

#define RES_TXT_ARTICLE_OPEN_OK "正常"
#define RES_TXT_ARTICLE_INVALID_ARG "参数无效"
#define RES_TXT_ARTICLE_NOT_FOUND "未找到 AppVar"
#define RES_TXT_ARTICLE_TOO_SMALL "文件过小"
#define RES_TXT_ARTICLE_READ_FAIL "读取失败"
#define RES_TXT_ARTICLE_BAD_MAGIC "CART 标识错误"
#define RES_TXT_ARTICLE_BAD_VERSION "不支持的版本"
#define RES_TXT_ARTICLE_BAD_HEADER "头部无效"
#define RES_TXT_ARTICLE_BAD_LENGTH "长度无效"
#define RES_TXT_ARTICLE_UNKNOWN_ERROR "未知错误"

#endif
