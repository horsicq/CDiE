/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#if defined(_WIN32)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>
#else
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <libnotify/notify.h>
#endif
#include "core/tray.h"
#include "core/i18n.h"

#if defined(_WIN32)

#define WM_TRAYICON      (WM_APP + 1)
#define IDM_TRAY_SHOW    1
#define IDM_TRAY_EXIT    2
#define IDM_TRAY_MONITOR 3

#ifndef NIN_BALLOONUSERCLICK
#define NIN_BALLOONUSERCLICK (WM_USER + 5)
#endif

static int g_enabled, g_added, g_persistent, g_monitor;
static HWND g_hwnd;
static UINT g_taskbarCreated;
static NOTIFYICONDATAW g_nid;
static TrayExitFn g_exit_cb;     static void* g_exit_user;
static TrayClickFn g_click_cb;   static void* g_click_user;
static TrayMonitorFn g_monitor_cb; static void* g_monitor_user;

static const WCHAR* wtr(const char* key, WCHAR* buf, int cap)
{
  if (!MultiByteToWideChar(CP_UTF8, 0, translate(key), -1, buf, cap)) buf[0] = 0;
  return buf;
}

void tray_init(TrayExitFn exit_cb, void* user)
{
  g_exit_cb = exit_cb; g_exit_user = user;
  g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
}
void tray_win32_attach(HWND hwnd) { g_hwnd = hwnd; }
void tray_set_callbacks(TrayClickFn click, void* cu, TrayMonitorFn mon, void* mu)
{
  g_click_cb = click; g_click_user = cu;
  g_monitor_cb = mon; g_monitor_user = mu;
}
void tray_set_enabled(int enabled) { g_enabled = enabled ? 1 : 0; }
int  tray_get_enabled(void) { return g_enabled; }
void tray_set_monitor(int on) { g_monitor = on ? 1 : 0; }
int  tray_get_monitor(void) { return g_monitor; }

static void tray_add(HWND hwnd)
{
  HICON icon;
  if (g_added) return;
  icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
  if (!icon) icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
  if (!icon) icon = LoadIconW(NULL, IDI_APPLICATION);
  ZeroMemory(&g_nid, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAYICON;
  g_nid.hIcon = icon;
  lstrcpynW(g_nid.szTip, L"cdie", ARRAYSIZE(g_nid.szTip));
  g_added = Shell_NotifyIconW(NIM_ADD, &g_nid) ? 1 : 0;
}

void tray_remove(void)
{
  if (!g_added) return;
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  g_added = 0;
}

void tray_set_persistent(int on)
{
  g_persistent = on ? 1 : 0;
  if (!g_hwnd) return;
  if (on) tray_add(g_hwnd);
  else {
    tray_remove();
    if (!IsWindowVisible(g_hwnd)) ShowWindow(g_hwnd, SW_SHOW);
  }
}

void tray_notify(const char* title, const char* text)
{
  NOTIFYICONDATAW n;
  if (!g_hwnd) return;
  tray_add(g_hwnd);
  if (!g_added) return;
  n = g_nid;
  n.uFlags = NIF_INFO;
  n.dwInfoFlags = NIIF_INFO;
  MultiByteToWideChar(CP_UTF8, 0, title, -1, n.szInfoTitle, ARRAYSIZE(n.szInfoTitle));
  MultiByteToWideChar(CP_UTF8, 0, text, -1, n.szInfo, ARRAYSIZE(n.szInfo));
  n.szInfoTitle[ARRAYSIZE(n.szInfoTitle) - 1] = 0;   /* truncated conversions aren't terminated */
  n.szInfo[ARRAYSIZE(n.szInfo) - 1] = 0;
  Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void tray_restore(HWND hwnd)
{
  if (!g_persistent) tray_remove();
  ShowWindow(hwnd, SW_SHOW);
  if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
  SetForegroundWindow(hwnd);
}

static void tray_menu(HWND hwnd)
{
  POINT pt;
  int cmd;
  HMENU menu = CreatePopupMenu();
  WCHAR t1[128], t2[128], t3[128];
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, wtr("Show cdie", t1, 128));
  AppendMenuW(menu, MF_STRING | (g_monitor ? MF_CHECKED : 0), IDM_TRAY_MONITOR, wtr("Monitor downloads", t2, 128));
  AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, wtr("Exit", t3, 128));
  SetMenuDefaultItem(menu, IDM_TRAY_SHOW, FALSE);
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);
  cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);

  if (cmd == IDM_TRAY_SHOW) tray_restore(hwnd);
  else if (cmd == IDM_TRAY_MONITOR) {
    g_monitor = !g_monitor;
    if (g_monitor_cb) g_monitor_cb(g_monitor_user, g_monitor);
  }
  else if (cmd == IDM_TRAY_EXIT) {
    tray_remove();
    if (g_exit_cb) g_exit_cb(g_exit_user);
  }
}

int tray_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
  *result = 0;

  if (g_taskbarCreated && msg == g_taskbarCreated) {
    g_added = 0;
    if (g_persistent) tray_add(hwnd);
    return 0;
  }

  switch (msg) {
  case WM_CLOSE:
    if (g_persistent) { ShowWindow(hwnd, SW_HIDE); return 1; }
    return 0;

  case WM_SYSCOMMAND:
    /* original precedence bug fixed: only intercept SC_MINIMIZE */
    if ((wp & 0xFFF0) == SC_MINIMIZE && (g_enabled || g_persistent)) {
      tray_add(hwnd);
      if (g_added) { ShowWindow(hwnd, SW_HIDE); return 1; }
    }
    return 0;

  case WM_TRAYICON:
    if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) tray_restore(hwnd);
    else if (lp == WM_RBUTTONUP) tray_menu(hwnd);
    else if (lp == NIN_BALLOONUSERCLICK) {
      tray_restore(hwnd);
      if (g_click_cb) g_click_cb(g_click_user);
    }
    return 1;

  case WM_NCDESTROY:
    tray_remove();
    return 0;

  default:
    return 0;
  }
}

#elif defined(__APPLE__)

static int g_enabled, g_persistent, g_monitor;
static TrayExitFn g_exit_cb;       static void* g_exit_user;
static TrayClickFn g_click_cb;     static void* g_click_user;
static TrayMonitorFn g_monitor_cb; static void* g_monitor_user;
static TrayWindowOps g_ops;

@interface CdieTray : NSObject <UNUserNotificationCenterDelegate>
@property(strong) NSStatusItem* item;
@property(strong) NSMenuItem* monitorItem;
@end

static CdieTray* g_tray;

@implementation CdieTray
- (void)show:(id)s { if (g_ops.show) g_ops.show(g_ops.user); [NSApp activateIgnoringOtherApps : YES] ; }
- (void)toggleMonitor : (id)s
{
  g_monitor = !g_monitor;
  self.monitorItem.state = g_monitor ? NSControlStateValueOn : NSControlStateValueOff;
  if (g_monitor_cb) g_monitor_cb(g_monitor_user, g_monitor);
}
- (void)quit:(id)s { tray_remove(); if (g_exit_cb) g_exit_cb(g_exit_user); }
- (void)userNotificationCenter : (UNUserNotificationCenter*)c
didReceiveNotificationResponse : (UNNotificationResponse*)r withCompletionHandler : (void(^)(void))h
{
  [self show : nil] ;
  if (g_click_cb) g_click_cb(g_click_user);
  h();
}
- (void)userNotificationCenter:(UNUserNotificationCenter*)c
willPresentNotification : (UNNotification*)n withCompletionHandler : (void(^)(UNNotificationPresentationOptions))h
{
  h(UNNotificationPresentationOptionBanner);
}
@end

void tray_init(TrayExitFn exit_cb, void* user) { g_exit_cb = exit_cb; g_exit_user = user; }
void tray_set_callbacks(TrayClickFn click, void* cu, TrayMonitorFn mon, void* mu)
{
  g_click_cb = click; g_click_user = cu;
  g_monitor_cb = mon; g_monitor_user = mu;
}
void tray_set_window_ops(const TrayWindowOps* ops) { g_ops = *ops; }
void tray_set_enabled(int enabled) { g_enabled = enabled ? 1 : 0; }
int  tray_get_enabled(void) { return g_enabled; }
int  tray_get_monitor(void) { return g_monitor; }

static void ensure_item(void)
{
  if (g_tray && g_tray.item) return;
  if (!g_tray) g_tray = [CdieTray new];
  g_tray.item = [[NSStatusBar systemStatusBar]statusItemWithLength:NSVariableStatusItemLength];
  g_tray.item.button.title = @"cdie";                  /* replace with a template image if you have one */
  NSMenu* m = [NSMenu new];
  NSMenuItem* show = [m addItemWithTitle:[NSString stringWithUTF8String:translate("Show cdie")] action : @selector(show:) keyEquivalent:@""];
  g_tray.monitorItem = [m addItemWithTitle:[NSString stringWithUTF8String:translate("Monitor downloads")] action : @selector(toggleMonitor:) keyEquivalent:@""];
  [m addItem:[NSMenuItem separatorItem] ] ;
  NSMenuItem* quit = [m addItemWithTitle:[NSString stringWithUTF8String:translate("Exit")] action : @selector(quit:) keyEquivalent:@""];
  show.target = g_tray; g_tray.monitorItem.target = g_tray; quit.target = g_tray;
  g_tray.monitorItem.state = g_monitor ? NSControlStateValueOn : NSControlStateValueOff;
  g_tray.item.menu = m;
}

void tray_set_monitor(int on)
{
  g_monitor = on ? 1 : 0;
  if (g_tray) g_tray.monitorItem.state = g_monitor ? NSControlStateValueOn : NSControlStateValueOff;
}

void tray_remove(void)
{
  if (g_tray && g_tray.item) {
    [[NSStatusBar systemStatusBar]removeStatusItem:g_tray.item];
    g_tray.item = nil;
  }
}

void tray_set_persistent(int on)
{
  g_persistent = on ? 1 : 0;
  if (on) ensure_item();
  else {
    tray_remove();
    if (g_ops.is_visible && !g_ops.is_visible(g_ops.user) && g_ops.show) g_ops.show(g_ops.user);
  }
}

void tray_notify(const char* title, const char* text)
{
  ensure_item();
  UNUserNotificationCenter* c = [UNUserNotificationCenter currentNotificationCenter];
  c.delegate = g_tray;
  NSString* t = [NSString stringWithUTF8String:title];
  NSString* b = [NSString stringWithUTF8String:text];
  [c requestAuthorizationWithOptions:UNAuthorizationOptionAlert completionHandler : ^ (BOOL ok, NSError * e) {
    if (!ok) return;
    UNMutableNotificationContent* n = [UNMutableNotificationContent new];
    n.title = t; n.body = b;
    UNNotificationRequest* r = [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID]UUIDString]
      content : n trigger : nil];
    [c addNotificationRequest:r withCompletionHandler : nil] ;
  }] ;
}

int tray_intercept_close(void)
{
  if (!g_persistent || !g_tray.item) return 0;
  if (g_ops.hide) g_ops.hide(g_ops.user);
  return 1;
}

int tray_intercept_minimize(void)
{
  if (!(g_enabled || g_persistent)) return 0;
  ensure_item();
  if (g_ops.hide) g_ops.hide(g_ops.user);
  return 1;
}

#else

static int g_enabled, g_persistent, g_monitor, g_block, g_notify_ready;
static AppIndicator* g_ind;
static GtkWidget* g_mon_item;
static TrayExitFn g_exit_cb;       static void* g_exit_user;
static TrayClickFn g_click_cb;     static void* g_click_user;
static TrayMonitorFn g_monitor_cb; static void* g_monitor_user;
static TrayWindowOps g_ops;

void tray_init(TrayExitFn exit_cb, void* user) { g_exit_cb = exit_cb; g_exit_user = user; }

void tray_set_callbacks(TrayClickFn click, void* cu, TrayMonitorFn mon, void* mu)
{
  g_click_cb = click; g_click_user = cu;
  g_monitor_cb = mon; g_monitor_user = mu;
}

void tray_set_window_ops(const TrayWindowOps* ops) { g_ops = *ops; }
void tray_set_enabled(int enabled) { g_enabled = enabled ? 1 : 0; }
int  tray_get_enabled(void) { return g_enabled; }
int  tray_get_monitor(void) { return g_monitor; }

static void window_show(void) { if (g_ops.show) g_ops.show(g_ops.user); }
static void window_hide(void) { if (g_ops.hide) g_ops.hide(g_ops.user); }

static void on_show(GtkMenuItem* i, gpointer d) { (void)i; (void)d; window_show(); }

static void on_menu_exit(GtkMenuItem* i, gpointer d) {
  (void)i; (void)d;
  tray_remove();
  if (g_exit_cb) g_exit_cb(g_exit_user);
}

static void on_monitor(GtkCheckMenuItem* i, gpointer d)
{
  (void)d;
  if (g_block) return;
  g_monitor = gtk_check_menu_item_get_active(i);
  if (g_monitor_cb) g_monitor_cb(g_monitor_user, g_monitor);
}

static void ensure_indicator(void)
{
  GtkWidget* menu, * show, * sep, * quit;
  if (g_ind) return;
  g_ind = app_indicator_new("cdie", "cdie", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
  menu = gtk_menu_new();
  show = gtk_menu_item_new_with_label(translate("Show cdie"));
  g_mon_item = gtk_check_menu_item_new_with_label(translate("Monitor downloads"));
  sep = gtk_separator_menu_item_new();
  quit = gtk_menu_item_new_with_label(translate("Exit"));
  g_block = 1;
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_mon_item), g_monitor);
  g_block = 0;
  g_signal_connect(show, "activate", G_CALLBACK(on_show), NULL);
  g_signal_connect(g_mon_item, "toggled", G_CALLBACK(on_monitor), NULL);
  g_signal_connect(quit, "activate", G_CALLBACK(on_menu_exit), NULL);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), show);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), g_mon_item);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
  gtk_widget_show_all(menu);
  app_indicator_set_menu(g_ind, GTK_MENU(menu));
  app_indicator_set_secondary_activate_target(g_ind, show);   /* middle click */
  app_indicator_set_status(g_ind, APP_INDICATOR_STATUS_PASSIVE);
}

void tray_set_monitor(int on)
{
  g_monitor = on ? 1 : 0;
  if (g_mon_item) {
    g_block = 1;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_mon_item), g_monitor);
    g_block = 0;
  }
}

void tray_remove(void)
{
  if (g_ind) app_indicator_set_status(g_ind, APP_INDICATOR_STATUS_PASSIVE);
}

void tray_set_persistent(int on)
{
  g_persistent = on ? 1 : 0;
  if (on) {
    ensure_indicator();
    app_indicator_set_status(g_ind, APP_INDICATOR_STATUS_ACTIVE);
  }
  else {
    tray_remove();
    if (g_ops.is_visible && !g_ops.is_visible(g_ops.user)) window_show();
  }
}

static void on_note_click(NotifyNotification* n, char* action, gpointer d)
{
  (void)n; (void)action; (void)d;
  window_show();
  if (g_click_cb) g_click_cb(g_click_user);
}
static void on_note_closed(NotifyNotification* n, gpointer d) { (void)d; g_object_unref(n); }

void tray_notify(const char* title, const char* text)
{
  NotifyNotification* n;
  if (!g_notify_ready) g_notify_ready = notify_init("cdie");
  if (!g_notify_ready) return;
  n = notify_notification_new(title, text, "cdie");
  notify_notification_add_action(n, "default", translate("Open"), on_note_click, NULL, NULL);
  g_signal_connect(n, "closed", G_CALLBACK(on_note_closed), NULL);
  if (!notify_notification_show(n, NULL)) g_object_unref(n);
}

int tray_intercept_close(void)
{
  if (!g_persistent || !g_ind) return 0;
  window_hide();
  return 1;
}

int tray_intercept_minimize(void)
{
  if (!(g_enabled || g_persistent)) return 0;
  ensure_indicator();
  app_indicator_set_status(g_ind, APP_INDICATOR_STATUS_ACTIVE);
  window_hide();
  return 1;
}

#endif
