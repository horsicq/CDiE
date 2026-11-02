/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_options_extra.h"
#include "gui_error.h"
#include "app/cdie_translation.h"
#include "app/cdie_paths.h"
#include <xxwidgets/xxwidgets_combobox.h>
#ifdef validate
#undef validate
#endif
#include <xxfclib/strings/xx_string.h>
#include <xxfclib/rt/xx_rt.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { FIELD_BOOL, FIELD_TEXT, FIELD_UINT, FIELD_COMBO };
typedef struct extra_field {
    xxwidgets_widget *control;
    void *value;
    const void *default_value;
    size_t capacity;
    int kind;
} extra_field;
typedef struct extra_page {
    gui_options_extra_values *config;
    xxwidgets_widget *dialog, *widgets[64], *browse, *update, *choose, *color_select, *color_fore, *color_back, *message;
    extra_field fields[20];
    size_t widget_count, field_count;
    int pending, selected_color;
    xxwidgets_disasm_color colors[XXWIDGETS_DISASM_COLOR_COUNT];
    int row;
    xxwidgets_rect bounds;
} extra_page;
static xxwidgets_status control(extra_page *p, xxwidgets_kind type, const char *text, int x, int y, int width, int height, xxwidgets_widget **out)
{
    xxwidgets_widget *w = NULL;
    xxwidgets_status s = xxwidgets_widget_create(xxwidgets_widget_app(p->dialog), p->dialog, type, CDIE_TR(text),
        (xxwidgets_rect){p->bounds.x+x,p->bounds.y+y,width,height}, &w);
    if (s == XXWIDGETS_OK) { p->widgets[p->widget_count++] = w; if(out)*out=w; }
    return s;
}
static xxwidgets_status choices(xxwidgets_widget *w, const char *const *names, size_t count)
{
    xx_meta_string records[XXWIDGETS_DISASM_COLOR_COUNT];
    xx_str_w_s labels[XXWIDGETS_DISASM_COLOR_COUNT];
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    memset(records,0,sizeof(records)); memset(labels,0,sizeof(labels));
    for(i=0;i<count;++i) {
        labels[i].data = xx_str_utf8_to_unicode(CDIE_TR(names[i]));
        if(!labels[i].data) {status=XXWIDGETS_OUT_OF_MEMORY;break;}
        labels[i].length=wcslen(labels[i].data); labels[i].capacity=labels[i].length+1; labels[i].is_view=1;
        records[i].meta_string=&labels[i]; records[i].var.type=XX_VAR_TYPE_UINT64; records[i].var.val.u64=i;
    }
    if(status==XXWIDGETS_OK)status=xxwidgets_combobox_set_records(w,records,count);
    for(i=0;i<count;++i)xx_str_free_unicode(labels[i].data);
    return status;
}
static xxwidgets_status fill_field(extra_field *f, int defaults)
{
    const void *v=defaults?f->default_value:f->value;
    char number[24];
    if(f->kind==FIELD_BOOL || f->kind==FIELD_COMBO)return xxwidgets_widget_set_value(f->control,*(const int*)v);
    if(f->kind==FIELD_UINT) {xx_rt_snprintf(number,sizeof(number),"%u",*(const unsigned*)v);return xxwidgets_widget_set_text(f->control,number);}
    return xxwidgets_widget_set_text(f->control,(const char*)v);
}
static xxwidgets_status field(extra_page *p, int kind, const char *label, void *value, const void *def, size_t cap, xxwidgets_widget **out)
{
    extra_field *f=&p->fields[p->field_count];
    xxwidgets_status s;
    f->kind=kind;f->value=value;f->default_value=def;f->capacity=cap;
    if(kind==FIELD_BOOL)s=control(p,XXWIDGETS_CHECKBOX,label,0,p->row,p->bounds.width,1,&f->control);
    else {
        s=control(p,XXWIDGETS_LABEL,label,0,p->row,22,1,NULL);
        if(s==XXWIDGETS_OK)s=control(p,kind==FIELD_COMBO?XXWIDGETS_COMBOBOX:XXWIDGETS_EDIT,"",23,p->row,p->bounds.width-23,kind==FIELD_COMBO?2:1,&f->control);
    }
    if(s==XXWIDGETS_OK) {++p->field_count;if(kind!=FIELD_COMBO)s=fill_field(f,0);if(out)*out=f->control;}
    p->row+=2; return s;
}
static int parse_color(const char *text,uint32_t *value)
{
    unsigned n=0;size_t i;
    if(!text[0] || !strcmp(text,"Default") || !strcmp(text,"default")) {*value=UINT32_MAX;return 1;}
    if(text[0]=='#')++text;
    if(strlen(text)!=6)return 0;
    for(i=0;i<6;++i){unsigned c=(unsigned char)text[i],v=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:16;if(v>15)return 0;n=n*16+v;}
    *value=n;return 1;
}
static int save_color(extra_page *p)
{
    char a[32],b[32];uint32_t fore,back;
    if(!p->color_select)return 1;
    if(xxwidgets_widget_get_text(p->color_fore,a,sizeof(a),NULL)!=XXWIDGETS_OK ||
       xxwidgets_widget_get_text(p->color_back,b,sizeof(b),NULL)!=XXWIDGETS_OK || !parse_color(a,&fore)||!parse_color(b,&back))return 0;
    p->colors[p->selected_color]=(xxwidgets_disasm_color){fore,back};return 1;
}
static xxwidgets_status show_color(extra_page *p)
{
    char a[24],b[24];xxwidgets_disasm_color c=p->colors[p->selected_color];
    if(c.foreground==UINT32_MAX)strcpy(a,"Default");else xx_rt_snprintf(a,sizeof(a),"#%06x",c.foreground);
    if(c.background==UINT32_MAX)strcpy(b,"Default");else xx_rt_snprintf(b,sizeof(b),"#%06x",c.background);
    xxwidgets_status s=xxwidgets_widget_set_text(p->color_fore,a);
    return s==XXWIDGETS_OK?xxwidgets_widget_set_text(p->color_back,b):s;
}
static int read_fields(extra_page *p,int commit,char *error,size_t capacity)
{
    size_t i;char text[GUI_PREFERENCE_PATH_MAX];
    for(i=0;i<p->field_count;++i){extra_field*f=&p->fields[i];int value=0;unsigned number=0;size_t j;
        if(f->kind==FIELD_BOOL||f->kind==FIELD_COMBO){if(xxwidgets_widget_get_value(f->control,&value)!=XXWIDGETS_OK || value<0)goto invalid;if(commit)*(int*)f->value=value;}
        else {size_t cap=f->kind==FIELD_UINT?sizeof(text):f->capacity;if(cap>sizeof(text))goto invalid;
            if(xxwidgets_widget_get_text(f->control,text,cap,NULL)!=XXWIDGETS_OK)goto invalid;
            if(f->kind==FIELD_UINT){if(!text[0])goto invalid;for(j=0;text[j];++j){if(text[j]<'0'||text[j]>'9'||number>96)goto invalid;number=number*10+(unsigned)(text[j]-'0');}if(number && (number<4||number>96))goto invalid;if(commit)*(unsigned*)f->value=number;}
            else if(commit)memcpy(f->value,text,strlen(text)+1);
        }
    }
    if(!save_color(p))goto invalid;
    if(commit && p->color_select)memcpy(p->config->values->disasm.colors,p->colors,sizeof(p->colors));
    return 1;
invalid:
    if(error&&capacity)xx_rt_snprintf(error,capacity,"%s",CDIE_TR("Check the field length, font size (0 or 4..96), and colors (#RRGGBB or Default)."));
    return 0;
}
static xxwidgets_status destroy(void *state)
{
    extra_page*p=state;xxwidgets_status status=XXWIDGETS_OK;
    while(p->widget_count){xxwidgets_status s=xxwidgets_widget_destroy(p->widgets[--p->widget_count]);if(status==XXWIDGETS_OK)status=s;}
    free(p);return status;
}
#define BOOL(label,member) do { if(s==XXWIDGETS_OK)s=field(p,FIELD_BOOL,label,&v->member,&d->member,0,NULL); } while(0)
#define TEXT(label,member) do { if(s==XXWIDGETS_OK)s=field(p,FIELD_TEXT,label,v->member,d->member,sizeof(v->member),NULL); } while(0)
static xxwidgets_status create(xxwidgets_widget *dialog,xxwidgets_rect bounds,void *user,void **out)
{
    extra_page*p=calloc(1,sizeof(*p));gui_options_extra_values*c=user;gui_options_values*v=c->values;const gui_options_values*d=c->defaults;
    xxwidgets_status s=XXWIDGETS_OK;xxwidgets_widget*w=NULL;
    if(!p)return XXWIDGETS_OUT_OF_MEMORY;p->config=c;p->dialog=dialog;p->bounds=bounds;
    switch(c->id){
    case GUI_OPTIONS_APPEARANCE:
        BOOL(CDIE_N_("Stay on top"),appearance.stay_on_top);BOOL(CDIE_N_("Single application"),appearance.single_application);
        TEXT(CDIE_N_("Style"),appearance.style);if(s==XXWIDGETS_OK)s=xxwidgets_widget_set_enabled(p->fields[p->field_count-1].control,0);
        TEXT(CDIE_N_("QSS"),appearance.qss);if(s==XXWIDGETS_OK)s=xxwidgets_widget_set_enabled(p->fields[p->field_count-1].control,0);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("System native appearance; Qt styles and QSS are unavailable."),0,p->row,bounds.width,1,NULL);p->row+=2;
        TEXT(CDIE_N_("Language (.po / System)"),appearance.language);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_BUTTON,CDIE_N_("Choose translation..."),23,p->row,25,1,&p->browse);p->row+=2;
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("Single application and language changes apply after restarting."),0,p->row,bounds.width,1,NULL);
        p->row+=2;if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,"",0,p->row,bounds.width,1,&p->message);
        break;
    case GUI_OPTIONS_FILE:
        BOOL(CDIE_N_("Save last directory"),file.save_last_directory);BOOL(CDIE_N_("Save history"),file.save_recent_files);BOOL(CDIE_N_("Save backup"),file.save_backup);
        BOOL(CDIE_N_("Add to context menu"),file.context_menu);
#ifndef _WIN32
        if(s==XXWIDGETS_OK)s=xxwidgets_widget_set_enabled(p->fields[p->field_count-1].control,0);
#endif
        BOOL(CDIE_N_("Set system environment variable"),file.set_environment);
#ifndef _WIN32
        if(s==XXWIDGETS_OK)s=xxwidgets_widget_set_enabled(p->fields[p->field_count-1].control,0);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("Explorer context menus and user PATH registration are Windows-only."),0,p->row,bounds.width,1,NULL);
#endif
        break;
    case GUI_OPTIONS_SIGNATURES:
        TEXT(CDIE_N_("Search signatures"),signatures.path);
        if(s==XXWIDGETS_OK)s=xxwidgets_widget_set_enabled(p->fields[p->field_count-1].control,0);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("The Qt search-signatures browser is unavailable in this native build."),0,p->row,bounds.width,1,NULL);
        break;
    case GUI_OPTIONS_ONLINE:
        TEXT(CDIE_N_("VirusTotal API key"),online.virustotal_api_key);
        if(s==XXWIDGETS_OK)s=xxwidgets_widget_set_enabled(p->fields[p->field_count-1].control,0);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_BUTTON,CDIE_N_("Get API key"),23,p->row,18,1,&p->update);p->row+=2;
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("The Qt VirusTotal API report widget is unavailable in this native build."),0,p->row,bounds.width,1,NULL);
        p->row+=2;if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("Get API key opens the VirusTotal page in the web browser."),0,p->row,bounds.width,1,&p->message);
        break;
    case GUI_OPTIONS_INFO:
        TEXT(CDIE_N_("Info database"),info.path);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_BUTTON,CDIE_N_("Browse..."),23,p->row,16,1,&p->browse);p->row+=2;
        if(s==XXWIDGETS_OK&&!c->host->browse)s=xxwidgets_widget_set_enabled(p->browse,0);
        TEXT(CDIE_N_("Database update URL"),info.update_url);
        /* Only shows the address; nothing is downloaded from here. */
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_BUTTON,CDIE_N_("Open page"),23,p->row,16,1,&p->update);
        p->row+=2;if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("Open page shows this address in the web browser."),0,p->row,bounds.width,1,&p->message);
        break;
    case GUI_OPTIONS_DISASM: {
        const char*syntax[]={CDIE_N_("Intel"),CDIE_N_("AT&T")};const char*names[XXWIDGETS_DISASM_COLOR_COUNT];size_t i;
        s=field(p,FIELD_COMBO,CDIE_N_("Syntax"),&v->disasm.syntax,&d->disasm.syntax,0,&w);
        if(s==XXWIDGETS_OK)s=choices(w,syntax,2);if(s==XXWIDGETS_OK)s=xxwidgets_widget_set_value(w,v->disasm.syntax);
        BOOL(CDIE_N_("Uppercase"),disasm.uppercase);BOOL(CDIE_N_("Show colons in addresses"),disasm.location_colon);BOOL(CDIE_N_("Highlight"),disasm.highlight);
        TEXT(CDIE_N_("Font family"),disasm.font.family);
        if(s==XXWIDGETS_OK)s=field(p,FIELD_UINT,CDIE_N_("Font size"),&v->disasm.font.point_size,&d->disasm.font.point_size,0,NULL);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_BUTTON,CDIE_N_("Choose font..."),23,p->row,20,1,&p->choose);p->row+=2;
        /* Without a native chooser (Cocoa, the terminal) the family and size are typed. */
        if(s==XXWIDGETS_OK&&!xxwidgets_font_choose_available(xxwidgets_widget_app(dialog)))s=xxwidgets_widget_set_enabled(p->choose,0);
        for(i=0;i<XXWIDGETS_DISASM_COLOR_COUNT;++i)names[i]=xxwidgets_disasm_color_name((xxwidgets_disasm_color_id)i);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("Color category"),0,p->row,22,1,NULL);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_COMBOBOX,"",23,p->row,bounds.width-23,2,&p->color_select);
        if(s==XXWIDGETS_OK)s=choices(p->color_select,names,XXWIDGETS_DISASM_COLOR_COUNT);p->row+=2;
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("Text color"),0,p->row,22,1,NULL);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_EDIT,"",23,p->row,18,1,&p->color_fore);p->row+=2;
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("Background color"),0,p->row,22,1,NULL);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_EDIT,"",23,p->row,18,1,&p->color_back);p->row+=2;
        memcpy(p->colors,v->disasm.colors,sizeof(p->colors));if(s==XXWIDGETS_OK)s=show_color(p);
        if(s==XXWIDGETS_OK)s=control(p,XXWIDGETS_LABEL,CDIE_N_("#RRGGBB or Default"),0,p->row,bounds.width,1,&p->message);
        break;}
    default:s=XXWIDGETS_INVALID_ARGUMENT;break;
    }
    if(s!=XXWIDGETS_OK){destroy(p);return s;}*out=p;return s;
}
static void event(void *state,const xxwidgets_event*e)
{
    extra_page*p=state;
    if(e->type==XXWIDGETS_EVENT_CLICK){if(e->widget==p->browse)p->pending=1;else if(e->widget==p->update)p->pending=2;else if(e->widget==p->choose)p->pending=3;}
    if(e->type==XXWIDGETS_EVENT_SELECT && e->widget==p->color_select)p->pending=4;
}
/* A page action reports on the page and returns OK: an error from process
 * ends the whole Options dialog, and every edit on every page with it. */
static xxwidgets_status page_message(extra_page*p,const char*text){return p->message?xxwidgets_widget_set_text(p->message,text):XXWIDGETS_OK;}
static xxwidgets_status page_failure(extra_page*p,const char*action,xxwidgets_status failure)
{
    char text[256];xx_rt_snprintf(text,sizeof(text),"%s %s",CDIE_TR(action),gui_error_status_text(failure));return page_message(p,text);
}
static xxwidgets_status open_page(extra_page*p,const char*url)
{
    char text[GUI_PREFERENCE_URL_MAX+160];xxwidgets_status s;
    if(!url[0])return page_message(p,CDIE_TR("Enter the address first."));
    s=xxwidgets_open_url(p->dialog,url);if(s==XXWIDGETS_OK)return s;
    if(s==XXWIDGETS_INVALID_ARGUMENT)return page_message(p,CDIE_TR("Enter an http:// or https:// address without spaces."));
    cdie_translation_snprintf(text,sizeof(text),CDIE_N_("No web browser opened; visit %s yourself."),url);return page_message(p,text);
}
static xxwidgets_status process(void*state)
{
    extra_page*p=state;gui_options_extra_values*c=p->config;int pending=p->pending,accepted=0;char*path=NULL;xxwidgets_status s=XXWIDGETS_OK;
    p->pending=0;if(!pending)return s;
    if(pending==4){int selected=0;s=xxwidgets_widget_get_value(p->color_select,&selected);if(s!=XXWIDGETS_OK)return s;if(!save_color(p)){(void)xxwidgets_widget_set_value(p->color_select,p->selected_color);return xxwidgets_widget_set_text(p->message,CDIE_TR("Use #RRGGBB or Default before selecting another color."));}if(selected>=0&&selected<XXWIDGETS_DISASM_COLOR_COUNT){p->selected_color=selected;return show_color(p);}return XXWIDGETS_OK;}
    if(pending==3){xxwidgets_disasm_options o=c->values->disasm;if(!read_fields(p,1,NULL,0))return page_message(p,CDIE_TR("Fix the font size (0 or 4..96) and the colors first."));
        o=c->values->disasm;s=xxwidgets_font_choose_dialog(p->dialog,XXWIDGETS_FONT_DISASM_VIEWS,&o.font,&accepted);
        if(s==XXWIDGETS_OK&&accepted){c->values->disasm.font=o.font;for(size_t i=0;i<p->field_count;++i)if(p->fields[i].value==c->values->disasm.font.family||p->fields[i].value==&c->values->disasm.font.point_size)s=fill_field(&p->fields[i],0);return s;}
        if(s==XXWIDGETS_OK)return s;
        /* A refused font (GTK: over 96 or under 4 points, absolute sizes) or no chooser. */
        return page_message(p,s==XXWIDGETS_INVALID_ARGUMENT||s==XXWIDGETS_PLATFORM_ERROR?CDIE_TR("The chosen font cannot be used; pick a size of 4..96 points."):
            CDIE_TR("No font chooser is available; type the family and size."));}
    if(pending==2){char url[GUI_PREFERENCE_URL_MAX];if(c->id==GUI_OPTIONS_ONLINE)strcpy(url,"https://www.virustotal.com/gui/my-apikey");else if(xxwidgets_widget_get_text(p->fields[1].control,url,sizeof(url),NULL)!=XXWIDGETS_OK)return XXWIDGETS_OK;return open_page(p,url);}
    if(pending==1){extra_field*f=&p->fields[c->id==GUI_OPTIONS_APPEARANCE?4:0];char initial[GUI_PREFERENCE_PATH_MAX];if(xxwidgets_widget_get_text(f->control,initial,sizeof(initial),NULL)!=XXWIDGETS_OK)return XXWIDGETS_OK;
        if(c->id==GUI_OPTIONS_APPEARANCE){
            /* No catalog chosen yet: start among the bundled ones (on macOS
             * inside the application bundle, which the picker cannot browse). */
            char*bundled=!initial[0]||!strcmp(initial,"System")?cdie_translations_dir():NULL;
            s=xxwidgets_file_dialog(p->dialog,XXWIDGETS_FILE_DIALOG_OPEN,CDIE_TR("Choose translation"),bundled?bundled:initial,&path,&accepted);
            xx_rt_free(bundled);}
        else s=gui_options_browse_for(c->host,GUI_OPTIONS_INFO,p->dialog,initial,&path,&accepted);
        if(s==XXWIDGETS_OK&&accepted&&path)s=xxwidgets_widget_set_text(f->control,path);free(path);
        return s==XXWIDGETS_OK?s:page_failure(p,CDIE_N_("No chooser opened; type the path."),s);}
    return s;
}
static xxwidgets_status reset(void*state)
{
    extra_page*p=state;xxwidgets_status s=XXWIDGETS_OK;size_t i;
    for(i=0;s==XXWIDGETS_OK&&i<p->field_count;++i)s=fill_field(&p->fields[i],1);
    if(p->color_select){memcpy(p->colors,p->config->defaults->disasm.colors,sizeof(p->colors));if(s==XXWIDGETS_OK)s=show_color(p);}return s;
}
static int extra_validate(void*state,char*error,size_t capacity){return read_fields(state,0,error,capacity);}
static int apply(void*state,char*error,size_t capacity){return read_fields(state,1,error,capacity);}
xxwidgets_status gui_options_extra_page(xxwidgets_options_page*page,const char*title,gui_options_extra_values*values)
{
    if(!page||!title||!values)return XXWIDGETS_INVALID_ARGUMENT;memset(page,0,sizeof(*page));page->title=title;page->width=68;page->height=values->id==GUI_OPTIONS_DISASM?23:values->id==GUI_OPTIONS_APPEARANCE?18:12;
    page->create=create;page->event=event;page->process=process;page->reset=reset;page->validate=extra_validate;page->apply=apply;page->destroy=destroy;page->user=values;return XXWIDGETS_OK;
}
