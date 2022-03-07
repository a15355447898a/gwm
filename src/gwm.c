/* *************************************************************************
 *     gwm.c：實現窗口管理器的主要部分。
 *     版權 (C) 2020-2022 gsm <406643764@qq.com>
 *     本程序為自由軟件：你可以依據自由軟件基金會所發布的第三版或更高版本的
 * GNU通用公共許可證重新發布、修改本程序。
 *     雖然基于使用目的而發布本程序，但不負任何擔保責任，亦不包含適銷性或特
 * 定目標之適用性的暗示性擔保。詳見GNU通用公共許可證。
 *     你應該已經收到一份附隨此程序的GNU通用公共許可證副本。否則，請參閱
 * <http://www.gnu.org/licenses/>。
 * ************************************************************************/

#include "config.h"
#include "gwm.h"
#include "desktop.h"
#include "menu.h"

void (*event_handlers[])(WM *, XEvent *)=
{
    [ButtonPress]       = handle_button_press,
    [ConfigureRequest]  = handle_config_request,
    [EnterNotify]       = handle_enter_notify,
    [Expose]            = handle_expose,
    [FocusIn]           = handle_focus_change,
    [FocusOut]          = handle_focus_change,
    [KeyPress]          = handle_key_press,
    [LeaveNotify]       = handle_leave_notify,
    [MapRequest]        = handle_map_request,
    [MotionNotify]      = handle_motion_notify,
    [UnmapNotify]       = handle_unmap_notify,
    [PropertyNotify]    = handle_property_notify,
};

int main(int argc, char *argv[])
{
    WM wm;
    set_signals();
    clear_zombies(0);
    init_wm(&wm);
    set_wm(&wm);
    handle_events(&wm);
    return EXIT_SUCCESS;
}

void set_signals(void)
{
	if(signal(SIGCHLD, clear_zombies) == SIG_ERR)
    exit_with_perror("不能安裝SIGCHLD信號處理函數");
}

void exit_with_perror(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}

void exit_with_msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

void init_wm(WM *wm)
{
    memset(wm, 0, sizeof(WM));
	if(!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fprintf(stderr, "warning: no locale support\n");
	if(!(wm->display=XOpenDisplay(NULL)))
        exit_with_msg("error: cannot open display");
    init_desktop(wm);
    wm->screen=DefaultScreen(wm->display);
    wm->cur_desktop=1;
    wm->screen_width=DisplayWidth(wm->display, wm->screen);
    wm->screen_height=DisplayHeight(wm->display, wm->screen);
	wm->mod_map=XGetModifierMapping(wm->display);
    wm->root_win=RootWindow(wm->display, wm->screen);
    wm->gc=XCreateGC(wm->display, wm->root_win, 0, NULL);
    wm->focus_mode=DEFAULT_FOCUS_MODE;
}

void init_desktop(WM *wm)
{
    for(size_t i=0; i<DESKTOP_N; i++)
    {
        Desktop *d=wm->desktop+i;
        d->n_main_max=DEFAULT_N_MAIN_MAX;
        d->cur_layout=d->prev_layout=DEFAULT_LAYOUT;
        d->default_area_type=DEFAULT_AREA_TYPE;
        d->main_area_ratio=DEFAULT_MAIN_AREA_RATIO;
        d->fixed_area_ratio=DEFAULT_FIXED_AREA_RATIO;
    }
}

void set_wm(WM *wm)
{
    XSetErrorHandler(x_fatal_handler);
    XSelectInput(wm->display, wm->root_win, ROOT_EVENT_MASK);
    set_icccm_atoms(wm);
    create_font_set(wm);
    create_cursors(wm);
    create_taskbar(wm);
    create_cmd_center(wm);
    create_clients(wm);
    update_layout(wm);
    grab_keys(wm);
    exec(wm, NULL, (Func_arg)SH_CMD("[ -x "AUTOSTART" ] && "AUTOSTART));
}

int x_fatal_handler(Display *display, XErrorEvent *e)
{
    if( e->request_code == X_ChangeWindowAttributes
        && e->error_code == BadAccess)
        exit_with_msg("錯誤：已經有其他窗口管理器在運行！");
    print_fatal_msg(display, e);
	if( e->error_code == BadWindow
        || (e->request_code==X_ConfigureWindow && e->error_code==BadMatch))
		return -1;
    return 0;
}

void set_icccm_atoms(WM *wm)
{
    for(size_t i=0; i<ICCCM_ATOMS_N; i++)
        wm->icccm_atoms[i]=XInternAtom(wm->display, ICCCM_NAMES[i], False);
}

void create_font_set(WM *wm)
{
    char **list, *str;
    int n;
    wm->font_set=XCreateFontSet(wm->display, FONT_SET, &list, &n, &str);
    XFreeStringList(list);
}

void create_cursors(WM *wm)
{
    for(size_t i=0; i<POINTER_ACT_N; i++)
        wm->cursors[i]=XCreateFontCursor(wm->display, CURSORS_SHAPE[i]);
}

void create_taskbar(WM *wm)
{
    Taskbar *b=&wm->taskbar;
    b->x=0, b->y=wm->screen_height-TASKBAR_HEIGHT;
    b->w=wm->screen_width, b->h=TASKBAR_HEIGHT;
    b->win=XCreateSimpleWindow(wm->display, wm->root_win, b->x, b->y,
        b->w, b->h, 0, 0, 0);
    create_taskbar_buttons(wm);
    create_status_area(wm);
    create_icon_area(wm);
    XMapRaised(wm->display, b->win);
    XMapWindow(wm->display, b->win);
    XMapSubwindows(wm->display, b->win);
}

void create_taskbar_buttons(WM *wm)
{
    Taskbar *b=&wm->taskbar;
    for(size_t i=0; i<TASKBAR_BUTTON_N; i++)
    {
        b->buttons[i]=XCreateSimpleWindow(wm->display, b->win,
            TASKBAR_BUTTON_WIDTH*i, 0,
            TASKBAR_BUTTON_WIDTH, TASKBAR_BUTTON_HEIGHT,
            0, 0, NORMAL_TASKBAR_BUTTON_COLOR);
        XSelectInput(wm->display, b->buttons[i], BUTTON_EVENT_MASK);
    }
}

void create_icon_area(WM *wm)
{
    Taskbar *b=&wm->taskbar;
    unsigned int bw=TASKBAR_BUTTON_WIDTH*TASKBAR_BUTTON_N,
        w=b->w-bw-b->status_area_w;
    wm->taskbar.icon_area=XCreateSimpleWindow(wm->display, b->win,
        bw, 0, w, b->h, 0, 0, ICON_AREA_COLOR);
}

void create_status_area(WM *wm)
{
    Taskbar *b=&wm->taskbar;
    b->status_text=get_text_prop(wm, wm->root_win, XA_WM_NAME);
    get_string_size(wm, b->status_text, &b->status_area_w, NULL);
    if(b->status_area_w > STATUS_AREA_WIDTH_MAX)
        b->status_area_w=STATUS_AREA_WIDTH_MAX;
    else if(b->status_area_w == 0)
        b->status_area_w=1;
    wm->taskbar.status_area=XCreateSimpleWindow(wm->display, b->win,
        b->w-b->status_area_w, 0, b->status_area_w, b->h,
        0, 0, STATUS_AREA_COLOR);
    XSelectInput(wm->display, b->status_area, ExposureMask);
}

void create_cmd_center(WM *wm)
{
    unsigned int n=CMD_CENTER_ITEM_N, col=CMD_CENTER_COL,
                 w=CMD_CENTER_ITEM_WIDTH, h=CMD_CENTER_ITEM_HEIGHT,
                 i=TASKBAR_BUTTON_INDEX(CMD_CENTER_ITEM);
    int x=TASKBAR_BUTTON_WIDTH*i, y=wm->screen_height-wm->taskbar.h;
    create_menu(wm, &wm->cmd_center, n, col, w, h, CMD_CENTER_COLOR);
    set_menu_pos_for_click(wm, wm->taskbar.buttons[i], x, y, &wm->cmd_center);
}

void print_fatal_msg(Display *display, XErrorEvent *e)
{
    fprintf(stderr, "X錯誤：資源號=%#lx, 請求量=%lu, 錯誤碼=%d, "
        "主請求碼=%d, 次請求碼=%d\n", e->resourceid, e->serial,
        e->error_code, e->request_code, e->minor_code);
}

/* 生成帶表頭結點的雙向循環鏈表 */
void create_clients(WM *wm)
{
    Window root, parent, *child=NULL;
    unsigned int n;
    Desktop *d=wm->desktop;

    wm->clients=malloc_s(sizeof(Client));
    memset(wm->clients, 0, sizeof(Client));
    for(size_t i=0; i<DESKTOP_N; i++)
        d[i].cur_focus_client=d[i].prev_focus_client=wm->clients;
    wm->clients->area_type=ROOT_AREA;
    wm->clients->win=wm->root_win;
    wm->clients->prev=wm->clients->next=wm->clients;
    if(!XQueryTree(wm->display, wm->root_win, &root, &parent, &child, &n))
        exit_with_msg("錯誤：查詢窗口清單失敗！");
    for(size_t i=0; i<n; i++)
        if(is_wm_win(wm, child[i]))
            add_client(wm, child[i]);
    XFree(child);
}

void *malloc_s(size_t size)
{
    void *p=malloc(size);
    if(p == NULL)
        exit_with_msg("錯誤：申請內存失敗");
    return p;
}

bool is_wm_win(WM *wm, Window win)
{
    XWindowAttributes attr;
    return ( win != wm->taskbar.win
        && XGetWindowAttributes(wm->display, win, &attr)
        && attr.map_state != IsUnmapped
        && !attr.override_redirect);
}

void add_client(WM *wm, Window win)
{
    Client *c=malloc_s(sizeof(Client));
    c->win=win;
    c->title_text=get_text_prop(wm, win, XA_WM_NAME);
    apply_rules(wm, c);
    add_client_node(get_area_head(wm, c->area_type), c);
    fix_area_type(wm);
    set_default_rect(wm, c);
    frame_client(wm, c);
    if(c->area_type == ICONIFY_AREA)
        iconify(wm, c);
    else
        focus_client(wm, wm->cur_desktop, c);
    grab_buttons(wm, c);
    XSelectInput(wm->display, win, PropertyChangeMask);
}

Client *get_area_head(WM *wm, Area_type type)
{
    Client *head=wm->clients;
    for(Client *c=head->next; c!=wm->clients; c=c->next)
        if(is_on_cur_desktop(wm, c) && c->area_type==type)
            return c->prev;
    for(Client *c=head->next; c!=wm->clients; c=c->next)
        if(c->area_type == type)
            return c->prev;
    for(Client *c=head->next; c!=wm->clients; c=c->next)
        if(c->area_type < type)
            head=c;
    return head;
}

void update_layout(WM *wm)
{
    if(wm->clients == wm->clients->next)
        return;

    fix_area_type(wm);
    switch(DESKTOP(wm).cur_layout)
    {
        case FULL: set_full_layout(wm); break;
        case PREVIEW: set_preview_layout(wm); break;
        case STACK: break;
        case TILE: set_tile_layout(wm); break;
    }
    fix_win_rect_for_frame(wm);
    fix_cur_focus_client_rect(wm);
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
        if(is_on_cur_desktop(wm, c))
            move_resize_client(wm, c, NULL);
    if(DESKTOP(wm).cur_layout == FULL)
        XUnmapWindow(wm->display, wm->taskbar.win);
    else if(DESKTOP(wm).prev_layout == FULL)
        XMapWindow(wm->display, wm->taskbar.win);
}

void fix_area_type(WM *wm)
{
    int n=0, m=DESKTOP(wm).n_main_max;
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
    {
        if(is_on_cur_desktop(wm, c))
        {
            n++;
            if(c->area_type==MAIN_AREA && n>m)
                c->area_type=SECOND_AREA;
            else if(c->area_type==SECOND_AREA && n<=m)
                c->area_type=MAIN_AREA;
        }
    }
}

void fix_cur_focus_client_rect(WM *wm)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    if( DESKTOP(wm).prev_layout==FULL && c->area_type==FLOATING_AREA
        && (DESKTOP(wm).cur_layout==TILE || DESKTOP(wm).cur_layout==STACK))
        set_default_rect(wm, c);
}

void set_full_layout(WM *wm)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    c->x=c->y=0, c->w=wm->screen_width, c->h=wm->screen_height;
}

void set_preview_layout(WM *wm)
{
    int n=get_clients_n(wm), i=n-1, rows, cols, w, h,
        sw=wm->screen_width, sh=wm->screen_height, ch=(sh-wm->taskbar.h);
    if(n == 0)
        return;
    /* 行、列数量尽量相近，以保证窗口比例基本不变 */
    for(cols=1; cols<=n && cols*cols<n; cols++)
        ;
    rows=(cols-1)*cols>=n ? cols-1 : cols;
    w=sw/cols, h=ch/rows;
    for(Client *c=wm->clients->prev; c!=wm->clients; c=c->prev)
    {
        if(is_on_cur_desktop(wm, c))
        {
            c->x=(i%cols)*w, c->y=(i/cols)*h;
            /* 下邊和右邊的窗口佔用剩餘空間 */
            c->w=(i+1)%cols ? w-WIN_GAP : w+(sw-w*cols);
            c->h=i<cols*(rows-1) ? h-WIN_GAP : h+(ch-h*rows);
            i--;
        }
    }
}

unsigned int get_clients_n(WM *wm)
{
    unsigned int n=0;
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
        if(is_on_cur_desktop(wm, c))
            n++;
    return n;
}

/* 平鋪布局模式的空間布置如下：
 *     1、屏幕從左至右分別布置次要區域、主要區域、固定區域；
 *     2、同一區域內的窗口均分本區域空間，窗口間隔設置在前窗尾部；
 *     3、在次要區域內設置其與主區域的窗口間隔；
 *     4、在固定區域內設置其與主區域的窗口間隔。 */
void set_tile_layout(WM *wm)
{
    unsigned int n1, n2, n3, i=0, j=0, k=0, mw, sw, fw, mh, sh, fh, h, g=WIN_GAP;

    n1=get_client_count(wm, MAIN_AREA),
    n2=get_client_count(wm, SECOND_AREA),
    n3=get_client_count(wm, FIXED_AREA),
    mw=DESKTOP(wm).main_area_ratio*wm->screen_width;
    fw=wm->screen_width*DESKTOP(wm).fixed_area_ratio;
    sw=wm->screen_width-fw-mw;
    h=wm->screen_height-wm->taskbar.h;
    mh=n1 ? h/n1 : h;
    fh=n3 ? h/n3 : h;
    sh=n2 ? h/n2 : h;
    if(n3 == 0)
        mw+=fw, fw=0;
    if(n2 == 0)
        mw+=sw, sw=0;

    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
    {
        if(is_on_cur_desktop(wm, c))
        {
            if(c->area_type == FIXED_AREA)
                c->x=mw+sw+g, c->y=i++*fh, c->w=fw-g, c->h=fh-g;
            else if(c->area_type == MAIN_AREA)
                c->x=sw, c->y=j++*mh, c->w=mw, c->h=mh-g;
            else if(c->area_type == SECOND_AREA)
                c->x=0, c->y=k++*sh, c->w=sw-g, c->h=sh-g;
        }
    }
}

unsigned int get_client_count(WM *wm, Area_type type)
{
    unsigned int n=0;
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
        if(is_on_cur_desktop(wm, c) && c->area_type==type)
            n++;
    return n;
}

void grab_keys(WM *wm)
{
    unsigned int num_lock_mask=get_num_lock_mask(wm);
    unsigned int masks[]={0, LockMask, num_lock_mask, num_lock_mask|LockMask};
    KeyCode code;
    XUngrabKey(wm->display, AnyKey, AnyModifier, wm->root_win);
    for(size_t i=0; i<ARRAY_NUM(KEYBINDS); i++)
        if((code=XKeysymToKeycode(wm->display, KEYBINDS[i].keysym)))
            for(size_t j=0; j<ARRAY_NUM(masks); j++)
                XGrabKey(wm->display, code, KEYBINDS[i].modifier|masks[j],
                    wm->root_win, True, GrabModeAsync, GrabModeAsync);
}

unsigned int get_num_lock_mask(WM *wm)
{
    size_t i, j;
	XModifierKeymap *m=XGetModifierMapping(wm->display);
    KeyCode code=XKeysymToKeycode(wm->display, XK_Num_Lock);
    if(code)
        for(i=0; i<8; i++)
            for(j=0; j<m->max_keypermod; j++)
                if(m->modifiermap[i*m->max_keypermod+j] == code)
                    break;
    if(j == m->max_keypermod)
        return 0;
    XFreeModifiermap(m);
    return (1<<i);
}
    
void grab_buttons(WM *wm, Client *c)
{
    unsigned int num_lock_mask=get_num_lock_mask(wm),
                 masks[]={0, LockMask, num_lock_mask, num_lock_mask|LockMask};
    Buttonbind *p=BUTTONBINDS;
    XUngrabButton(wm->display, AnyButton, AnyModifier, c->win);
    for(size_t i=0; i<ARRAY_NUM(BUTTONBINDS); i++, p++)
    {
        if(p->widget_type == CLIENT_WIN)
        {
            int m=is_equal_modifier_mask(wm, 0, p->modifier) ?
                GrabModeSync : GrabModeAsync;
            for(size_t j=0; j<ARRAY_NUM(masks); j++)
                XGrabButton(wm->display, p->button, p->modifier|masks[j],
                    c->win, False, BUTTON_MASK, m, m, None, None);
        }
    }
}

void handle_events(WM *wm)
{
	XEvent e;
    XSync(wm->display, False);
	while(!XNextEvent(wm->display, &e))
        if(event_handlers[e.type])
            event_handlers[e.type](wm, &e);
}

void handle_button_press(WM *wm, XEvent *e)
{
    Buttonbind *b=BUTTONBINDS;
    Widget_type type=get_widget_type(wm, e->xbutton.window);
    XUnmapWindow(wm->display, wm->cmd_center.win);
    for(size_t i=0; i<ARRAY_NUM(BUTTONBINDS); i++, b++)
    {
        if(is_func_click(wm, type, b, e))
        {
            focus_clicked_client(wm, e->xbutton.window);
            if(b->func)
                b->func(wm, e, b->arg);
            if(type == CLIENT_WIN)
                XAllowEvents(wm->display, ReplayPointer, CurrentTime);
        }
    }
}
 
void focus_clicked_client(WM *wm, Window win)
{
    Client *c=win_to_client(wm, win);
    if(c == NULL)
        c=win_to_iconic_state_client(wm, win);
    if(c)
        focus_client(wm, wm->cur_desktop, c);
}

bool is_func_click(WM *wm, Widget_type type, Buttonbind *b, XEvent *e)
{
    return (b->widget_type == type 
        && b->button == e->xbutton.button
        && is_equal_modifier_mask(wm, b->modifier, e->xbutton.state));
}

void choose_client(WM *wm, XEvent *e, Func_arg arg)
{
    Client *c=DESKTOP(wm).cur_focus_client;
    if(c->area_type == ICONIFY_AREA)
        move_client(wm, c, get_area_head(wm, c->icon->area_type),
            c->icon->area_type);
    if(DESKTOP(wm).cur_layout == PREVIEW)
        change_layout(wm, e, (Func_arg){.layout=DESKTOP(wm).prev_layout});
}

bool is_equal_modifier_mask(WM *wm, unsigned int m1, unsigned int m2)
{
    return (get_valid_mask(wm, m1) == get_valid_mask(wm, m2));
}

bool is_click_client_in_preview(WM *wm, Widget_type type)
{
    return ((type==CLIENT_WIN || type==CLIENT_FRAME || type==TITLE_AREA)
        && DESKTOP(wm).cur_layout==PREVIEW);
}

void handle_config_request(WM *wm, XEvent *e)
{
    XConfigureRequestEvent cr=e->xconfigurerequest;
    Client *c=win_to_client(wm, cr.window);

    if(c)
        config_managed_client(wm, c);
    else
        config_unmanaged_win(wm, &cr);
}

void config_managed_client(WM *wm, Client *c)
{
    XConfigureEvent ce=
    {
        .type=ConfigureNotify, .display=wm->display, .event=c->win,
        .window=c->win, .x=c->x, .y=c->y, .width=c->w, .height=c->h,
        .border_width=0, .above=None, .override_redirect=False
    };
    XSendEvent(wm->display, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void config_unmanaged_win(WM *wm, XConfigureRequestEvent *e)
{
    XWindowChanges wc=
    {
        .x=e->x, .y=e->y, .width=e->width, .height=e->height,
        .border_width=e->border_width, .sibling=e->above, .stack_mode=e->detail
    };
    XConfigureWindow(wm->display, e->window, e->value_mask, &wc);
}

Client *win_to_client(WM *wm, Window win)
{
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
    {
        if(win==c->win || win==c->frame || win==c->title_area)
            return c;
        for(size_t i=0; i<TITLE_BUTTON_N; i++)
            if(win == c->buttons[i])
                return c;
    }
        
    return NULL;
}

void del_client(WM *wm, Client *c)
{
    if(c)
    {
        if(c->area_type == ICONIFY_AREA)
            del_icon(wm, c);
        del_client_node(c);
        fix_area_type(wm);
        XFree(c->class_hint.res_class);
        XFree(c->class_hint.res_name);
        free(c->title_text);
        free(c);
        for(size_t i=1; i<=DESKTOP_N; i++)
            if(is_on_desktop_n(i, c))
                focus_client(wm, i, NULL);
    }
}

void handle_expose(WM *wm, XEvent *e)
{
    if(e->xexpose.count)
        return;
    Window win=e->xexpose.window;
    Widget_type type=get_widget_type(wm, win);
    if(type == CLIENT_ICON)
        update_icon_text(wm, win);
    else if(IS_TASKBAR_BUTTON(type))
        update_taskbar_button_text(wm, TASKBAR_BUTTON_INDEX(type));
    else if(IS_CMD_CENTER_ITEM(type))
        update_cmd_center_button_text(wm, CMD_CENTER_ITEM_INDEX(type));
    else if(type == STATUS_AREA)
        update_status_area_text(wm);
    else if(type == TITLE_AREA)
        update_title_area_text(wm, win_to_client(wm, win));
    else if(IS_TITLE_BUTTON(type))
        update_title_button_text(wm, win_to_client(wm, win),
            TITLE_BUTTON_INDEX(type));
}

void update_icon_text(WM *wm, Window win)
{
    Client *c=win_to_iconic_state_client(wm, win);
    if(c)
    {
        unsigned int w;
        get_string_size(wm, c->class_name, &w, NULL);
        String_format f={{0, 0, w, c->icon->h},
            CENTER, ICON_CLASS_NAME_FG_COLOR, ICON_CLASS_NAME_BG_COLOR};
        draw_string(wm, c->icon->win, c->class_name, &f);
        if(!c->icon->is_short_text)
        {
            String_format f={{w, 0, c->icon->w-w, c->icon->h},
                CENTER, ICON_TITLE_TEXT_FG_COLOR, ICON_TITLE_TEXT_BG_COLOR};
            draw_string(wm, c->icon->win, c->title_text, &f);
        }
    }
}

void update_taskbar_button_text(WM *wm, size_t index)
{
    String_format f={{0, 0, TASKBAR_BUTTON_WIDTH, TASKBAR_BUTTON_HEIGHT},
            CENTER, TASKBAR_BUTTON_TEXT_COLOR, TASKBAR_BUTTON_TEXT_COLOR};
    if(index == TASKBAR_BUTTON_INDEX(DESKTOP_BUTTON_BEGIN+wm->cur_desktop-1)
        || (index == TASKBAR_BUTTON_INDEX(LAYOUT_BUTTON_BEGIN+DESKTOP(wm).cur_layout)))
        f.bg=CHECKED_TASKBAR_BUTTON_COLOR;
    draw_string(wm, wm->taskbar.buttons[index], TASKBAR_BUTTON_TEXT[index], &f);
}

void update_taskbar_buttons(WM *wm)
{
    for(size_t b=TASKBAR_BUTTON_BEGIN; b<TASKBAR_BUTTON_END; b++)
    {
        size_t i=TASKBAR_BUTTON_INDEX(b);
        String_format f={{0, 0, TASKBAR_BUTTON_WIDTH, TASKBAR_BUTTON_HEIGHT},
                CENTER, TASKBAR_BUTTON_TEXT_COLOR, NORMAL_TASKBAR_BUTTON_COLOR};
        if(b == DESKTOP_BUTTON_BEGIN+wm->cur_desktop-1
            || (b == LAYOUT_BUTTON_BEGIN+DESKTOP(wm).cur_layout))
            f.bg=CHECKED_TASKBAR_BUTTON_COLOR;
        draw_string(wm, wm->taskbar.buttons[i], TASKBAR_BUTTON_TEXT[i], &f);
    }
}

void update_cmd_center_button_text(WM *wm, size_t index)
{
    String_format f={{0, 0, CMD_CENTER_ITEM_WIDTH, CMD_CENTER_ITEM_HEIGHT},
        CENTER_LEFT, CMD_CENTER_ITEM_TEXT_COLOR, CMD_CENTER_ITEM_TEXT_COLOR};
    draw_string(wm, wm->cmd_center.items[index], CMD_CENTER_ITEM_TEXT[index], &f);
}

void handle_focus_change(WM *wm, XEvent *e)
{
    Client *c=win_to_client(wm, e->xfocus.window);
    if(!c)
        c=win_to_iconic_state_client(wm, e->xfocus.window);
    if(c)
        update_client_look(wm, wm->cur_desktop, c);
}

void update_client_look(WM *wm, unsigned int desktop_n, Client *c)
{
    if(c && c!=wm->clients)
    {
        Desktop *d=wm->desktop+desktop_n-1;
        if(c->area_type==ICONIFY_AREA && d->cur_layout!=PREVIEW)
            XSetWindowBorder(wm->display, c->icon->win, c==d->cur_focus_client ?
                CURRENT_ICON_BORDER_COLOR : NORMAL_ICON_BORDER_COLOR);
        else
            update_frame(wm, desktop_n,  c);
    }
}

void handle_key_press(WM *wm, XEvent *e)
{
    int n;
    KeyCode kc=e->xkey.keycode;
	KeySym *keysym=XGetKeyboardMapping(wm->display, kc, 1, &n);
    Keybind *p=KEYBINDS;
	for(size_t i=0; i<ARRAY_NUM(KEYBINDS); i++, p++)
		if( *keysym == p->keysym
            && is_equal_modifier_mask(wm, p->modifier, e->xkey.state)
            && p->func)
            p->func(wm, e, p->arg);
    XFree(keysym);
}
 
unsigned int get_valid_mask(WM *wm, unsigned int mask)
{
    return (mask & ~(LockMask|get_modifier_mask(wm, XK_Num_Lock))
        & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask));
}

unsigned int get_modifier_mask(WM *wm, KeySym key_sym)
{
    KeyCode kc;
    if((kc=XKeysymToKeycode(wm->display, key_sym)) != 0)
    {
        for(size_t i=0; i<8*wm->mod_map->max_keypermod; i++)
            if(wm->mod_map->modifiermap[i] == kc)
                return 1 << (i/wm->mod_map->max_keypermod);
        fprintf(stderr, "錯誤：找不到指定的鍵符號相應的功能轉換鍵！\n");
    }
    else
        fprintf(stderr, "錯誤：指定的鍵符號不存在對應的鍵代碼！\n");
    return 0;
}

void handle_map_request(WM *wm, XEvent *e)
{
    Window win=e->xmaprequest.window;
    XMapWindow(wm->display, win);
    if(is_wm_win(wm, win) && !win_to_client(wm, win))
    {
        add_client(wm, win);
        update_layout(wm);
        DESKTOP(wm).default_area_type=DEFAULT_AREA_TYPE;
    }
}

/* 對已經映射的窗口重設父窗口會依次執行以下操作：
 *     1、自動解除映射該窗口，原父窗口可以收到该UnmapNotify事件；
 *     2、把該窗口從窗口層次結構中移走；
 *     3、把該窗口插入到新父窗口的子窗口堆疊頂部；
 *     4、產生ReparentNotify事件；
 *     5、自動重新映射該窗口，新父窗口可以收到该MapNotify事件。
 * 因爲只在接收MapRequest事件時才考慮添加client，而在接受MapNotify事件時沒
 * 考慮，從而保證不會重復添加，所以相應地，重設父窗口產生UnmapNotify事件時
 * ，也不重復刪除client。重設父窗口產生UnmapNotify事件時，xunmap.event等於
 * 原父窗口。銷毀窗口產生UnmapNotify事件時，xunmap.event等於新父窗口。*/
void handle_unmap_notify(WM *wm, XEvent *e)
{
    Client *c=win_to_client(wm, e->xunmap.window);
    if(c && e->xunmap.event==c->frame && e->xunmap.window==c->win)
    {
        XUnmapWindow(wm->display, c->frame);
        del_client(wm, c);
        update_layout(wm);
    }
}

void handle_motion_notify(WM *wm, XEvent *e)
{
    Window win=e->xmotion.window;
    if(win==wm->root_win)
    {
        if(is_layout_adjust_area(wm, win, e->xmotion.x))
            XDefineCursor(wm->display, win, wm->cursors[ADJUST_LAYOUT_RATIO]);
        else
            XUndefineCursor(wm->display, win);
    }
}

void handle_property_notify(WM *wm, XEvent *e)
{
    Window win=e->xproperty.window;
    char *s;
    if(e->xproperty.atom==XA_WM_NAME && (s=get_text_prop(wm, win, XA_WM_NAME)))
    {
        Client *c;
        if((c=win_to_client(wm, win)) && win==c->win)
        {
            free(c->title_text);
            c->title_text=s;
            update_title_area_text(wm, c);
        }
        else if(win == wm->root_win)
        {
            unsigned int w, bw=TASKBAR_BUTTON_WIDTH*TASKBAR_BUTTON_N;
            Taskbar *b=&wm->taskbar;
            free(b->status_text);
            get_string_size(wm, s, &w, NULL);
            if(w > STATUS_AREA_WIDTH_MAX)
                w=STATUS_AREA_WIDTH_MAX;
            if(w != b->status_area_w)
            {
                XMoveResizeWindow(wm->display, b->status_area, b->w-w, 0, w, b->h);
                XMoveResizeWindow(wm->display, b->icon_area, bw, 0, b->w-bw-w, b->h);
            }
            b->status_text=s;
            b->status_area_w=w;
            update_status_area_text(wm);
        }
    }
}

char *get_text_prop(WM *wm, Window win, Atom atom)
{
    int n;
    char **list=NULL, *result=NULL;
    XTextProperty name;
    if(XGetTextProperty(wm->display, win, &name, atom))
    {
        if(name.encoding == XA_STRING)
            result=copy_string((char *)name.value);
        else if(XmbTextPropertyToTextList(wm->display, &name, &list, &n) == Success)
            result=copy_string(*list), XFreeStringList(list);
        XFree(name.value);
    }
    else
        result=copy_string(win==wm->taskbar.win ? "gwm" : "");
    return result;
}

char *copy_string(const char *s)
{
    return strcpy(malloc_s(strlen(s)+1), s);
}

void draw_string(WM *wm, Drawable d, const char *str, const String_format *f)
{
    if(str && str[0]!='\0')
    {
        unsigned int w=f->r.w, h=f->r.h, lw, lh, n=strlen(str);
        get_string_size(wm, str, &lw, &lh);
        int x=f->r.x, y=f->r.y, sx, sy, cx=x+w/2-lw/2, cy=y+h/2+lh/2,
            left=x, right=x+w-lw, top=y+lh, bottom=y+h;
        switch(f->align)
        {
            case TOP_LEFT: sx=left, sy=top; break;
            case TOP_CENTER: sx=cx, sy=top; break;
            case TOP_RIGHT: sx=right, sy=top; break;
            case CENTER_LEFT: sx=left, sy=cy; break;
            case CENTER: sx=cx, sy=cy; break;
            case CENTER_RIGHT: sx=right, sy=cy; break;
            case BOTTOM_LEFT: sx=left, sy=bottom; break;
            case BOTTOM_CENTER: sx=cx, sy=bottom; break;
            case BOTTOM_RIGHT: sx=right, sy=bottom; break;
        }
        XClearArea(wm->display, d, x, y, w, h, False); 
        if(f->bg != f->fg)
        {
            XSetForeground(wm->display, wm->gc, f->bg);
            XFillRectangle(wm->display, d, wm->gc, x, y, w, h);
        }
        XSetForeground(wm->display, wm->gc, f->fg);
        XmbDrawString(wm->display, d, wm->font_set, wm->gc, sx, sy, str, n);
    }
}

void get_string_size(WM *wm, const char *str, unsigned int *w, unsigned int *h)
{
    XRectangle ink, logical;
    XmbTextExtents(wm->font_set, str, strlen(str), &ink, &logical);
    if(w)
        *w=logical.width;
    if(h)
        *h=logical.height;
}

void exec(WM *wm, XEvent *e, Func_arg arg)
{
    pid_t pid=fork();
	if(pid == 0)
    {
		if(wm->display)
            close(ConnectionNumber(wm->display));
		if(!setsid())
            perror("未能成功地爲命令創建新會話：");
		if(execvp(arg.cmd[0], arg.cmd) == -1)
            exit_with_perror("命令執行錯誤：");
    }
    else if(pid == -1)
        perror("未能成功地爲命令創建新進程：");
}

void key_move_resize_client(WM *wm, XEvent *e, Func_arg arg)
{
    if(DESKTOP(wm).cur_layout==TILE || DESKTOP(wm).cur_layout==STACK)
    {
        int s=MOVE_RESIZE_INC;
        Delta_rect dr[] =
        {
            [UP]          = { 0, -s,  0,  0},
            [DOWN]        = { 0,  s,  0,  0},
            [LEFT]        = {-s,  0,  0,  0},
            [RIGHT]       = { s,  0,  0,  0},
            [LEFT2LEFT]   = {-s,  0,  s,  0},
            [LEFT2RIGHT]  = { s,  0, -s,  0},
            [RIGHT2LEFT]  = { 0,  0, -s,  0},
            [RIGHT2RIGHT] = { 0,  0,  s,  0},
            [UP2UP]       = { 0, -s,  0,  s},
            [UP2DOWN]     = { 0,  s,  0, -s},
            [DOWN2UP]     = { 0,  0,  0, -s},
            [DOWN2DOWN]   = { 0,  0,  0,  s},
        };
        Delta_rect d=dr[arg.direction];
        Client *c=DESKTOP(wm).cur_focus_client;
        if(DESKTOP(wm).cur_layout == TILE)
            move_client(wm, c, get_area_head(wm, FLOATING_AREA), FLOATING_AREA);
        if(is_valid_move_resize(wm, c, &d))
            move_resize_client(wm, c, &d);
    }
}

/* 通過求窗口與屏幕是否有交集來判斷窗口是否已經在屏幕外，即是否合法。
 * 若滿足以下條件，則有交集：窗口與屏幕中心距≤窗口半邊長+屏幕半邊長。
 * 即：|x+w/2-0-sw/2|＜|w/2+sw/2| 且 |y+h/2-0-sh/2|＜|h/2+sh/2|。
 * 兩邊同乘以2，得：|2*x+w-sw|＜|w+sw| 且 |2*y+h-sh|＜|h+sh|。
 */
bool is_valid_move_resize(WM *wm, Client *c, Delta_rect *d)
{
    int x=c->x+d->dx, y=c->y+d->dy, w=c->w+d->dw, h=c->h+d->dh,
        sw=wm->screen_width, sh=wm->screen_height, s=MOVE_RESIZE_INC;
    return w>=s && h>=s && abs(2*x+w-sw)<w+sw && abs(2*y+h-sh)<h+sh;
}

void quit_wm(WM *wm, XEvent *e, Func_arg arg)
{
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
        del_client(wm, c);
    XDestroyWindow(wm->display, wm->taskbar.win);
    free(wm->taskbar.status_text);
    XFreeFontSet(wm->display, wm->font_set);
    for(size_t i=0; i<POINTER_ACT_N; i++)
        XFreeCursor(wm->display, wm->cursors[i]);
    XSetInputFocus(wm->display, wm->root_win, RevertToPointerRoot, CurrentTime);
    XClearWindow(wm->display, wm->root_win);
    XFlush(wm->display);
    XCloseDisplay(wm->display);
    clear_zombies(0);
    exit(EXIT_SUCCESS);
}

void close_client(WM *wm, XEvent *e, Func_arg arg)
{
    /* 刪除窗口會產生UnmapNotify事件，處理該事件時再刪除框架 */
    Client *c=DESKTOP(wm).cur_focus_client;
    if( c != wm->clients
        && !send_event(wm, wm->icccm_atoms[WM_DELETE_WINDOW], c))
        XDestroyWindow(wm->display, c->win);
}

void close_all_clients(WM *wm, XEvent *e, Func_arg arg)
{
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
        if(!send_event(wm, wm->icccm_atoms[WM_DELETE_WINDOW], c))
            XDestroyWindow(wm->display, c->win);
}

bool send_event(WM *wm, Atom protocol, Client *c)
{
	int i, n;
	Atom *protocols;

	if(XGetWMProtocols(wm->display, c->win, &protocols, &n))
    {
        XEvent event;
        for(i=0; i<n && protocols[i]!=protocol; i++)
            ;
		XFree(protocols);
        if(i < n)
        {
            event.type=ClientMessage;
            event.xclient.window=c->win;
            event.xclient.message_type=wm->icccm_atoms[WM_PROTOCOLS];
            event.xclient.format=32;
            event.xclient.data.l[0]=protocol;
            event.xclient.data.l[1]=CurrentTime;
            XSendEvent(wm->display, c->win, False, NoEventMask, &event);
        }
        return i<n;
	}
    return false;
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

/* 取得存儲結構意義上的下一個客戶窗口 */
Client *get_next_client(WM *wm, Client *c)
{
    for(Client *p=c->next; p!=wm->clients; p=p->next)
        if(is_on_cur_desktop(wm, p))
            return p;
    return NULL;
}

/* 取得存儲結構意義上的上一個客戶窗口 */
Client *get_prev_client(WM *wm, Client *c)
{
    for(Client *p=c->prev; p!=wm->clients; p=p->prev)
        if(is_on_cur_desktop(wm, p))
            return p;
    return NULL;
}

void focus_client(WM *wm, unsigned int desktop_n, Client *c)
{
    update_focus_client_pointer(wm, desktop_n, c);

    Desktop *d=wm->desktop+desktop_n-1;
    Client *pc=d->cur_focus_client;

    if(desktop_n == wm->cur_desktop)
        XSetInputFocus(wm->display, pc->area_type==ICONIFY_AREA ? pc->icon->win : pc->win, RevertToPointerRoot, CurrentTime);
    if(pc->area_type!=ICONIFY_AREA || d->cur_layout==PREVIEW)
        raise_client(wm, desktop_n);
}

void update_focus_client_pointer(WM *wm, unsigned int desktop_n, Client *c)
{
    Desktop *desktop=wm->desktop+desktop_n-1;
    Client **pp=&desktop->prev_focus_client, **pc=&desktop->cur_focus_client;
    if(!c)
    {
        if(!is_normal_client(wm, desktop_n, *pp))
            *pp=wm->clients;
        if(!is_normal_client(wm, desktop_n, *pc))
            *pc=*pp;
    }
    else if(c != *pc)
        *pp=*pc, *pc=c;
}

bool is_normal_client(WM *wm, unsigned int desktop_n, Client *c)
{
    for(Client *p=wm->clients->next; p!=wm->clients; p=p->next)
        if(p==c && p->area_type!=ICONIFY_AREA && is_on_desktop_n(desktop_n, p))
            return true;
    return false;
}

void change_layout(WM *wm, XEvent *e, Func_arg arg)
{
    Layout *cl=&DESKTOP(wm).cur_layout, *pl=&DESKTOP(wm).prev_layout;
    if(*cl != arg.layout)
    {
        Display *d=wm->display;
        *pl=*cl, *cl=arg.layout;
        if(*pl == PREVIEW)
            for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
                if(is_on_cur_desktop(wm, c) && c->area_type==ICONIFY_AREA)
                    XMapWindow(d, c->icon->win), XUnmapWindow(d, c->frame);
        if(*cl == PREVIEW)
            for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
                if(is_on_cur_desktop(wm, c) && c->area_type==ICONIFY_AREA)
                    XMapWindow(d, c->frame), XUnmapWindow(d, c->icon->win);
        update_layout(wm);
        update_title_bar_layout(wm);
        update_taskbar_buttons(wm);
    }
}

void update_title_bar_layout(WM *wm)
{
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
    {
        if(c->title_bar_h && is_on_cur_desktop(wm, c))
        {
            Rect r=get_title_area_rect(wm, c);
            XResizeWindow(wm->display, c->title_area, r.w, r.h);
        }
    }
}

bool grab_pointer(WM *wm, XEvent *e)
{
    Cursor gc;
    XButtonEvent *be=&e->xbutton;
    Client *c=win_to_client(wm, be->window);
    Move_info m={be->x_root, be->y_root, 0, 0};
    if(DESKTOP(wm).cur_layout==TILE && be->window==wm->root_win)
        gc=wm->cursors[ADJUST_LAYOUT_RATIO];
    else if(c)
    {
        if(be->window == c->title_area)
            gc=wm->cursors[MOVE];
        else if(be->window == c->frame)
            gc=wm->cursors[get_resize_act(c, &m)];
    }
    else
        return false;
    return XGrabPointer(wm->display, wm->root_win, False, POINTER_MASK,
        GrabModeAsync, GrabModeAsync, None, gc, CurrentTime) == GrabSuccess;
}

void apply_rules(WM *wm, Client *c)
{
    c->area_type=DESKTOP(wm).default_area_type;
    c->border_w=BORDER_WIDTH;
    c->title_bar_h=TITLE_BAR_HEIGHT;
    c->desktop_mask=get_desktop_mask(wm->cur_desktop);
    if(!XGetClassHint(wm->display, c->win, &c->class_hint))
        c->class_hint.res_class=c->class_hint.res_name=NULL, c->class_name="?";
    else
    {
        Rule *r=RULES;
        c->class_name=c->class_hint.res_class;
        for(size_t i=0; i<ARRAY_NUM(RULES); i++, r++)
        {
            if(have_rule(r, c))
            {
                c->area_type=r->area_type;
                c->border_w=r->border_w;
                c->title_bar_h=r->title_bar_h;
                c->desktop_mask = r->desktop_mask ?
                    r->desktop_mask : get_desktop_mask(wm->cur_desktop);
                if(r->class_alias)
                    c->class_name=r->class_alias;
            }
        }
    }
}

bool have_rule(Rule *r, Client *c)
{
    const char *pc=r->app_class, *pn=r->app_name;
    return ((pc && (strstr(c->class_hint.res_class, pc) || strcmp(pc, "*")==0))
        || ((pn && (strstr(c->class_hint.res_name, pn) || strcmp(pc, "*")==0))));
}

void set_default_rect(WM *wm, Client *c)
{
    c->w=wm->screen_width/4, c->h=wm->screen_height/4,
    c->x=wm->screen_width/2-c->w/2, c->y=wm->screen_height/2-c->h/2;
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
    if(DESKTOP(wm).cur_layout==TILE && get_client_count(wm, SECOND_AREA))
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
    if(DESKTOP(wm).cur_layout==TILE && get_client_count(wm, FIXED_AREA))
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
    if(c!=wm->clients && (l==TILE || (l==STACK && t==ICONIFY_AREA)))
        move_client(wm, c, get_area_head(wm, t), t);
}

void del_client_node(Client *c)
{
    c->prev->next=c->next;
    c->next->prev=c->prev;
}

void add_client_node(Client *head, Client *c)
{
    c->prev=head;
    c->next=head->next;
    head->next=c;
    c->next->prev=c;
}

void pointer_change_area(WM *wm, XEvent *e, Func_arg arg)
{
    Client *from=DESKTOP(wm).cur_focus_client, *to;
    if(DESKTOP(wm).cur_layout==TILE && from!=wm->clients && grab_pointer(wm, e))
    {
        XEvent ev;
        do
        {
            XMaskEvent(wm->display, ROOT_EVENT_MASK, &ev);
            if(event_handlers[ev.type])
                event_handlers[ev.type](wm, &ev);
        }while((ev.type!=ButtonRelease || ev.xbutton.button!=e->xbutton.button));
        XUngrabPointer(wm->display, CurrentTime);

        /* 因爲窗口不隨定位器動態移動，故釋放按鈕時定位器已經在按下按鈕時
         * 定位器所在的窗口的外邊。因此，接收事件的是根窗口。 */
        Window win=ev.xbutton.window, subw=ev.xbutton.subwindow;
        to=win_to_client(wm, subw);
        if(ev.xbutton.x == 0)
            move_client(wm, from, get_area_head(wm, SECOND_AREA), SECOND_AREA);
        else if(ev.xbutton.x == wm->screen_width-1)
            move_client(wm, from, get_area_head(wm, FIXED_AREA), FIXED_AREA);
        else if(ev.xbutton.y == 0)
            maximize_client(wm, NULL, arg);
        else if(subw == wm->taskbar.win)
            move_client(wm, from, get_area_head(wm, ICONIFY_AREA), ICONIFY_AREA);
        else if(to)
            move_client(wm, from, to, to->area_type);
        else if(win==wm->root_win && subw==None)
            move_client(wm, from, get_area_head(wm, MAIN_AREA), MAIN_AREA);
    }
}

int compare_client_order(WM *wm, Client *c1, Client *c2)
{
    if(c1 == c2)
        return 0;
    for(Client *c=c1; c!=wm->clients; c=c->next)
        if(c == c2)
            return -1;
    return 1;
}

void move_client(WM *wm, Client *from, Client *to, Area_type type)
{
    if(move_client_node(wm, from, to, type))
    {
        if(from->area_type == ICONIFY_AREA)
            deiconify(wm, from);
        if(type == ICONIFY_AREA)
            iconify(wm, from);
        from->area_type=type;
        fix_area_type(wm);
        raise_client(wm, wm->cur_desktop);
        update_layout(wm);
    }
}

bool move_client_node(WM *wm, Client *from, Client *to, Area_type type)
{
    Client *head;
    Area_type ft=from->area_type, tt=to->area_type;
    if(from==wm->clients || (from==to && (tt==type || (ft==MAIN_AREA && type==SECOND_AREA))))
        return false;
    del_client_node(from);
    if(tt == type)
    {
        if((ft==MAIN_AREA && tt==SECOND_AREA)
            || (ft==tt && compare_client_order(wm, from, to)==-1))
            head=to;
        else
            head=to->prev;
    }
    else
    {
        if(ft==MAIN_AREA && type==SECOND_AREA)
            head=to->next;
        else if(from == to)
            head=to->prev;
        else
            head=to;
    }
    add_client_node(head, from);
    return true;
}

void pointer_swap_clients(WM *wm, XEvent *e, Func_arg arg)
{
    XEvent ev;
    Client *from=DESKTOP(wm).cur_focus_client, *to;
    if(DESKTOP(wm).cur_layout!=TILE || from==wm->clients || !grab_pointer(wm, e))
        return;
    do
    {
        XMaskEvent(wm->display, ROOT_EVENT_MASK, &ev);
        if(event_handlers[ev.type])
            event_handlers[ev.type](wm, &ev);
    }while((ev.type!=ButtonRelease || ev.xbutton.button!=e->xbutton.button));
    XUngrabPointer(wm->display, CurrentTime);

    /* 因爲窗口不隨定位器動態移動，故釋放按鈕時定位器已經在按下按鈕時
     * 定位器所在的窗口的外邊。因此，接收事件的是根窗口。 */
    if((to=win_to_client(wm, ev.xbutton.subwindow)))
        swap_clients(wm, from, to);
}

void swap_clients(WM *wm, Client *a, Client *b)
{
    if(a != b)
    {
        Client *aprev=a->prev, *bprev=b->prev;
        Area_type atype=a->area_type, btype=b->area_type;
        del_client_node(a), add_client_node(compare_client_order(wm, a, b)==-1 ? b : bprev, a);
        if(aprev!=b && bprev!=a) //不相邻
            del_client_node(b), add_client_node(aprev, b);
        a->area_type=btype, b->area_type=atype;
        raise_client(wm, wm->cur_desktop);
        update_layout(wm);
    }
}

/* 僅在移動窗口、聚焦窗口時才有可能需要提升 */
void raise_client(WM *wm, unsigned int desktop_n)
{
    Client *c=wm->desktop[desktop_n-1].cur_focus_client;
    if(c != wm->clients)
    {
        Window wins[]={wm->taskbar.win, c->frame};
        if(is_on_desktop_n(desktop_n, c) && c->area_type==FLOATING_AREA)
            XRaiseWindow(wm->display, c->frame);
        else
            XRestackWindows(wm->display, wins, ARRAY_NUM(wins));
    }
}

void frame_client(WM *wm, Client *c)
{
    Rect fr=get_frame_rect(c);
    c->frame=XCreateSimpleWindow(wm->display, wm->root_win, fr.x, fr.y,
        fr.w, fr.h, c->border_w, CURRENT_BORDER_COLOR, 0);
    XSelectInput(wm->display, c->frame, FRAME_EVENT_MASK);
    if(c->title_bar_h)
        create_title_bar(wm, c);
    XAddToSaveSet(wm->display, c->win);
    XReparentWindow(wm->display, c->win, c->frame,
        0, c->title_bar_h);
    XMapWindow(wm->display, c->frame);
    XMapSubwindows(wm->display, c->frame);
}

void create_title_bar(WM *wm, Client *c)
{
    Rect tr=get_title_area_rect(wm, c);
    for(size_t i=0; i<TITLE_BUTTON_N; i++)
    {
        Rect br=get_button_rect(c, i);
        c->buttons[i]=XCreateSimpleWindow(wm->display, c->frame,
            br.x, br.y, br.w, br.h, 0, 0, CURRENT_TITLE_BUTTON_COLOR);
        XSelectInput(wm->display, c->buttons[i], BUTTON_EVENT_MASK);
    }
    c->title_area=XCreateSimpleWindow(wm->display, c->frame,
        tr.x, tr.y, tr.w, tr.h, 0, 0, CURRENT_TITLE_AREA_COLOR);
    XSelectInput(wm->display, c->title_area, TITLE_AREA_EVENT_MASK);
}

Rect get_frame_rect(Client *c)
{
    return (Rect){c->x-c->border_w, c->y-c->title_bar_h-c->border_w,
        c->w, c->h+c->title_bar_h};
}

Rect get_title_area_rect(WM *wm, Client *c)
{
    int buttons_n[]={[FULL]=0, [PREVIEW]=1, [STACK]=3, [TILE]=7};
    return (Rect){0, 0, c->w-
        TITLE_BUTTON_WIDTH*buttons_n[DESKTOP(wm).cur_layout], c->title_bar_h};
}

Rect get_button_rect(Client *c, size_t index)
{
    return (Rect){c->w-TITLE_BUTTON_WIDTH*(TITLE_BUTTON_N-index),
        (c->title_bar_h-TITLE_BUTTON_HEIGHT)/2,
        TITLE_BUTTON_WIDTH, TITLE_BUTTON_HEIGHT};
}

void update_title_area_text(WM *wm, Client *c)
{
    if(c->title_bar_h)
    {
        String_format f={get_title_area_rect(wm, c),
            CENTER_LEFT, TITLE_TEXT_COLOR, TITLE_TEXT_COLOR};
        draw_string(wm, c->title_area, c->title_text, &f);
    }
}

void update_title_button_text(WM *wm, Client *c, size_t index)
{
    if(c->title_bar_h)
    {
        String_format f={{0, 0, TITLE_BUTTON_WIDTH, TITLE_BUTTON_HEIGHT},
            CENTER, TITLE_BUTTON_TEXT_COLOR, TITLE_BUTTON_TEXT_COLOR};
        draw_string(wm, c->buttons[index], TITLE_BUTTON_TEXT[index], &f);
    }
}

void update_status_area_text(WM *wm)
{
    Taskbar *b=&wm->taskbar;
    String_format f={{0, 0, b->status_area_w, b->h},
        CENTER_RIGHT, STATUS_AREA_TEXT_COLOR, STATUS_AREA_TEXT_COLOR};
    draw_string(wm, b->status_area, b->status_text, &f);
}

void move_resize_client(WM *wm, Client *c, const Delta_rect *d)
{
    if(d)
        c->x+=d->dx, c->y+=d->dy, c->w+=d->dw, c->h+=d->dh;
    Rect fr=get_frame_rect(c), tr=get_title_area_rect(wm, c);
    XMoveResizeWindow(wm->display, c->win,
        0, c->title_bar_h, c->w, c->h);
    if(c->title_bar_h)
    {
        for(size_t i=0; i<TITLE_BUTTON_N; i++)
        {
            Rect br=get_button_rect(c, i);
            XMoveWindow(wm->display, c->buttons[i], br.x, br.y);
        }
        XResizeWindow(wm->display, c->title_area, tr.w, tr.h);
    }
    XMoveResizeWindow(wm->display, c->frame, fr.x, fr.y, fr.w, fr.h);
}

void fix_win_rect_for_frame(WM *wm)
{
    if(DESKTOP(wm).cur_layout==FULL || DESKTOP(wm).cur_layout==STACK)
        return;
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
        if(should_fix_win_rect(wm, c))
            c->x+=c->border_w, c->y+=c->title_bar_h+c->border_w,
            c->w-=2*c->border_w, c->h-=c->title_bar_h+2*c->border_w;
}

bool should_fix_win_rect(WM *wm, Client *c)
{
    Area_type t=c->area_type;
    return (is_on_cur_desktop(wm, c)
        && (DESKTOP(wm).cur_layout==PREVIEW || (DESKTOP(wm).cur_layout==TILE
        && (t==MAIN_AREA || t==SECOND_AREA || t==FIXED_AREA))));
}

void update_frame(WM *wm, unsigned int desktop_n, Client *c)
{
    bool flag=(c==wm->desktop[desktop_n-1].cur_focus_client);
    if(c->border_w)
        XSetWindowBorder(wm->display, c->frame, flag ?
            CURRENT_BORDER_COLOR : NORMAL_BORDER_COLOR);
    if(c->title_bar_h)
    {
        update_win_background(wm, c->title_area, flag ?
            CURRENT_TITLE_AREA_COLOR : NORMAL_TITLE_AREA_COLOR);
        for(size_t i=0; i<TITLE_BUTTON_N; i++)
            update_win_background(wm, c->buttons[i], flag ?
                CURRENT_TITLE_BUTTON_COLOR : NORMAL_TITLE_BUTTON_COLOR);
    }
}

/* 在調用XSetWindowBackground之後，在收到下一個顯露事件或調用XClearWindow
 * 之前，背景不變。此處用發送顯露事件的方式使背景設置立即生效。*/
void update_win_background(WM *wm, Window win, unsigned long color)
{
    XEvent event={.xexpose={.type=Expose, .window=win}};
    XSetWindowBackground(wm->display, win, color);
    XSendEvent(wm->display, win, False, NoEventMask, &event);
}

Widget_type get_widget_type(WM *wm, Window win)
{
    Widget_type type;
    Client *c;
    if(win == wm->root_win)
        return ROOT_WIN;
    for(type=TASKBAR_BUTTON_BEGIN; type<=TASKBAR_BUTTON_END; type++)
        if(win == wm->taskbar.buttons[TASKBAR_BUTTON_INDEX(type)])
            return type;
    if(win == wm->taskbar.status_area)
        return STATUS_AREA;
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
        if(c->area_type==ICONIFY_AREA && win==c->icon->win)
            return CLIENT_ICON;
    for(type=CMD_CENTER_ITEM_BEGIN; type<=CMD_CENTER_ITEM_END; type++)
        if(win == wm->cmd_center.items[CMD_CENTER_ITEM_INDEX(type)])
            return type;
    if((c=win_to_client(wm, win)))
    {
        if(win == c->win)
            return CLIENT_WIN;
        else if(win == c->frame)
            return CLIENT_FRAME;
        else if(win == c->title_area)
            return TITLE_AREA;
        else
            for(type=TITLE_BUTTON_BEGIN; type<=TITLE_BUTTON_END; type++)
                if(win == c->buttons[TITLE_BUTTON_INDEX(type)])
                    return type;
    }
    return UNDEFINED;
}

void handle_enter_notify(WM *wm, XEvent *e)
{
    int x=e->xcrossing.x_root, y=e->xcrossing.y_root;
    Window win=e->xcrossing.window;
    Client *c=win_to_client(wm, win);
    Widget_type type=get_widget_type(wm, win);
    if(wm->focus_mode==ENTER_FOCUS && c)
        focus_client(wm, wm->cur_desktop, c);
    if(is_layout_adjust_area(wm, win, x))
        XDefineCursor(wm->display, win, wm->cursors[ADJUST_LAYOUT_RATIO]);
    else if(type == ROOT_WIN)
        XDefineCursor(wm->display, win, wm->cursors[NO_OP]);
    else if(type == STATUS_AREA)
        XDefineCursor(wm->display, wm->taskbar.status_area, wm->cursors[NO_OP]);
    else if(IS_TASKBAR_BUTTON(type))
        hint_enter_taskbar_button(wm, type);
    else if(IS_CMD_CENTER_ITEM(type))
        hint_enter_cmd_center_button(wm, type);
    else if(type == CLIENT_FRAME)
        hint_resize_client(wm, c, x, y);
    else if(type == TITLE_AREA)
        XDefineCursor(wm->display, c->title_area, wm->cursors[MOVE]);
    else if(IS_TITLE_BUTTON(type))
        hint_enter_title_button(wm, c, type);
}

void hint_enter_taskbar_button(WM *wm, Widget_type type)
{
    Window win=wm->taskbar.buttons[TASKBAR_BUTTON_INDEX(type)];
    XDefineCursor(wm->display, win, wm->cursors[NO_OP]);
    update_win_background(wm, win, ENTERED_TASKBAR_BUTTON_COLOR);
}

void hint_enter_cmd_center_button(WM *wm, Widget_type type)
{
    Window win=wm->cmd_center.items[CMD_CENTER_ITEM_INDEX(type)];
    XDefineCursor(wm->display, win, wm->cursors[NO_OP]);
    update_win_background(wm, win, ENTERED_CMD_CENTER_ITEM_COLOR);
}

void hint_enter_title_button(WM *wm, Client *c, Widget_type type)
{
    Window win=c->buttons[TITLE_BUTTON_INDEX(type)];
    XDefineCursor(wm->display, win, wm->cursors[NO_OP]);
    update_win_background(wm, win, type==CLOSE_BUTTON ?
        ENTERED_CLOSE_BUTTON_COLOR : ENTERED_TITLE_BUTTON_COLOR);
}

void hint_resize_client(WM *wm, Client *c, int x, int y)
{
    Move_info m={x, y, 0, 0};
    XDefineCursor(wm->display, c->frame, wm->cursors[get_resize_act(c, &m)]);
}

bool is_layout_adjust_area(WM *wm, Window win, int x)
{
    return (DESKTOP(wm).cur_layout==TILE && win==wm->root_win
        && (is_main_sec_gap(wm, x) || is_main_fix_gap(wm, x)));
}

void handle_leave_notify(WM *wm, XEvent *e)
{
    Window win=e->xcrossing.window;
    Widget_type type=get_widget_type(wm, win);
    if(IS_TASKBAR_BUTTON(type))
        hint_leave_taskbar_button(wm, type);
    else if(IS_CMD_CENTER_ITEM(type))
        hint_leave_cmd_center_button(wm, type);
    else if(IS_TITLE_BUTTON(type))
        hint_leave_title_button(wm, win_to_client(wm, win), type);
    XUndefineCursor(wm->display, win);
}

void hint_leave_taskbar_button(WM *wm, Widget_type type)
{
    if(type !=DESKTOP_BUTTON_BEGIN+wm->cur_desktop-1)
    {
        Window win=wm->taskbar.buttons[TASKBAR_BUTTON_INDEX(type)];
        update_win_background(wm, win, NORMAL_TASKBAR_BUTTON_COLOR);
    }
}

void hint_leave_cmd_center_button(WM *wm, Widget_type type)
{
    Window win=wm->cmd_center.items[CMD_CENTER_ITEM_INDEX(type)];
    update_win_background(wm, win, NORMAL_CMD_CENTER_ITEM_COLOR);
}

void hint_leave_title_button(WM *wm, Client *c, Widget_type type)
{
    Window win=c->buttons[TITLE_BUTTON_INDEX(type)];
    update_win_background(wm, win, c==DESKTOP(wm).cur_focus_client ?
        CURRENT_TITLE_BUTTON_COLOR : NORMAL_TITLE_BUTTON_COLOR);
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

void iconify(WM *wm, Client *c)
{
    create_icon(wm, c);
    XSelectInput(wm->display, c->icon->win, ICON_EVENT_MASK);
    XMapWindow(wm->display, c->icon->win);
    XUnmapWindow(wm->display, c->frame);
    if(c == DESKTOP(wm).cur_focus_client)
    {
        focus_client(wm, wm->cur_desktop, NULL);
        update_frame(wm, wm->cur_desktop, c);
    }
}

void create_icon(WM *wm, Client *c)
{
    Icon *p=c->icon=malloc_s(sizeof(Icon));
    c->icon->w=1, c->icon->h=ICON_HEIGHT;
    c->icon->y=wm->taskbar.h/2-c->icon->h/2-ICON_BORDER_WIDTH;
    p->area_type=c->area_type==ICONIFY_AREA ? DEFAULT_AREA_TYPE : c->area_type;
    c->area_type=ICONIFY_AREA;
    p->win=XCreateSimpleWindow(wm->display, wm->taskbar.icon_area, p->x, p->y,
        p->w, p->h, ICON_BORDER_WIDTH, NORMAL_ICON_BORDER_COLOR, ICON_BG_COLOR);
    update_icon_area(wm);
}

bool have_same_class_icon_client(WM *wm, Client *c)
{
    for(Client *p=wm->clients->next; p!=wm->clients; p=p->next)
        if( p!=c && is_on_cur_desktop(wm, p) && p->area_type==ICONIFY_AREA
            && !strcmp(p->class_hint.res_class, c->class_hint.res_class))
            return true;
    return false;
}

void update_icon_area(WM *wm)
{
    unsigned int x=0, w=0;
    for(Client *c=wm->clients->prev; c!=wm->clients; c=c->prev)
    {
        if(is_on_cur_desktop(wm, c) && c->area_type==ICONIFY_AREA)
        {
            Icon *i=c->icon;
            get_string_size(wm, c->class_name, &i->w, NULL);
            if(have_same_class_icon_client(wm, c))
            {
                get_string_size(wm, c->title_text, &w, NULL);
                i->w+=w;
                i->is_short_text=false;
            }
            else
                i->is_short_text=true;
            i->x=x;
            x+=i->w+ICONS_SPACE;
            XMoveResizeWindow(wm->display, i->win, i->x, i->y, i->w, i->h); 
        }
    }
}

void deiconify(WM *wm, Client *c)
{
    if(c)
    {
        XMapWindow(wm->display, c->frame);
        del_icon(wm, c);
        focus_client(wm, wm->cur_desktop, c);
    }
}

void del_icon(WM *wm, Client *c)
{
    XDestroyWindow(wm->display, c->icon->win);
    c->area_type=c->icon->area_type;
    free(c->icon);
    update_icon_area(wm);
}

void pointer_move_client(WM *wm, XEvent *e, Func_arg arg)
{
    Layout layout=DESKTOP(wm).cur_layout;
    if(layout==FULL || layout==PREVIEW || !grab_pointer(wm, e))
        return;
    XEvent ev;
    Move_info m={e->xbutton.x_root, e->xbutton.y_root, 0, 0};
    Delta_rect d={0, 0, 0, 0};
    Client *c=DESKTOP(wm).cur_focus_client;

    do /* 因設置了獨享定位器且XMaskEvent會阻塞，故應處理按、放按鈕之間的事件 */
    {
        XMaskEvent(wm->display, ROOT_EVENT_MASK, &ev);
        if(ev.type == MotionNotify)
        {
            m.nx=ev.xmotion.x, m.ny=ev.xmotion.y, d.dx=m.nx-m.ox, d.dy=m.ny-m.oy;
            if(c->area_type!=FLOATING_AREA && layout==TILE)
                move_client(wm, c, get_area_head(wm, FLOATING_AREA), FLOATING_AREA);
            move_resize_client(wm, c, &d);
            m.ox=m.nx, m.oy=m.ny;
        }
        else if(event_handlers[ev.type])
            event_handlers[ev.type](wm, &ev);
    }while(!(ev.type==ButtonRelease && ev.xbutton.button==e->xbutton.button));
    XUngrabPointer(wm->display, CurrentTime);
}

void pointer_resize_client(WM *wm, XEvent *e, Func_arg arg)
{
    Layout layout=DESKTOP(wm).cur_layout;
    if(layout==FULL || layout==PREVIEW || !grab_pointer(wm, e))
        return;
    XEvent ev;
    Move_info m={e->xbutton.x_root, e->xbutton.y_root, 0, 0};
    Delta_rect d;
    Client *c=DESKTOP(wm).cur_focus_client;
    do /* 因設置了獨享定位器且XMaskEvent會阻塞，故應處理按、放按鈕之間的事件 */
    {
        XMaskEvent(wm->display, ROOT_EVENT_MASK, &ev);
        if(ev.type == MotionNotify)
        {
            if(c->area_type!=FLOATING_AREA && layout==TILE)
                move_client(wm, c, get_area_head(wm, FLOATING_AREA), FLOATING_AREA);
            m.nx=ev.xmotion.x, m.ny=ev.xmotion.y;
            d=get_delta_rect(c, &m);
            if(is_valid_move_resize(wm, c, &d))
            {
                move_resize_client(wm, c, &d);
                m.ox=m.nx, m.oy=m.ny;
            }
        }
        else if(event_handlers[ev.type])
            event_handlers[ev.type](wm, &ev);
    }while(!(ev.type==ButtonRelease && ev.xbutton.button==e->xbutton.button));
    XUngrabPointer(wm->display, CurrentTime);
}

Pointer_act get_resize_act(Client *c, const Move_info *m)
{
    int bw=c->border_w, bh=c->title_bar_h, cw=c->w/3, ch=c->h/3, // 窗口角落的寬度和高度
        // 窗口框架左、右橫坐標和上、下縱坐標
        lx=c->x-bw, rx=c->x+c->w+bw, ty=c->y-bh-bw, by=c->y+c->h+bw;

    if(m->ox>=lx && m->ox<lx+bw+cw && m->oy>=ty && m->oy<ty+bw+ch)
        return TOP_LEFT_RESIZE;
    else if(m->ox>=rx-bw-cw && m->ox<rx && m->oy>=ty && m->oy<ty+bw+ch)
        return TOP_RIGHT_RESIZE;
    else if(m->ox>=lx && m->ox<lx+bw+cw && m->oy>=by-bw-ch && m->oy<by)
        return BOTTOM_LEFT_RESIZE;
    else if(m->ox>=rx-bw-cw && m->ox<rx && m->oy>=by-bw-ch && m->oy<by)
        return BOTTOM_RIGHT_RESIZE;
    else if(m->oy>=ty && m->oy<ty+bw+ch)
        return TOP_RESIZE;
    else if(m->oy>=by-bw-ch && m->oy<by)
        return BOTTOM_RESIZE;
    else if(m->ox>=lx && m->ox<lx+bw+cw)
        return LEFT_RESIZE;
    else if(m->ox>=rx-bw-cw && m->ox<rx)
        return RIGHT_RESIZE;
    else
        return NO_OP;
}

Delta_rect get_delta_rect(Client *c, const Move_info *m)
{
    int dx=m->nx-m->ox, dy=m->ny-m->oy;
    Delta_rect dr[] =
    {
        [TOP_LEFT_RESIZE]     = {dx, dy, -dx, -dy},
        [TOP_RIGHT_RESIZE]    = { 0, dy,  dx, -dy},
        [BOTTOM_LEFT_RESIZE]  = {dx,  0, -dx,  dy},
        [BOTTOM_RIGHT_RESIZE] = { 0,  0,  dx,  dy},
        [TOP_RESIZE]          = { 0, dy,   0, -dy},
        [BOTTOM_RESIZE]       = { 0,  0,   0,  dy},
        [LEFT_RESIZE]         = {dx,  0, -dx,   0},
        [RIGHT_RESIZE]        = { 0,  0,  dx,   0},
        [NO_OP]               = { 0,  0,   0,   0},
    };
    return dr[get_resize_act(c, m)];
}

void adjust_layout_ratio(WM *wm, XEvent *e, Func_arg arg)
{
    if( DESKTOP(wm).cur_layout!=TILE
        || !is_layout_adjust_area(wm, e->xbutton.window, e->xbutton.x_root)
        || !grab_pointer(wm, e))
        return;
    int ox=e->xbutton.x_root, nx, dx;
    XEvent ev;
    do /* 因設置了獨享定位器且XMaskEvent會阻塞，故應處理按、放按鈕之間的事件 */
    {
        XMaskEvent(wm->display, ROOT_EVENT_MASK, &ev);
        if(ev.type == MotionNotify)
        {
            nx=ev.xmotion.x, dx=nx-ox;
            if(abs(dx)>=MOVE_RESIZE_INC && change_layout_ratio(wm, ox, nx))
                update_layout(wm), ox=nx;
        }
        else if(event_handlers[ev.type])
            event_handlers[ev.type](wm, &ev);
    }while(!(ev.type==ButtonRelease && ev.xbutton.button==e->xbutton.button));
    XUngrabPointer(wm->display, CurrentTime);
}

bool change_layout_ratio(WM *wm, int ox, int nx)
{
    Desktop *d=&DESKTOP(wm);
    double dr;
    dr=1.0*(nx-ox)/wm->screen_width;
    if(is_main_sec_gap(wm, ox))
        d->main_area_ratio-=dr;
    else if(is_main_fix_gap(wm, ox))
        d->main_area_ratio+=dr, d->fixed_area_ratio-=dr;
    else
        return false;
    return true;
}

bool is_main_sec_gap(WM *wm, int x)
{
    Desktop *d=&DESKTOP(wm);
    unsigned int w=wm->screen_width*(1-d->main_area_ratio-d->fixed_area_ratio);
    return (get_client_count(wm, SECOND_AREA) && x>=w-WIN_GAP && x<w);
}

bool is_main_fix_gap(WM *wm, int x)
{
    unsigned int w=wm->screen_width*(1-DESKTOP(wm).fixed_area_ratio);
    return (get_client_count(wm, FIXED_AREA) && x>=w && x<w+WIN_GAP);
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
}

Client *win_to_iconic_state_client(WM *wm, Window win)
{
    for(Client *c=wm->clients->next; c!=wm->clients; c=c->next)
        if(c->area_type==ICONIFY_AREA && c->icon->win==win)
            return c;
    return NULL;
}

void change_default_area_type(WM *wm, XEvent *e, Func_arg arg)
{
    DESKTOP(wm).default_area_type=arg.area_type;
}

void clear_zombies(int unused)
{
	while(0 < waitpid(-1, NULL, WNOHANG))
        ;
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
