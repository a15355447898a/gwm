/* *************************************************************************
 *     gwm.h：與gwm.c相應的頭文件。
 *     版權 (C) 2020-2022 gsm <406643764@qq.com>
 *     本程序為自由軟件：你可以依據自由軟件基金會所發布的第三版或更高版本的
 * GNU通用公共許可證重新發布、修改本程序。
 *     雖然基于使用目的而發布本程序，但不負任何擔保責任，亦不包含適銷性或特
 * 定目標之適用性的暗示性擔保。詳見GNU通用公共許可證。
 *     你應該已經收到一份附隨此程序的GNU通用公共許可證副本。否則，請參閱
 * <http://www.gnu.org/licenses/>。
 * ************************************************************************/

#ifndef GWM_H
#define GWM_H

#include <stdbool.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>
#include "config.h"

#define ICCCM_NAMES (const char *[]) {"WM_PROTOCOLS", "WM_DELETE_WINDOW"}
#define ARRAY_NUM(a) (sizeof(a)/sizeof(a[0]))
#define SH_CMD(cmd_str) {.cmd=(char *const []){"/bin/sh", "-c", cmd_str, NULL}}
#define FUNC_ARG(var, data) (Func_arg){.var=data}

#define TITLE_BUTTON_N (TITLE_BUTTON_END-TITLE_BUTTON_BEGIN+1)
#define TASKBAR_BUTTON_N (TASKBAR_BUTTON_END-TASKBAR_BUTTON_BEGIN+1)
#define CMD_CENTER_ITEM_N (CMD_CENTER_ITEM_END-CMD_CENTER_ITEM_BEGIN+1)
#define DESKTOP_N (DESKTOP_BUTTON_END-DESKTOP_BUTTON_BEGIN+1)

#define BUTTON_MASK (ButtonPressMask|ButtonReleaseMask)
#define POINTER_MASK (BUTTON_MASK|ButtonMotionMask)
#define CROSSING_MASK (EnterWindowMask|LeaveWindowMask)
#define ROOT_EVENT_MASK (SubstructureRedirectMask|SubstructureNotifyMask| \
    PropertyChangeMask|POINTER_MASK|PointerMotionMask|ExposureMask|CROSSING_MASK)
#define BUTTON_EVENT_MASK (ButtonPressMask|ExposureMask|CROSSING_MASK)
#define FRAME_EVENT_MASK (SubstructureRedirectMask|SubstructureNotifyMask| \
    ExposureMask|ButtonPressMask|CROSSING_MASK|FocusChangeMask)
#define TITLE_AREA_EVENT_MASK (ButtonPressMask|ExposureMask|CROSSING_MASK)
#define ICON_EVENT_MASK (ButtonPressMask|ExposureMask|FocusChangeMask)

#define TITLE_BUTTON_INDEX(type) ((type)-TITLE_BUTTON_BEGIN)
#define IS_TITLE_BUTTON(type) \
    ((type)>=TITLE_BUTTON_BEGIN && (type)<=TITLE_BUTTON_END)

#define TASKBAR_BUTTON_INDEX(type) ((type)-TASKBAR_BUTTON_BEGIN)
#define IS_TASKBAR_BUTTON(type) \
    ((type)>=TASKBAR_BUTTON_BEGIN && (type)<=TASKBAR_BUTTON_END)

#define CMD_CENTER_ITEM_INDEX(type) ((type)-CMD_CENTER_ITEM_BEGIN)
#define IS_CMD_CENTER_ITEM(type) \
    ((type)>=CMD_CENTER_ITEM_BEGIN && (type)<=CMD_CENTER_ITEM_END)

#define DESKTOP(wm) (wm->desktop[wm->cur_desktop-1])

enum focus_mode_tag // 窗口聚焦模式
{
    ENTER_FOCUS, CLICK_FOCUS,
};
typedef enum focus_mode_tag Focus_mode;

enum area_type_tag // 窗口的區域類型
{
     MAIN_AREA, SECOND_AREA, FIXED_AREA, FLOATING_AREA, ICONIFY_AREA,
     PREV_AREA, ROOT_AREA,
};
typedef enum area_type_tag Area_type;

enum widget_type_tag // 構件類型
{
    UNDEFINED, ROOT_WIN, STATUS_AREA,
    CLIENT_WIN, CLIENT_FRAME, TITLE_AREA, CLIENT_ICON,

    MAIN_BUTTON, SECOND_BUTTON, FIXED_BUTTON, FLOAT_BUTTON,
    ICON_BUTTON, MAX_BUTTON, CLOSE_BUTTON,

    DESKTOP1_BUTTON, DESKTOP2_BUTTON, DESKTOP3_BUTTON, 

    FULL_BUTTON, PREVIEW_BUTTON, STACK_BUTTON, TILE_BUTTON, DESKTOP_BUTTON,

    CMD_CENTER_ITEM,
    HELP_BUTTON, FILE_BUTTON, TERM_BUTTON, BROWSER_BUTTON, 
    PLAY_START_BUTTON, PLAY_TOGGLE_BUTTON, PLAY_QUIT_BUTTON,
    VOLUME_DOWN_BUTTON, VOLUME_UP_BUTTON, VOLUME_MAX_BUTTON, VOLUME_TOGGLE_BUTTON,
    MAIN_NEW_BUTTON, SEC_NEW_BUTTON, FIX_NEW_BUTTON, FLOAT_NEW_BUTTON,
    ICON_NEW_BUTTON, N_MAIN_UP_BUTTON, N_MAIN_DOWN_BUTTON, FOCUS_MODE_BUTTON,
    QUIT_WM_BUTTON, LOGOUT_BUTTON, REBOOT_BUTTON, POWEROFF_BUTTON, RUN_BUTTON,

    TITLE_BUTTON_BEGIN=MAIN_BUTTON, TITLE_BUTTON_END=CLOSE_BUTTON,
    TASKBAR_BUTTON_BEGIN=DESKTOP1_BUTTON, TASKBAR_BUTTON_END=CMD_CENTER_ITEM,
    LAYOUT_BUTTON_BEGIN=FULL_BUTTON, LAYOUT_BUTTON_END=TILE_BUTTON, 
    DESKTOP_BUTTON_BEGIN=DESKTOP1_BUTTON, DESKTOP_BUTTON_END=DESKTOP3_BUTTON,
    CMD_CENTER_ITEM_BEGIN=HELP_BUTTON, CMD_CENTER_ITEM_END=RUN_BUTTON,
};
typedef enum widget_type_tag Widget_type;

enum font_type_tag // 字體類型, 按字符顯示位置分類
{
    TITLE_AREA_FONT, TITLE_BUTTON_FONT, CMD_CENTER_FONT,
    TASKBAR_BUTTON_FONT, ICON_CLASS_FONT, ICON_TITLE_FONT, STATUS_AREA_FONT,
    FONT_N
};
typedef enum font_type_tag Font_type;

struct rectangle_tag // 矩形窗口的坐標和尺寸
{
    int x, y; // 坐標
    unsigned int w, h; // 尺寸
};
typedef struct rectangle_tag Rect;

struct icon_tag // 縮微窗口相關信息
{
    Window win; // 縮微窗口
    int x, y; // 無邊框時縮微窗口的坐標
    unsigned int w, h; // 無邊框時縮微窗口的尺寸
    Area_type area_type; // 窗口微縮之前的區域類型
    bool is_short_text; // 是否只爲縮微窗口顯示簡短的文字
};
typedef struct icon_tag Icon;

struct client_tag // 客戶窗口相關信息
{   /* 分別爲客戶窗口、父窗口、標題區、標題區按鈕 */
    Window win, frame, title_area, buttons[TITLE_BUTTON_N];
    int x, y; // win的橫、縱坐標
    /* win的寬、高、標題欄高、邊框寬、所属虚拟桌面的掩碼 */
    unsigned int w, h, title_bar_h, border_w, desktop_mask;
    Area_type area_type; // 區域類型
    char *title_text; // 標題的文字
    Icon *icon; // 圖符信息
    const char *class_name; // 客戶窗口的程序類型名
    XClassHint class_hint; // 客戶窗口的程序類型提示
    struct client_tag *prev, *next; // 分別爲前、後節點
};
typedef struct client_tag Client;

enum layout_tag // 窗口管理器的布局模式
{
    FULL, PREVIEW, STACK, TILE,
};
typedef enum layout_tag Layout;

enum pointer_act_tag // 定位器操作類型
{
    NO_OP, MOVE, TOP_RESIZE, BOTTOM_RESIZE, LEFT_RESIZE, RIGHT_RESIZE,
    TOP_LEFT_RESIZE, TOP_RIGHT_RESIZE, BOTTOM_LEFT_RESIZE, BOTTOM_RIGHT_RESIZE,
    ADJUST_LAYOUT_RATIO, POINTER_ACT_N
};
typedef enum pointer_act_tag Pointer_act;

struct taskbar_tag // 窗口管理器的任務欄
{
    /* 分別爲任務欄的窗口、按鈕、縮微區域、狀態區域 */
    Window win, buttons[TASKBAR_BUTTON_N], icon_area, status_area;
    int x, y; // win的坐標
    unsigned int w, h, status_area_w; // win的尺寸、按鈕的尺寸和狀態區域的寬度
    char *status_text; // 狀態區域要顯示的文字
};
typedef struct taskbar_tag Taskbar;

enum icccm_atom_tag // icccm規範的標識符
{
    WM_PROTOCOLS, WM_DELETE_WINDOW, ICCCM_ATOMS_N
};
typedef enum icccm_atom_tag Icccm_atom;

struct desktop_tag // 虛擬桌面相關信息
{
    int n_main_max; // 主區域可容納的客戶窗口數量
    Client *cur_focus_client, *prev_focus_client; // 分別爲當前聚焦結點、前一個聚焦結點
    Layout cur_layout, prev_layout; // 分別爲當前布局模式和前一個布局模式
    Area_type default_area_type; // 默認的窗口區域類型
    double main_area_ratio, fixed_area_ratio; // 分別爲主要和固定區域屏佔比
};
typedef struct desktop_tag Desktop;

struct menu_tag // 一級多行多列菜單 
{
    Window win, *items; // 菜單窗口和菜單項
    unsigned int n, col, row, w, h; // 菜單項數量、列數、行數、寬度、高度
    int x, y; // 菜單窗口的坐標
    unsigned long bg; // 菜單的背景色
};
typedef struct menu_tag Menu;

enum widget_color_tag // 構件顏色類型
{
    NORMAL_BORDER_COLOR, CURRENT_BORDER_COLOR,
    NORMAL_TITLE_AREA_COLOR, CURRENT_TITLE_AREA_COLOR,
    NORMAL_TITLE_BUTTON_COLOR, CURRENT_TITLE_BUTTON_COLOR,
    ENTERED_NORMAL_BUTTON_COLOR, ENTERED_CLOSE_BUTTON_COLOR,
    NORMAL_TASKBAR_BUTTON_COLOR, CHOSEN_TASKBAR_BUTTON_COLOR,
    CMD_CENTER_COLOR, ICON_COLOR, ICON_AREA_COLOR, STATUS_AREA_COLOR,
    WIDGET_COLOR_N 
};
typedef enum widget_color_tag Widget_color;

enum text_color_tag // 文本顏色類型
{
    TITLE_AREA_TEXT_COLOR, TITLE_BUTTON_TEXT_COLOR,
    TASKBAR_BUTTON_TEXT_COLOR, STATUS_AREA_TEXT_COLOR,
    ICON_CLASS_TEXT_COLOR, ICON_TITLE_TEXT_COLOR,
    CMD_CENTER_ITEM_TEXT_COLOR,
    TEXT_COLOR_N 
};
typedef enum text_color_tag Text_color;

struct wm_tag // 窗口管理器相關信息
{
    Display *display; // 顯示器
    int screen; // 屏幕
    unsigned int screen_width, screen_height; // 屏幕寬度、高度
    unsigned int cur_desktop; // 當前虛擬桌面編號
    Desktop desktop[DESKTOP_N]; // 虛擬桌面
	XModifierKeymap *mod_map; // 功能轉換鍵映射
    Window root_win; // 根窗口
    GC gc; // 窗口管理器的圖形信息
    Visual *visual; // 着色類型
    Colormap colormap; // 着色圖
    Atom icccm_atoms[ICCCM_ATOMS_N]; // icccm規範的標識符
    Client *clients; // 頭結點
    Focus_mode focus_mode; // 窗口聚焦模式
    XftFont *font[FONT_N]; // 窗口管理器用到的字體
    Cursor cursors[POINTER_ACT_N]; // 光標
    Taskbar taskbar; // 任務欄
    Menu cmd_center; // 操作中心
    XColor widget_color[WIDGET_COLOR_N]; // 構件顏色
    XftColor text_color[TEXT_COLOR_N]; // 文本顏色
};
typedef struct wm_tag WM;

enum direction_tag // 方向
{
    UP, DOWN, LEFT, RIGHT, 
    LEFT2LEFT, LEFT2RIGHT, RIGHT2LEFT, RIGHT2RIGHT,
    UP2UP, UP2DOWN, DOWN2UP, DOWN2DOWN,
};
typedef enum direction_tag Direction;

enum align_type_tag // 文字對齊方式
{
    TOP_LEFT, TOP_CENTER, TOP_RIGHT,
    CENTER_LEFT, CENTER, CENTER_RIGHT,
    BOTTOM_LEFT, BOTTOM_CENTER, BOTTOM_RIGHT,
};
typedef enum align_type_tag Align_type;

union func_arg_tag // 函數參數類型
{
    bool focus; // 是否聚焦的標志
    char *const *cmd; // 命令字符串
    Direction direction; // 方向
    Layout layout; // 窗口布局模式
    Pointer_act pointer_act; // 窗口操作類型
    int n; // 表示數量
    unsigned int desktop_n; // 虛擬桌面編號
    Area_type area_type; // 窗口區域類型
    double change_ratio; // 變化率
};
typedef union func_arg_tag Func_arg;

struct buttonbind_tag // 定位器按鈕功能綁定
{
    Widget_type widget_type; // 要綁定的構件類型
	unsigned int modifier; // 要綁定的鍵盤功能轉換鍵 
    unsigned int button; // 要綁定的定位器按鈕
	void (*func)(WM *wm, XEvent *e, Func_arg arg); // 要綁定的函數
    Func_arg arg; // 要綁定的函數的參數
};
typedef struct buttonbind_tag Buttonbind;

struct keybind_tag // 鍵盤按鍵功能綁定
{
	unsigned int modifier; // 要綁定的鍵盤功能轉換鍵
	KeySym keysym; // 要綁定的鍵盤功能轉換鍵
	void (*func)(WM *wm, XEvent *e, Func_arg arg); // 要綁定的函數
    Func_arg arg; // 要綁定的函數的參數
};
typedef struct keybind_tag Keybind;

struct rule_tag // 窗口管理器的規則
{
    const char *app_class, *app_name; // 分別爲客戶窗口的程序類型和程序名稱
    const char *class_alias; // 客戶窗口的類型別名
    Area_type area_type; // 客戶窗口的區域類型
    unsigned int title_bar_h, border_w, desktop_mask; // 客戶窗口標題欄高度和邊框寬度、所属虚拟桌面
};
typedef struct rule_tag Rule;

struct move_info_tag /* 定位器舊、新坐標信息 */
{
    int ox, oy, nx, ny; /* 分別爲定位器舊、新坐標 */
};
typedef struct move_info_tag Move_info;

struct delta_rect_tag /* 調整窗口尺寸的信息 */
{
    int dx, dy, dw, dh; /* 分別爲窗口坐標和尺寸的變化量 */
};
typedef struct delta_rect_tag Delta_rect;

struct string_format_tag // 字符串格式
{
    Rect r; // 坐標和尺寸信息
    Align_type align; // 對齊方式
    bool change_bg; // 是否改變背景色的標志
    unsigned long bg; // r區域的背景色
    XftColor fg; // 字符串的前景色
    Font_type font_type; // 字體類型
};
typedef struct string_format_tag String_format;

#endif
