/* *************************************************************************
 *     func.c：實現按鍵和按鍵所要綁定的功能。
 *     版權 (C) 2020-2022 gsm <406643764@qq.com>
 *     本程序為自由軟件：你可以依據自由軟件基金會所發布的第三版或更高版本的
 * GNU通用公共許可證重新發布、修改本程序。
 *     雖然基于使用目的而發布本程序，但不負任何擔保責任，亦不包含適銷性或特
 * 定目標之適用性的暗示性擔保。詳見GNU通用公共許可證。
 *     你應該已經收到一份附隨此程序的GNU通用公共許可證副本。否則，請參閱
 * <http://www.gnu.org/licenses/>。
 * ************************************************************************/

#include <time.h>
#include <unistd.h>
#include "gwm.h"
#include "font.h"
#include "func.h"
#include "client.h"
#include "desktop.h"
#include "entry.h"
#include "grab.h"
#include "handler.h"
#include "hint.h"
#include "icon.h"
#include "layout.h"
#include "menu.h"
#include "misc.h"

static Delta_rect get_key_delta_rect(Client *c, Direction dir);
static bool is_prefer_move_resize(WM *wm, Client *c, Delta_rect *d);
static bool is_on_screen(WM *wm, int x, int y, unsigned int w, unsigned int h);
static bool fix_move_resize(WM *wm, Client *c, Delta_rect *d);
static bool is_match_click(WM *wm, XEvent *oe, XEvent *ne);
static bool get_valid_click(WM *wm, Pointer_act act, XEvent *oe, XEvent *ne);
static void do_valid_pointer_move_resize(WM *wm, Client *c, Move_info *m, Pointer_act act, bool is_resize);
static void update_hint_win_for_resize(WM *wm, Client *c);
static Delta_rect get_pointer_delta_rect(Client *c, const Move_info *m, Pointer_act act);
static void print_area(Drawable d, int x, int y, unsigned int w, unsigned int h);

void choose_client(WM *wm, XEvent *e, Func_arg arg)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    if(c->area_type == ICONIFY_AREA)
        move_client(wm, c, get_area_head(wm, c->icon->area_type), c->icon->area_type);
    if(DESKTOP(wm).cur_layout == PREVIEW)
        change_layout(wm, e, (Func_arg){.layout=DESKTOP(wm).prev_layout});
}

void exec(WM *wm, XEvent *e, Func_arg arg)
{
    pid_t pid=fork();
	if(pid == 0)
    {
		if(wm->display)
            close(ConnectionNumber(wm->display));
		if(!setsid())
            perror("未能成功地爲命令創建新會話");
		if(execvp(arg.cmd[0], arg.cmd) == -1)
            exit_with_perror("命令執行錯誤");
    }
    else if(pid == -1)
        perror("未能成功地爲命令創建新進程");
}

void key_move_resize_client(WM *wm, XEvent *e, Func_arg arg)
{
    if(DESKTOP(wm).cur_layout==TILE || DESKTOP(wm).cur_layout==STACK)
    {
        Client *c=DESKTOP(wm).cur_focus_client;
        Delta_rect d=get_key_delta_rect(c, arg.direction);
        if(c->area_type!=FLOATING_AREA && DESKTOP(wm).cur_layout==TILE)
            move_client(wm, c, get_area_head(wm, FLOATING_AREA), FLOATING_AREA);
        if(is_prefer_move_resize(wm, c, &d) || fix_move_resize(wm, c, &d))
        {
            move_resize_client(wm, c, &d);
            update_hint_win_for_resize(wm, c);
            while(1)
            {
                XEvent ev;
                XMaskEvent(wm->display, ROOT_EVENT_MASK|KeyReleaseMask, &ev);
                if( ev.type==KeyRelease && ev.xkey.state==e->xkey.state
                    && ev.xkey.keycode==e->xkey.keycode)
                {
                    XUnmapWindow(wm->display, wm->hint_win);
                    break;
                }
                else
                    handle_event(wm, &ev);
            }
        }
    }
}

static Delta_rect get_key_delta_rect(Client *c, Direction dir)
{
    int wi=c->size_hint.width_inc, hi=c->size_hint.height_inc;
    Delta_rect dr[] =
    {
        [UP]          = {  0, -hi,   0,   0},
        [DOWN]        = {  0,  hi,   0,   0},
        [LEFT]        = {-wi,   0,   0,   0},
        [RIGHT]       = { wi,   0,   0,   0},
        [LEFT2LEFT]   = {-wi,   0,  wi,   0},
        [LEFT2RIGHT]  = { wi,   0, -wi,   0},
        [RIGHT2LEFT]  = {  0,   0, -wi,   0},
        [RIGHT2RIGHT] = {  0,   0,  wi,   0},
        [UP2UP]       = {  0, -hi,   0,  hi},
        [UP2DOWN]     = {  0,  hi,   0, -hi},
        [DOWN2UP]     = {  0,   0,   0, -hi},
        [DOWN2DOWN]   = {  0,   0,   0,  hi},
    };
    return dr[dir];
}

static bool is_prefer_move_resize(WM *wm, Client *c, Delta_rect *d)
{
    return ( ((!d->dw && !d->dh)
        && is_on_screen(wm, c->x+d->dx, c->y+d->dy, c->w+d->dw, c->h+d->dh))
        || is_prefer_resize(wm, c, d));
}

/* 通過求窗口與屏幕是否有交集來判斷窗口是否已經在屏幕外。
 * 若滿足以下條件，則有交集：窗口與屏幕中心距≤窗口半邊長+屏幕半邊長。
 * 即：|x+w/2-0-sw/2|＜|w/2+sw/2| 且 |y+h/2-0-sh/2|＜|h/2+sh/2|。
 * 兩邊同乘以2，得：|2*x+w-sw|＜|w+sw| 且 |2*y+h-sh|＜|h+sh|。
 */
static bool is_on_screen(WM *wm, int x, int y, unsigned int w, unsigned int h)
{
    unsigned int sw=wm->screen_width, sh=wm->screen_height;
    return abs(2*x+w-sw)<w+sw && abs(2*y+h-sh)<h+sh;
}

static bool fix_move_resize(WM *wm, Client *c, Delta_rect *d)
{
    XSizeHints *p=&c->size_hint;
    unsigned int w, h, cw=c->w, ch=c->h;
    if( (d->dw || d->dh)
        && (!is_prefer_size(cw, ch, p) || !is_prefer_aspect(cw, ch, p)))
    {
        if(p->min_width)
            w=cw=MAX(cw, p->min_width);
        if(p->min_height)
            h=ch=MAX(ch, p->min_height);
        if(p->max_width)
            w=cw=MIN(cw, p->max_width);
        if(p->max_height)
            h=ch=MIN(ch, p->max_height);
        if(d->dw)
            for(w=(int)p->base_width; ;w+=(int)p->width_inc)
                if(is_prefer_size(w, ch, p) && is_prefer_aspect(w, ch, p))
                    break;
        if(d->dh)
            for(h=p->base_height; ;h+=p->height_inc)
                if(is_prefer_size(w, h, p) && is_prefer_aspect(w, h, p))
                    break;
        d->dw=(int)w-(int)c->w, d->dh=(int)h-(int)c->h;
        // 修正尺寸時，應確保尺寸變化方向上鄰近光標的邊與光標的間距基本不變
        d->dx = d->dx ? d->dx : -d->dw, d->dy = d->dy ? d->dy : -d->dh;
        return true;
    }
    return false;
}

void quit_wm(WM *wm, XEvent *e, Func_arg arg)
{
    clear_wm(wm);
    exit(EXIT_SUCCESS);
}

void close_client(WM *wm, XEvent *e, Func_arg arg)
{
    /* 刪除窗口會產生UnmapNotify事件，處理該事件時再刪除框架 */
    Client *c=DESKTOP(wm).cur_focus_client;
    if( c != wm->clients
        && !send_event(wm, wm->icccm_atoms[WM_DELETE_WINDOW], c->win))
        XDestroyWindow(wm->display, c->win);
}

void close_all_clients(WM *wm, XEvent *e, Func_arg arg)
{
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
        if(!send_event(wm, wm->icccm_atoms[WM_DELETE_WINDOW], c->win))
            XDestroyWindow(wm->display, c->win);
}

/* 取得窗口疊次序意義上的下一個客戶窗口 */
void next_client(WM *wm, XEvent *e, Func_arg arg)
{   /* 允許切換至根窗口 */
    Client *c=get_prev_client(wm, DESKTOP(wm).cur_focus_client);
    focus_client(wm, wm->cur_desktop, c ? c : wm->clients);
}

/* 取得窗口疊次序意義上的上一個客戶窗口 */
void prev_client(WM *wm, XEvent *e, Func_arg arg)
{   /* 允許切換至根窗口 */
    Client *c=get_next_client(wm, DESKTOP(wm).cur_focus_client);
    focus_client(wm, wm->cur_desktop, c ? c : wm->clients);
}

void adjust_n_main_max(WM *wm, XEvent *e, Func_arg arg)
{
    if(DESKTOP(wm).cur_layout == TILE)
    {
        int *m=&DESKTOP(wm).n_main_max;
        *m = *m+arg.desktop_n>=1 ? *m+arg.desktop_n : 1;
        update_layout(wm);
    }
}

/* 在固定區域比例不變的情況下調整主區域比例，主、次區域比例此消彼長 */
void adjust_main_area_ratio(WM *wm, XEvent *e, Func_arg arg)
{
    if(DESKTOP(wm).cur_layout==TILE && get_typed_clients_n(wm, SECOND_AREA))
    {
        Desktop *d=&DESKTOP(wm);
        double mr=d->main_area_ratio+arg.change_ratio, fr=d->fixed_area_ratio;
        int mw=mr*wm->screen_width, sw=wm->screen_width*(1-fr)-mw;
        if(sw>=MOVE_RESIZE_INC && mw>=MOVE_RESIZE_INC)
        {
            d->main_area_ratio=mr;
            update_layout(wm);
        }
    }
}

/* 在次區域比例不變的情況下調整固定區域比例，固定區域和主區域比例此消彼長 */
void adjust_fixed_area_ratio(WM *wm, XEvent *e, Func_arg arg)
{ 
    if(DESKTOP(wm).cur_layout==TILE && get_typed_clients_n(wm, FIXED_AREA))
    {
        Desktop *d=&DESKTOP(wm);
        double fr=d->fixed_area_ratio+arg.change_ratio, mr=d->main_area_ratio;
        int mw=wm->screen_width*(mr-arg.change_ratio), fw=wm->screen_width*fr;
        if(mw>=MOVE_RESIZE_INC && fw>=MOVE_RESIZE_INC)
        {
            d->main_area_ratio-=arg.change_ratio, d->fixed_area_ratio=fr;
            update_layout(wm);
        }
    }
}

void change_area(WM *wm, XEvent *e, Func_arg arg)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    Layout l=DESKTOP(wm).cur_layout;
    Area_type t=arg.area_type==PREV_AREA ? c->icon->area_type : arg.area_type;
    if( c!=wm->clients && (l==TILE || (l==STACK
        && (c->area_type==ICONIFY_AREA || t==ICONIFY_AREA))))
        move_client(wm, c, get_area_head(wm, t), t);
}

static bool is_match_click(WM *wm, XEvent *oe, XEvent *ne)
{
    return (ne->type==ButtonRelease && ne->xbutton.button==oe->xbutton.button);
}

static bool get_valid_click(WM *wm, Pointer_act act, XEvent *oe, XEvent *ne)
{
    if(act==NO_OP || grab_pointer(wm, act))
    {
        do
        {
            XMaskEvent(wm->display, ROOT_EVENT_MASK|POINTER_MASK, ne);
            handle_event(wm, ne);
        }while(!is_match_click(wm, oe, ne));
        if(act != NO_OP)
            XUngrabPointer(wm->display, CurrentTime);
        return true;
    }
    return false;
}

void pointer_swap_clients(WM *wm, XEvent *e, Func_arg arg)
{
    XEvent ev;
    Layout layout=DESKTOP(wm).cur_layout;
    Client *from=DESKTOP(wm).cur_focus_client, *to=NULL, *head=wm->clients;
    if(layout!=TILE || from==head || !get_valid_click(wm, SWAP, e, &ev))
        return;

    /* 因爲窗口不隨定位器動態移動，故釋放按鈕時定位器已經在按下按鈕時
     * 定位器所在的窗口的外邊。因此，接收事件的是根窗口。 */
    int x=ev.xbutton.x-TASKBAR_BUTTON_WIDTH*TASKBAR_BUTTON_N;
    if((to=win_to_client(wm, ev.xbutton.subwindow)) == NULL)
        for(Client *c=head->next; c!=head && !to; c=c->next)
            if( c!=from && c->area_type==ICONIFY_AREA
                && x>=c->icon->x && x<c->icon->x+c->icon->w)
                to=c;
    if(to) 
        swap_clients(wm, from, to);
}

void maximize_client(WM *wm, XEvent *e, Func_arg arg)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    if(c != wm->clients)
    {
        unsigned int bw=c->border_w, th=c->title_bar_h;
        c->x=bw, c->y=bw+th;
        c->w=wm->screen_width-2*bw;
        c->h=wm->screen_height-2*bw-th-wm->taskbar.h;
        if(DESKTOP(wm).cur_layout == TILE)
            move_client(wm, c, get_area_head(wm, FLOATING_AREA), FLOATING_AREA);
        move_resize_client(wm, c, NULL);
    }
}

void pointer_move_resize_client(WM *wm, XEvent *e, Func_arg arg)
{
    Layout layout=DESKTOP(wm).cur_layout;
    Move_info m={e->xbutton.x_root, e->xbutton.y_root, 0, 0};
    Client *c=DESKTOP(wm).cur_focus_client;
    Pointer_act act=(arg.resize ? get_resize_act(c, &m) : MOVE);
    if(layout==FULL || layout==PREVIEW || !grab_pointer(wm, act))
        return;

    XEvent ev;
    do /* 因設置了獨享定位器且XMaskEvent會阻塞，故應處理按、放按鈕之間的事件 */
    {
        XMaskEvent(wm->display, ROOT_EVENT_MASK|POINTER_MASK, &ev);
        if(ev.type == MotionNotify)
        {
            if(c->area_type!=FLOATING_AREA && layout==TILE)
                move_client(wm, c, get_area_head(wm, FLOATING_AREA), FLOATING_AREA);
            /* 因X事件是異步的，故xmotion.x和ev.xmotion.y可能不是連續變化 */
            m.nx=ev.xmotion.x, m.ny=ev.xmotion.y;
            do_valid_pointer_move_resize(wm, c, &m, act, arg.resize);
        }
        else
            handle_event(wm, &ev);
    }while(!is_match_click(wm, e, &ev));
    XUngrabPointer(wm->display, CurrentTime);
    XUnmapWindow(wm->display, wm->hint_win);
}

static void do_valid_pointer_move_resize(WM *wm, Client *c, Move_info *m, Pointer_act act, bool is_resize)
{
    bool fix;
    Delta_rect d=get_pointer_delta_rect(c, m, act);
    if(is_prefer_move_resize(wm, c, &d) || (fix=fix_move_resize(wm, c, &d)))
    {
        move_resize_client(wm, c, &d);
        update_hint_win_for_resize(wm, c);
        if(is_resize)
        {
            if(!fix && d.dw) // dx爲0表示定位器從窗口右邊調整尺寸，非0則表示左邊調整
                m->ox = d.dx ? m->ox-d.dw : m->ox+d.dw;
            if(!fix && d.dh) // dy爲0表示定位器從窗口下邊調整尺寸，非0則表示上邊調整
                m->oy = d.dy ? m->oy-d.dh : m->oy+d.dh;
        }
        else
            m->ox=m->nx, m->oy=m->ny;
    }
}

static void update_hint_win_for_resize(WM *wm, Client *c)
{
    char str[BUFSIZ];
    unsigned int w=get_client_col(wm, c), h=get_client_row(wm, c);
    sprintf(str, "(%d, %d) %ux%u", c->x, c->y, w, h);
    get_string_size(wm, wm->font[HINT_FONT], str, &w, NULL);
    int x=(wm->screen_width-w)/2, y=(wm->screen_height-HINT_WIN_HEIGHT)/2;
    XMoveResizeWindow(wm->display, wm->hint_win, x, y, w, HINT_WIN_HEIGHT);
    XMapRaised(wm->display, wm->hint_win);
    String_format f={{0, 0, w, HINT_WIN_HEIGHT}, CENTER,
        false, 0, wm->text_color[HINT_TEXT_COLOR], HINT_FONT};
    draw_string(wm, wm->hint_win, str, &f);
}

static Delta_rect get_pointer_delta_rect(Client *c, const Move_info *m, Pointer_act act)
{
    int dx=m->nx-m->ox, dy=m->ny-m->oy;
    Delta_rect dr[] =
    {
        [NO_OP]               = { 0,  0,   0,   0},
        [MOVE]                = {dx, dy,   0,   0},
        [TOP_RESIZE]          = { 0, dy,   0, -dy},
        [BOTTOM_RESIZE]       = { 0,  0,   0,  dy},
        [LEFT_RESIZE]         = {dx,  0, -dx,   0},
        [RIGHT_RESIZE]        = { 0,  0,  dx,   0},
        [TOP_LEFT_RESIZE]     = {dx, dy, -dx, -dy},
        [TOP_RIGHT_RESIZE]    = { 0, dy,  dx, -dy},
        [BOTTOM_LEFT_RESIZE]  = {dx,  0, -dx,  dy},
        [BOTTOM_RIGHT_RESIZE] = { 0,  0,  dx,  dy},
    };
    return dr[act];
}

void pointer_change_area(WM *wm, XEvent *e, Func_arg arg)
{
    XEvent ev;
    Client *from=DESKTOP(wm).cur_focus_client, *to;
    if( DESKTOP(wm).cur_layout!=TILE || from==wm->clients
        || !get_valid_click(wm, CHANGE, e, &ev))
        return;

    /* 因爲窗口不隨定位器動態移動，故釋放按鈕時定位器已經在按下按鈕時
     * 定位器所在的窗口的外邊。因此，接收事件的是根窗口。 */
    Window win=ev.xbutton.window, subw=ev.xbutton.subwindow;
    to=win_to_client(wm, subw);
    if(!to)
        to=win_to_iconic_state_client(wm, subw);
    if(ev.xbutton.x == 0)
        move_client(wm, from, get_area_head(wm, SECOND_AREA), SECOND_AREA);
    else if(ev.xbutton.x == wm->screen_width-1)
        move_client(wm, from, get_area_head(wm, FIXED_AREA), FIXED_AREA);
    else if(ev.xbutton.y == 0)
        maximize_client(wm, NULL, arg);
    else if(subw == wm->taskbar.win)
        move_client(wm, from, get_area_head(wm, ICONIFY_AREA), ICONIFY_AREA);
    else if(win==wm->root_win && subw==None)
        move_client(wm, from, get_area_head(wm, MAIN_AREA), MAIN_AREA);
    else if(to)
        move_client(wm, from, to, to->area_type);
}

void adjust_layout_ratio(WM *wm, XEvent *e, Func_arg arg)
{
    if( DESKTOP(wm).cur_layout!=TILE
        || !is_layout_adjust_area(wm, e->xbutton.window, e->xbutton.x_root)
        || !grab_pointer(wm, ADJUST_LAYOUT_RATIO))
        return;
    int ox=e->xbutton.x_root, nx, dx;
    XEvent ev;
    do /* 因設置了獨享定位器且XMaskEvent會阻塞，故應處理按、放按鈕之間的事件 */
    {
        XMaskEvent(wm->display, ROOT_EVENT_MASK|POINTER_MASK, &ev);
        if(ev.type == MotionNotify)
        {
            nx=ev.xmotion.x, dx=nx-ox;
            if(abs(dx)>=MOVE_RESIZE_INC && change_layout_ratio(wm, ox, nx))
                update_layout(wm), ox=nx;
        }
        else
            handle_event(wm, &ev);
    }while(!is_match_click(wm, e, &ev));
    XUngrabPointer(wm->display, CurrentTime);
}

void iconify_all_clients(WM *wm, XEvent *e, Func_arg arg)
{
    for(Client *c=wm->clients->prev; c!=wm->clients; c=c->prev)
        if(is_on_cur_desktop(wm, c) && c->area_type!=ICONIFY_AREA)
            iconify(wm, c);
}

void deiconify_all_clients(WM *wm, XEvent *e, Func_arg arg)
{
    for(Client *c=wm->clients->prev; c!=wm->clients; c=c->prev)
        if(is_on_cur_desktop(wm, c) && c->area_type==ICONIFY_AREA)
            deiconify(wm, c);
    update_layout(wm);
}

void change_default_area_type(WM *wm, XEvent *e, Func_arg arg)
{
    DESKTOP(wm).default_area_type=arg.area_type;
}

void toggle_focus_mode(WM *wm, XEvent *e, Func_arg arg)
{
    wm->focus_mode = wm->focus_mode==ENTER_FOCUS ? CLICK_FOCUS : ENTER_FOCUS;
}

void open_cmd_center(WM *wm, XEvent *e, Func_arg arg)
{
    show_menu(wm, e, &wm->cmd_center, wm->taskbar.buttons[TASKBAR_BUTTON_INDEX(CMD_CENTER_ITEM)]);
}

void toggle_border_visibility(WM *wm, XEvent *e, Func_arg arg)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    c->border_w = c->border_w ? 0 : BORDER_WIDTH;
    XSetWindowBorderWidth(wm->display, c->frame, c->border_w);
    update_layout(wm);
}

void toggle_title_bar_visibility(WM *wm, XEvent *e, Func_arg arg)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    c->title_bar_h = c->title_bar_h ? 0 : TITLE_BAR_HEIGHT;
    if(c->title_bar_h)
    {
        create_title_bar(wm, c);
        XMapSubwindows(wm->display, c->frame);
    }
    else
    {
        for(size_t i=0; i<TITLE_BUTTON_N; i++)
            XDestroyWindow(wm->display, c->buttons[i]);
        XDestroyWindow(wm->display, c->title_area);
    }
    update_layout(wm);
}

void focus_desktop(WM *wm, XEvent *e, Func_arg arg)
{
    focus_desktop_n(wm, get_desktop_n(wm, e, arg));
}

void next_desktop(WM *wm, XEvent *e, Func_arg arg)
{
    focus_desktop_n(wm, wm->cur_desktop<DESKTOP_N ? wm->cur_desktop+1 : 1);
}

void prev_desktop(WM *wm, XEvent *e, Func_arg arg)
{
    focus_desktop_n(wm, wm->cur_desktop>1 ? wm->cur_desktop-1 : DESKTOP_N);
}

void move_to_desktop(WM *wm, XEvent *e, Func_arg arg)
{
    unsigned int n=get_desktop_n(wm, e, arg);
    Client *pc=DESKTOP(wm).cur_focus_client, *pp=DESKTOP(wm).prev_focus_client;
    if(n && n!=wm->cur_desktop && pc!=wm->clients)
    {
        pc->desktop_mask=get_desktop_mask(n);
        focus_client(wm, n, pc);
        focus_client(wm, wm->cur_desktop, pp);
        focus_desktop_n(wm, wm->cur_desktop);
    }
}

void all_move_to_desktop(WM *wm, XEvent *e, Func_arg arg)
{
    unsigned int n=get_desktop_n(wm, e, arg);
    if(n)
    {
        Client *pc=DESKTOP(wm).cur_focus_client;
        for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
            c->desktop_mask=get_desktop_mask(n);
        for(unsigned int i=1; i<=DESKTOP_N; i++)
            focus_client(wm, i, i==n ? pc : wm->clients);
        focus_desktop_n(wm, wm->cur_desktop);
    }
}

void change_to_desktop(WM *wm, XEvent *e, Func_arg arg)
{
    move_to_desktop(wm, e, arg);
    focus_desktop(wm, e, arg);
}

void all_change_to_desktop(WM *wm, XEvent *e, Func_arg arg)
{
    all_move_to_desktop(wm, e, arg);
    focus_desktop(wm, e, arg);
}

void attach_to_desktop(WM *wm, XEvent *e, Func_arg arg)
{
    unsigned int n=get_desktop_n(wm, e, arg);
    Client *c=DESKTOP(wm).cur_focus_client;
    if(n && n!=wm->cur_desktop && c!=wm->clients)
    {
        c->desktop_mask |= get_desktop_mask(n);
        focus_client(wm, n, c);
    }
}

void attach_to_all_desktops(WM *wm, XEvent *e, Func_arg arg)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    if(c != wm->clients)
    {
        c->desktop_mask=~0;
        for(unsigned int i=1; i<=DESKTOP_N; i++)
            if(i != wm->cur_desktop)
                focus_client(wm, i, c);
    }
}

void all_attach_to_desktop(WM *wm, XEvent *e, Func_arg arg)
{
    unsigned int n=get_desktop_n(wm, e, arg);
    if(n)
    {
        for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
            c->desktop_mask |= get_desktop_mask(n);
        if(n == wm->cur_desktop)
            focus_desktop_n(wm, wm->cur_desktop);
        else
            focus_client(wm, n, wm->desktop[n-1].cur_focus_client);
    }
}

void enter_and_run_cmd(WM *wm, XEvent *e, Func_arg arg)
{
    show_entry(wm, &wm->run_cmd);
}

void change_wallpaper(WM *wm, XEvent *e, Func_arg arg)
{
    srand((unsigned int)time(NULL));
    unsigned long r1=rand(), r2=rand(), color=(r1<<32)|r2;
    Pixmap pixmap=None;
#ifdef WALLPAPER_PATHS
    File *f;
    for(f=wm->wallpapers->next; f; f=f->next)
        if(!strcmp(f->name, wm->cur_wallpaper->name))
            break;
    if(!f || !f->next)
        f=wm->wallpapers;
    wm->cur_wallpaper=f=f->next;
    if(f)
        pixmap=create_pixmap_from_file(wm, wm->root_win, f->name);
#endif
    update_win_background(wm, wm->root_win, color, pixmap);
#ifdef WALLPAPER_FILENAME
    if(pixmap)
        XFreePixmap(wm->display, pixmap);
#endif
    XClearWindow(wm->display, wm->root_win);
}

void print_screen(WM *wm, XEvent *e, Func_arg arg)
{
    print_area(wm->root_win, 0, 0, wm->screen_width, wm->screen_height);
}

void print_win(WM *wm, XEvent *e, Func_arg arg)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    if(c != wm->clients)
        print_area(c->frame, 0, 0, c->w, c->h);
}

static void print_area(Drawable d, int x, int y, unsigned int w, unsigned int h)
{
    imlib_context_set_drawable(d);
    Imlib_Image image=imlib_create_image_from_drawable(None, x, y, w, h, 0);
    if(image)
    {
        time_t timer=time(NULL), err=-1;
        char name[FILENAME_MAX];
        sprintf(name, "%s/gwm-", SCREENSHOT_PATH);
        if(timer != err)
            strftime(name+strlen(name), FILENAME_MAX, "%Y-%m-%d-%H:%M:%S", localtime(&timer));
        imlib_context_set_image(image);
        imlib_image_set_format(SCREENSHOT_FORMAT);
        sprintf(name+strlen(name), ".%s", SCREENSHOT_FORMAT);
        imlib_save_image(name);
        imlib_free_image();
    }
}
