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
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "global.h"
#include "core/gui_backend.h"
#include "theme/theme.h"
#include "controls/sidebar.h"
#include "panels/header.h"
#include "panels/infobar.h"
#include "controls/bottombar.h"
#include "controls/treeview.h"
#include "panels/signatures.h"
#include "panels/strings.h"
#include "panels/options.h"
#include "panels/entropy.h"
#include "panels/sectionsview.h"
#include "controls/hexviewer.h"
#include "controls/disasmview.h"
#include "core/virustotal.h"
#include "core/config.h"
#include "core/i18n.h"
#include "platform/platform.h"
#include "xxwidgets/xxwidgets.h"
#include "core/tray.h"
#include "core/dlwatch.h"

static GtkWidget* g_win;
static GtkWidget* g_sw;
static GtkWidget* g_fixed;
static Sidebar* g_sidebar;
static Header* g_header;
static Infobar* g_infobar;
static Bottombar* g_bottombar;
static ResultsTree* g_tree;
static StringsView* g_strv;
static HexViewer* g_hex;
static DisasmView* g_dasm;
static EntropyView* g_entropy;
static VtView* g_vt;
static SecView* g_sec;
static SigView* g_sig;
static FILE* g_hexFile;
static uint64_t g_hexSize;

static int g_page = NAV_OVERVIEW;
static void layout(void);

void* cdie_gtk_main_window(void);
void* cdie_gtk_main_window(void) { return g_win; }

static char* g_lastPath;
static char* g_lastReport;

static int g_bFlagDeep = 1, g_bFlagHeur = 1, g_bFlagVerbose = 1;
static int g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat;
static int g_bDbExtra = 1, g_bDbCustom = 1;

static void set_status(const char* text)
{
  if (g_bottombar) bottombar_set_status(g_bottombar, text);
}

static void apply_flags_to_backend(void)
{
  gui_backend_set_flags(g_bFlagDeep, g_bFlagHeur, g_bFlagVerbose,
    g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat);
  gui_backend_set_databases(g_bDbExtra, g_bDbCustom);
}

static int hex_read(void* u, uint64_t off, void* buf, size_t len)
{
  (void)u;
  if (!g_hexFile) return 0;
  if (fseeko(g_hexFile, (off_t)off, SEEK_SET) != 0) return 0;
  return fread(buf, 1, len, g_hexFile) == len;
}

static void hex_close(void)
{
  if (g_hexFile) fclose(g_hexFile);
  g_hexFile = NULL;
  g_hexSize = 0;
}

static void load_views(const char* path)
{
  GStatBuf st;
  hex_close();
  g_hexFile = g_fopen(path, "rb");
  if (g_hexFile && g_stat(path, &st) == 0) g_hexSize = (uint64_t)st.st_size;
  else hex_close();

  if (g_hex) hexview_set_source(g_hex, g_hexSize, hex_read, NULL, 0);
  if (g_dasm) {
    if (g_hexFile) disasmview_open(g_dasm, hex_read, NULL, g_hexSize);
    else disasmview_close(g_dasm);
  }
  if (g_entropy) {
    if (g_hexFile) entropyview_open(g_entropy, path, hex_read, NULL, g_hexSize);
    else entropyview_close(g_entropy);
  }
  if (g_vt) vtview_set_file(g_vt, path);
  if (g_sec) secview_set_file(g_sec, path);
  if (g_strv) stringsview_set_file(g_strv, path);
}

static int opt_copy_text(void* u, const char* text)
{
  GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  (void)u;
  if (!cb || !text) return 0;
  gtk_clipboard_set_text(cb, text, -1);
  return 1;
}

static void goto_entropy(void* u)
{
  (void)u;
  g_page = NAV_ENTROPY;
  layout();
}

static void clear_scan(void)
{
  if (g_tree) resultstree_clear(g_tree);
  if (g_infobar) infobar_clear(g_infobar);
  if (g_header) header_clear(g_header);
}

typedef struct {
  char type[80], size[80], size_sub[80];
  char ep[80], ep_sub[80], base[80], base_sub[80];
} FileInfo;

static uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const unsigned char* p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const unsigned char* p) { return rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

static void get_file_info(const char* path, const char* type_name, FileInfo* fi)
{
  GStatBuf st;
  FILE* f;

  memset(fi, 0, sizeof(*fi));
  g_strlcpy(fi->type, (type_name && type_name[0]) ? type_name : "Unknown", sizeof(fi->type));
  g_strlcpy(fi->size, "N/A", sizeof(fi->size));
  g_strlcpy(fi->ep, "N/A", sizeof(fi->ep));
  g_strlcpy(fi->base, "N/A", sizeof(fi->base));

  if (g_stat(path, &st) == 0) {
    char* s = g_format_size_full((guint64)st.st_size, G_FORMAT_SIZE_IEC_UNITS);
    g_strlcpy(fi->size, s, sizeof(fi->size));
    g_free(s);
    g_snprintf(fi->size_sub, sizeof(fi->size_sub), "%" G_GUINT64_FORMAT " bytes",
      (guint64)st.st_size);
  }

  f = g_fopen(path, "rb");
  if (!f) return;
  {
    unsigned char dos[64], hdr[120];
    if (fread(dos, 1, 64, f) == 64 && dos[0] == 'M' && dos[1] == 'Z') {
      uint32_t pe_off = rd32(dos + 0x3C);
      if (fseek(f, (long)pe_off, SEEK_SET) == 0 &&
        fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) &&
        memcmp(hdr, "PE\0\0", 4) == 0) {
        uint16_t magic = rd16(hdr + 24);
        uint32_t ep = rd32(hdr + 40);
        uint64_t base = 0;
        int ok = 1;
        if (magic == 0x10B) base = rd32(hdr + 52);
        else if (magic == 0x20B) base = rd64(hdr + 48);
        else ok = 0;
        if (ok) {
          g_snprintf(fi->ep, sizeof(fi->ep), "0x%08X", ep);
          if (base > 0xFFFFFFFFull)
            g_snprintf(fi->base, sizeof(fi->base), "0x%08X%08X",
              (unsigned)(base >> 32), (unsigned)(base & 0xFFFFFFFFu));
          else
            g_snprintf(fi->base, sizeof(fi->base), "0x%08X", (unsigned)base);
          g_strlcpy(fi->ep_sub, "RVA", sizeof(fi->ep_sub));
          g_strlcpy(fi->base_sub, "Preferred load", sizeof(fi->base_sub));
        }
      }
    }
  }
  fclose(f);
}

static void update_chrome_after_scan(const char* path, const char* type_name)
{
  FileInfo fi;
  get_file_info(path, type_name, &fi);

  if (g_header) {
    const char* last = strrchr(path, '/');
    if (last) {
      char* folder = g_strndup(path, (gsize)(last - path + 1));
      header_set_path(g_header, folder, last + 1);
      g_free(folder);
    }
    else header_set_path(g_header, "", path);
  }
  if (g_infobar) {
    InfoCard cards[4];
    cards[0].label = "FILE TYPE";    cards[0].value = fi.type; cards[0].subvalue = "";
    cards[1].label = "FILE SIZE";    cards[1].value = fi.size; cards[1].subvalue = fi.size_sub;
    cards[2].label = "ENTRY POINT";  cards[2].value = fi.ep;   cards[2].subvalue = fi.ep_sub;
    cards[3].label = "BASE ADDRESS"; cards[3].value = fi.base; cards[3].subvalue = fi.base_sub;
    infobar_set_cards(g_infobar, cards, 4);
  }
}

typedef struct { unsigned count; int failed; } ScanCtx;

static void collect_result(const char* type, const char* name,
  const char* version, const char* info, void* user)
{
  ScanCtx* ctx = (ScanCtx*)user;
  ++ctx->count;
  if (!resultstree_add(g_tree, type, name, version, info)) ctx->failed = 1;
}

static void do_scan(const char* path)
{
  ScanCtx ctx = { 0, 0 };
  char* report = NULL, * type = NULL;
  int ok;
  gint64 t0;

  if (!path || !path[0]) return;
  apply_flags_to_backend();
  clear_scan();
  set_status("Scanning the selected file...");
  while (gtk_events_pending()) gtk_main_iteration();

  t0 = g_get_monotonic_time();
  ok = gui_backend_scan_results_type(path, 0, collect_result, &ctx, &report, &type);
  t0 = (g_get_monotonic_time() - t0) / 1000;

  g_free(g_lastReport);
  g_lastReport = report ? g_strdup(report) : NULL;

  if (ok) {
    char msg[512];
    g_snprintf(msg, sizeof(msg),
      "Scan complete (%s). %u result%s. Press Ctrl+R for the full engine output.",
      (type && type[0]) ? type : "Unknown type", ctx.count, ctx.count == 1 ? "" : "s");
    set_status(ctx.failed ? "Out of memory while displaying results." : msg);
    update_chrome_after_scan(path, type);
    if (g_header) header_set_time(g_header, (unsigned)t0);
    g_free(g_lastPath);
    g_lastPath = g_strdup(path);
    load_views(path);
  }
  else {
    set_status(report && report[0] ? report : "Cannot open the file.");
  }
  if (report) gui_backend_free(report);
  if (type) gui_backend_free(type);
}

static void do_browse(void)
{
  GtkWidget* dlg = gtk_file_chooser_dialog_new("Select a file to scan",
    GTK_WINDOW(g_win), GTK_FILE_CHOOSER_ACTION_OPEN,
    "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
  char* path = NULL;
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT)
    path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
  gtk_widget_destroy(dlg);
  if (path) { do_scan(path); g_free(path); }
}

static void do_report(void)
{
  GtkWidget* dlg, * sc, * tv;
  GtkTextBuffer* buf;
  if (!g_lastReport || !g_lastReport[0]) { set_status("No scan report yet."); return; }
  dlg = gtk_dialog_new_with_buttons("Scan report", GTK_WINDOW(g_win),
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
  gtk_window_set_default_size(GTK_WINDOW(dlg), 800, 600);
  sc = gtk_scrolled_window_new(NULL, NULL);
  tv = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
  buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
  gtk_text_buffer_set_text(buf, g_lastReport, -1);
  gtk_container_add(GTK_CONTAINER(sc), tv);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), sc, TRUE, TRUE, 0);
  gtk_widget_show_all(dlg);
  gtk_dialog_run(GTK_DIALOG(dlg));
  gtk_widget_destroy(dlg);
}

static void do_about(void)
{
  GtkWidget* dlg = gtk_about_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(g_win));
  gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), "Detect It Easy - cdie");
  gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg), X_APPLICATIONVERSION);
  gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg), "Detect It Easy scan engine, ported to C.");
  gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dlg), "Copyright (C) 2026 hors <horsicq@gmail.com>");
  gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dlg), "https://ntinfo.biz");
  gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dlg), GTK_LICENSE_MIT_X11);
  gtk_dialog_run(GTK_DIALOG(dlg));
  gtk_widget_destroy(dlg);
}

static void on_pick_folder(GtkButton* b, gpointer entry)
{
  GtkWidget* dlg = gtk_file_chooser_dialog_new("Select the database folder",
    GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(b))), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
    "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char* p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    if (p) { gtk_entry_set_text(GTK_ENTRY(entry), p); g_free(p); }
  }
  gtk_widget_destroy(dlg);
}

static void do_database_paths(void)
{
  GtkWidget* dlg = gtk_dialog_new_with_buttons("Database paths", GTK_WINDOW(g_win),
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_ACCEPT, NULL);
  GtkWidget* grid = gtk_grid_new(), * err = gtk_label_new("");
  GtkWidget* entry[3];
  const char* names[3] = { "Main", "Extra", "Custom" };
  const char* cur[3];
  int i, saved = 0;

  cur[0] = gui_backend_main_db_path();
  cur[1] = gui_backend_extra_db_path();
  cur[2] = gui_backend_custom_db_path();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
  for (i = 0; i < 3; ++i) {
    GtkWidget* b = gtk_button_new_with_label("...");
    entry[i] = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry[i]), 56);
    gtk_entry_set_text(GTK_ENTRY(entry[i]), cur[i] ? cur[i] : "");
    g_signal_connect(b, "clicked", G_CALLBACK(on_pick_folder), entry[i]);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(names[i]), 0, i, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry[i], 1, i, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), b, 2, i, 1, 1);
  }
  gtk_grid_attach(GTK_GRID(grid), err, 0, 3, 3, 1);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), grid, TRUE, TRUE, 0);
  gtk_widget_show_all(dlg);

  while (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    if (gui_backend_set_db_paths(gtk_entry_get_text(GTK_ENTRY(entry[0])),
      gtk_entry_get_text(GTK_ENTRY(entry[1])),
      gtk_entry_get_text(GTK_ENTRY(entry[2])))) {
      saved = 1; break;
    }
    gtk_label_set_text(GTK_LABEL(err),
      "Cannot load the main signature database. Check its folder and try again.");
  }
  gtk_widget_destroy(dlg);

  if (saved) {
    char msg[160];
    clear_scan();
    if (g_sig) sigview_reload(g_sig);
    g_snprintf(msg, sizeof(msg), "Database reloaded: %d signatures. Choose a file and press Scan.",
      gui_backend_signature_count());
    set_status(msg);
  }
}

static void do_options(void)
{
  struct { const char* label; int* var; } rows[] = {
    { "Deep scan", &g_bFlagDeep },{ "Heuristic scan", &g_bFlagHeur },
    { "Verbose", &g_bFlagVerbose },{ "Aggressive scan", &g_bFlagAggr },
    { "Hide unknown", &g_bFlagHideUnknown },{ "Format result", &g_bFlagFormat },
    { "Extra database", &g_bDbExtra },{ "Custom database", &g_bDbCustom }
  };
  GtkWidget* dlg = gtk_dialog_new_with_buttons("cdie Scan options", GTK_WINDOW(g_win),
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    "_Database paths...", GTK_RESPONSE_APPLY,
    "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_ACCEPT, NULL);
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), * chk[8], * chkMon;
  int i, resp;

  gtk_container_set_border_width(GTK_CONTAINER(box), 12);
  for (i = 0; i < 8; ++i) {
    chk[i] = gtk_check_button_new_with_label(rows[i].label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk[i]), *rows[i].var);
    gtk_box_pack_start(GTK_BOX(box), chk[i], FALSE, FALSE, 0);
  }
  chkMon = gtk_check_button_new_with_label("Monitor downloads");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chkMon), dlwatch_enabled());
  gtk_box_pack_start(GTK_BOX(box), chkMon, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), box, TRUE, TRUE, 0);
  gtk_widget_show_all(dlg);

  while ((resp = gtk_dialog_run(GTK_DIALOG(dlg))) == GTK_RESPONSE_APPLY)
    do_database_paths();

  if (resp == GTK_RESPONSE_ACCEPT) {
    for (i = 0; i < 8; ++i)
      *rows[i].var = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk[i]));
    apply_flags_to_backend();
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chkMon)) != (dlwatch_enabled() != 0))
      dlwatch_set_enabled(!dlwatch_enabled());
    set_status("Options applied to the next scan. Use Database paths to choose signature folders.");
  }
  gtk_widget_destroy(dlg);
}

/* ---- tray / download monitor glue ---- */
static void on_tray_exit(void* user) { (void)user; gtk_widget_destroy(g_win); }

static void win_show(void* u)
{
  (void)u;
  gtk_widget_show(g_win);
  gtk_window_deiconify(GTK_WINDOW(g_win));
  gtk_window_present(GTK_WINDOW(g_win));
}
static void win_hide(void* u) { (void)u; gtk_widget_hide(g_win); }
static int  win_is_visible(void* u) { (void)u; return gtk_widget_get_visible(g_win); }

static void mon_request_scan(const char* path, void* u) { (void)u; do_scan(path); }
static void mon_set_status(const char* text, void* u) { (void)u; set_status(text); }
static void mon_restore_flags(void* u) { (void)u; apply_flags_to_backend(); }

static gboolean on_delete(GtkWidget* w, GdkEvent* e, gpointer u)
{
  (void)w; (void)e; (void)u;
  return tray_intercept_close() ? TRUE : FALSE;     /* TRUE = don't close, we hid it */
}

static gboolean on_window_state(GtkWidget* w, GdkEventWindowState* e, gpointer u)
{
  (void)w; (void)u;
  if ((e->changed_mask & GDK_WINDOW_STATE_ICONIFIED) &&
    (e->new_window_state & GDK_WINDOW_STATE_ICONIFIED))
    tray_intercept_minimize();
  return FALSE;
}

static gboolean idle_browse(gpointer u) { (void)u; do_browse(); return G_SOURCE_REMOVE; }
static gboolean idle_options(gpointer u) { (void)u; do_options(); return G_SOURCE_REMOVE; }

static void on_browse(void* user) { (void)user; g_idle_add(idle_browse, NULL); }

static void on_nav(void* user, int nav_id)
{
  (void)user;
  switch (nav_id) {
  case NAV_OPTIONS:
    g_idle_add(idle_options, NULL);
    break;
  case NAV_SIGNATURES:
    g_page = nav_id; layout();
    if (g_sig) sigview_activate(g_sig);
    break;
  case NAV_STRINGS:
    g_page = nav_id; layout();
    if (g_strv) stringsview_activate(g_strv);
    break;
  case NAV_VIRUSTOTAL:
    g_page = nav_id; layout();
    if (g_vt) vtview_refresh(g_vt);
    break;
  case NAV_OVERVIEW:
  case NAV_SECTIONS:
  case NAV_HEX:
  case NAV_DISASM:
  case NAV_ENTROPY:
    g_page = nav_id; layout();
    break;
  default: break;
  }
}

static int route_key(GdkEventKey* e, guint ctrl, guint shift)
{
  int mods = 0, key = 0;
  uint32_t ch = 0;
  guint lk = gdk_keyval_to_lower(e->keyval);

  if (g_page != NAV_STRINGS && g_page != NAV_SIGNATURES && g_page != NAV_HEX) return 0;
  if (ctrl)  mods |= PL_MOD_CTRL;
  if (shift) mods |= PL_MOD_SHIFT;

  switch (e->keyval) {
  case GDK_KEY_Escape:    key = PL_KEY_ESC;   break;
  case GDK_KEY_BackSpace: key = PL_KEY_BACK;  break;
  case GDK_KEY_Delete:    key = PL_KEY_DEL;   break;
  case GDK_KEY_Left:      key = PL_KEY_LEFT;  break;
  case GDK_KEY_Right:     key = PL_KEY_RIGHT; break;
  case GDK_KEY_Home:      key = PL_KEY_HOME;  break;
  case GDK_KEY_End:       key = PL_KEY_END;   break;
  case GDK_KEY_Up:        key = PL_KEY_UP;    break;
  case GDK_KEY_Down:      key = PL_KEY_DOWN;  break;
  case GDK_KEY_Page_Up:   key = PL_KEY_PGUP;  break;
  case GDK_KEY_Page_Down: key = PL_KEY_PGDN;  break;
  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:  key = PL_KEY_ENTER; break;
  case GDK_KEY_Tab:       key = PL_KEY_TAB;   break;
  case GDK_KEY_F3:        key = PL_KEY_F3;    break;
  default:
    if (ctrl) {
      if (g_page == NAV_HEX) {          /* hex viewer wants control codes */
        if (lk == 'a') ch = 1;
        else if (lk == 'c') ch = 3;
        else if (lk == 'f') ch = 6;
        else if (lk == 'g') ch = 7;
      }
      else if (lk == 'f' || lk == 's') ch = lk;
      if (ch) key = PL_KEY_CHAR;
    }
    else {
      guint32 uc = gdk_keyval_to_unicode(e->keyval);
      if (uc >= 32 && uc != 127) { key = PL_KEY_CHAR; ch = uc; }
    }
    break;
  }
  if (!key) return 0;

  if (g_page == NAV_STRINGS && g_strv) stringsview_key(g_strv, key, ch, mods);
  else if (g_page == NAV_SIGNATURES && g_sig) sigview_key(g_sig, key, ch, mods);
  else if (g_page == NAV_HEX && g_hex) hexview_key(g_hex, key, (key == PL_KEY_CHAR ? ch : 0), mods);
  else return 0;
  return 1;
}

static gboolean on_key_press(GtkWidget* w, GdkEventKey* e, gpointer u)
{
  guint ctrl = e->state & GDK_CONTROL_MASK, shift = e->state & GDK_SHIFT_MASK;
  (void)w; (void)u;
  if (e->keyval == GDK_KEY_F5) { if (g_lastPath) { char* p = g_strdup(g_lastPath); do_scan(p); g_free(p); } return TRUE; }
  if (e->keyval == GDK_KEY_F1) { do_about(); return TRUE; }
  if (ctrl && !shift && (e->keyval == GDK_KEY_m || e->keyval == GDK_KEY_M)) {
    dlwatch_set_enabled(!dlwatch_enabled());
    return TRUE;
  }
  if (ctrl && !shift && (e->keyval == GDK_KEY_o || e->keyval == GDK_KEY_O)) { do_browse(); return TRUE; }
  if (ctrl && !shift && (e->keyval == GDK_KEY_r || e->keyval == GDK_KEY_R)) { do_report(); return TRUE; }
  if (ctrl && shift && (e->keyval == GDK_KEY_d || e->keyval == GDK_KEY_D)) { do_database_paths(); return TRUE; }
  return route_key(e, ctrl, shift) ? TRUE : FALSE;
}

static void on_drag_received(GtkWidget* w, GdkDragContext* ctx, gint x, gint y,
  GtkSelectionData* data, guint info, guint time, gpointer u)
{
  gchar** uris = gtk_selection_data_get_uris(data);
  (void)w; (void)x; (void)y; (void)info; (void)u;
  if (uris && uris[0]) {
    char* path = g_filename_from_uri(uris[0], NULL, NULL);
    if (path) { do_scan(path); g_free(path); }
  }
  g_strfreev(uris);
  gtk_drag_finish(ctx, TRUE, FALSE, time);
}

static void layout_impl(void);
static void layout(void)
{
  static int busy;
  if (busy) return;
  busy = 1;
  layout_impl();
  busy = 0;
}

static void place_view(int page, int x, int y, int w, int h)
{
  switch (page) {
  case NAV_HEX:        if (g_hex)     hexview_set_rect(g_hex, x, y, w, h); break;
  case NAV_DISASM:     if (g_dasm)    disasmview_set_rect(g_dasm, x, y, w, h); break;
  case NAV_ENTROPY:    if (g_entropy) entropyview_set_rect(g_entropy, x, y, w, h); break;
  case NAV_VIRUSTOTAL: if (g_vt)      vtview_set_rect(g_vt, x, y, w, h); break;
  case NAV_SECTIONS:   if (g_sec)     secview_set_rect(g_sec, x, y, w, h); break;
  case NAV_SIGNATURES: if (g_sig)     sigview_set_rect(g_sig, x, y, w, h); break;
  case NAV_STRINGS:    if (g_strv)    stringsview_set_rect(g_strv, x, y, w, h); break;
  default:             pl_widget_set_rect(resultstree_widget(g_tree), x, y, w, h); break;
  }
}

static void layout_impl(void)
{
  static const int pages[] = { NAV_OVERVIEW, NAV_HEX, NAV_DISASM, NAV_ENTROPY,
    NAV_VIRUSTOTAL, NAV_SECTIONS, NAV_SIGNATURES, NAV_STRINGS };
  static int s_W = -1, s_H = -1, s_page = -1;
  int W, H, cx, cy, cw, tx, ty, tw, th, i, cur;

  if (!g_sw || !g_fixed || !g_sidebar || !g_header || !g_infobar ||
    !g_bottombar || !g_tree) return;
  W = gtk_widget_get_allocated_width(g_sw);
  H = gtk_widget_get_allocated_height(g_sw);
  if (W <= SIDEBAR_W || H <= 0) return;

  /* which "slot" is visible: anything that isn't a dedicated view uses the tree */
  cur = g_page;
  if (cur != NAV_HEX && cur != NAV_DISASM && cur != NAV_ENTROPY &&
    cur != NAV_VIRUSTOTAL && cur != NAV_SECTIONS &&
    cur != NAV_SIGNATURES && cur != NAV_STRINGS)
    cur = NAV_OVERVIEW;

  if (W == s_W && H == s_H && cur == s_page) return;   /* nothing changed */

  cx = SIDEBAR_W; cw = W - cx; cy = HEADER_H + INFOBAR_H;
  tx = cx + CONTENT_PAD; tw = cw - 2 * CONTENT_PAD;
  ty = cy + 4;           th = H - cy - BOTTOMBAR_H - 8;
  if (tw < 40) tw = 40;
  if (th < 40) th = 40;

  if (W != s_W || H != s_H) {
    /* window was resized: reposition chrome and every view */
    pl_widget_set_rect(sidebar_widget(g_sidebar), 0, 0, SIDEBAR_W, H);
    pl_widget_set_rect(header_widget(g_header), cx, 0, cw, HEADER_H);
    pl_widget_set_rect(infobar_widget(g_infobar), cx, HEADER_H, cw, INFOBAR_H);
    pl_widget_set_rect(bottombar_widget(g_bottombar), cx, H - BOTTOMBAR_H, cw, BOTTOMBAR_H);
    for (i = 0; i < 8; ++i)
      place_view(pages[i], tx, ty, pages[i] == cur ? tw : 0, pages[i] == cur ? th : 0);
  }
  else {
    /* page switch only: hide the old view, show the new one */
    if (s_page != -1) place_view(s_page, tx, ty, 0, 0);
    place_view(cur, tx, ty, tw, th);
  }

  s_W = W; s_H = H; s_page = cur;
}
static void on_size_allocate(GtkWidget* w, GdkRectangle* a, gpointer user)
{
  (void)w; (void)a; (void)user;
  layout();
}

static gboolean blink_tick(gpointer u)
{
  (void)u;
  if (g_vt) vtview_tick(g_vt);
  if (g_strv) stringsview_tick(g_strv);
  if (g_sig) sigview_tick(g_sig);
  if (g_hex) hexview_tick(g_hex);
  if (g_sidebar && sidebar_get_selected(g_sidebar) != g_page)
    sidebar_set_selected(g_sidebar, g_page);
  return G_SOURCE_CONTINUE;
}

int gui_run(void)
{
  GtkWidget* win;
  static const GtkTargetEntry targets[] = { { (gchar*)"text/uri-list", 0, 0 } };
  char ready[256];

  gtk_init(NULL, NULL);

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_win = win;
  gtk_window_set_title(GTK_WINDOW(win), "Detect It Easy v" X_APPLICATIONVERSION);
  gtk_window_set_default_size(GTK_WINDOW(win), 1150, 700);
  gtk_widget_set_size_request(win, 900, 540);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), NULL);
	g_signal_connect(win, "delete-event", G_CALLBACK(on_delete), NULL);
	g_signal_connect(win, "window-state-event", G_CALLBACK(on_window_state), NULL);

  {
    GtkCssProvider* css = gtk_css_provider_new(); 
    gtk_css_provider_load_from_data(css,
      "window, scrolledwindow, viewport { background-color: #1F212E; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
  }

  {
    GtkWidget* sw = gtk_scrolled_window_new(NULL, NULL);
    g_sw = sw;
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
    g_fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(sw), g_fixed);
    gtk_container_add(GTK_CONTAINER(win), sw);
  }

  g_sidebar = sidebar_create(g_fixed, 0, 0, SIDEBAR_W, 700, on_nav, NULL);
  g_header = header_create(g_fixed, SIDEBAR_W, 0, 800, HEADER_H, on_browse, NULL);
  g_infobar = infobar_create(g_fixed, SIDEBAR_W, HEADER_H, 800, INFOBAR_H);
  g_bottombar = bottombar_create(g_fixed, SIDEBAR_W, 644, 800, BOTTOMBAR_H, NULL, NULL);
  g_tree = resultstree_create(g_fixed, SIDEBAR_W, 0, 100, 100);
  g_hex = hexview_create(g_fixed, NULL, SIDEBAR_W, 0, 0, 0);
  if (g_hex) hexview_set_callbacks(g_hex, NULL, opt_copy_text, NULL);
  g_dasm = disasmview_create(g_fixed, NULL, SIDEBAR_W, 0, 0, 0);
  if (g_dasm) disasmview_set_status_callback(g_dasm, mon_set_status, NULL);
  g_entropy = entropyview_create(g_fixed, SIDEBAR_W, 0, 0, 0);
  if (g_entropy) entropyview_set_callbacks(g_entropy, opt_copy_text, NULL);
  g_vt = vtview_create(g_fixed, SIDEBAR_W, 0, 0, 0);
  if (g_vt) vtview_set_callbacks(g_vt, opt_copy_text, NULL);
  g_sec = secview_create(g_fixed, SIDEBAR_W, 0, 0, 0);
  if (g_sec) secview_set_callbacks(g_sec, goto_entropy, NULL);
  g_sig = sigview_create(g_fixed, SIDEBAR_W, 0, 0, 0);
  g_strv = stringsview_create(g_fixed, SIDEBAR_W, 0, 0, 0);
  {
    CdieConfig lc;
    if (config_load(&lc)) vt_set_api_key(lc.vt_key);
    if (config_load(&lc) && lc.language[0]) i18n_load(lc.language);
  }
  if (!g_sidebar || !g_header || !g_infobar || !g_bottombar || !g_tree) {
    g_printerr("cdie: failed to create the chrome controls\n");
    return 1;
  }

  gtk_drag_dest_set(win, GTK_DEST_DEFAULT_ALL, targets, 1, GDK_ACTION_COPY);
  g_signal_connect(win, "drag-data-received", G_CALLBACK(on_drag_received), NULL);
  g_signal_connect(g_sw, "size-allocate", G_CALLBACK(on_size_allocate), NULL);

  if (gui_backend_init()) {
    g_snprintf(ready, sizeof(ready),
      "Ready. %d signatures loaded. Choose a file or drag one here.",
      gui_backend_signature_count());
    set_status(ready);
  }
  else {
    set_status("Main signature database not found. Use Database paths (Ctrl+Shift+D) to select it.");
  }
  apply_flags_to_backend();

  tray_init(on_tray_exit, NULL);
  {
    TrayWindowOps ops = { win_show, win_hide, win_is_visible, NULL };
    DlWatchHost mh;
    tray_set_window_ops(&ops);
    memset(&mh, 0, sizeof(mh));
    mh.request_scan = mon_request_scan;
    mh.set_status = mon_set_status;
    mh.restore_flags = mon_restore_flags;
    dlwatch_init(&mh);
  }
  tray_set_callbacks(dlwatch_balloon_click, NULL, dlwatch_menu_toggle, NULL);

  gtk_widget_show_all(win);
  //g_timeout_add(100, blink_tick, NULL);
  gtk_main();

  if (g_vt) vtview_cancel(g_vt);
  hex_close();
  dlwatch_shutdown();
  tray_remove();
  gui_backend_shutdown();
  g_free(g_lastPath);
  g_free(g_lastReport);
  return 0;
}