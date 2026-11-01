/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_options.h"
#include "gui_options_extra.h"
#include "gui_backend.h"
#include "app/cdie_translation.h"
#include <xxfclib/rt/xx_rt.h>
#include <stdlib.h>
#include <string.h>

typedef struct options_session {
    const gui_options_host *host;
    gui_options_values *edited;
    xxwidgets_optimization_page_values *optimization;
    gui_backend_optimization_options previous_optimization;
    gui_backend_scan_preferences previous_scan;
    char previous_paths[GUI_DB_COUNT][GUI_PREFERENCE_PATH_MAX];
    int committed, reloaded;
} options_session;
static xxwidgets_status browse_database(xxwidgets_widget *dialog,const char*initial,char**path,int*accepted,void*user)
{
    options_session*s=user;
    return s->host->browse?s->host->browse(dialog,initial,path,accepted,s->host->user):XXWIDGETS_UNAVAILABLE;
}
static void optimization_to_page(const gui_backend_optimization_options *from, const gui_backend_optimization_capabilities *features,
                                 xxwidgets_optimization_page_values *to)
{
    to->options.buffer_size = from->buffer_size;
    to->options.file_buffer_size = from->file_buffer_size;
    to->options.use_sse2 = from->use_sse2; to->options.use_avx2 = from->use_avx2; to->options.use_sse42 = from->use_sse42;
    to->options.use_aes = from->use_aes; to->options.use_sha = from->use_sha; to->options.use_vpclmulqdq = from->use_vpclmulqdq;
    to->options.use_neon = from->use_neon; to->options.use_arm_crc32 = from->use_arm_crc32; to->options.use_arm_crypto = from->use_arm_crypto;
    to->options.use_arm_dotprod = from->use_arm_dotprod; to->options.use_arm_pmull = from->use_arm_pmull;
    to->capabilities.sse2 = features->sse2; to->capabilities.avx2 = features->avx2; to->capabilities.sse42 = features->sse42;
    to->capabilities.aes = features->aes; to->capabilities.sha = features->sha; to->capabilities.vpclmulqdq = features->vpclmulqdq;
    to->capabilities.neon = features->neon; to->capabilities.arm_crc32 = features->arm_crc32; to->capabilities.arm_crypto = features->arm_crypto;
    to->capabilities.arm_dotprod = features->arm_dotprod; to->capabilities.arm_pmull = features->arm_pmull;
}

static void optimization_from_page(const xxwidgets_optimization_options *from, gui_backend_optimization_options *to)
{
    to->buffer_size = from->buffer_size;
    to->file_buffer_size = from->file_buffer_size;
    to->use_sse2 = from->use_sse2; to->use_avx2 = from->use_avx2; to->use_sse42 = from->use_sse42;
    to->use_aes = from->use_aes; to->use_sha = from->use_sha; to->use_vpclmulqdq = from->use_vpclmulqdq;
    to->use_neon = from->use_neon; to->use_arm_crc32 = from->use_arm_crc32; to->use_arm_crypto = from->use_arm_crypto;
    to->use_arm_dotprod = from->use_arm_dotprod; to->use_arm_pmull = from->use_arm_pmull;
}


/* The last page commits shared backend state only after every form validated
 * and copied into the private edit model. A failed database load leaves both
 * databases and scan behavior unchanged and keeps Options open. */
typedef struct commit_binding { xxwidgets_options_page page; options_session *session; } commit_binding;
typedef struct commit_state { commit_binding *binding; void *page_state; } commit_state;
static xxwidgets_status commit_create(xxwidgets_widget*w,xxwidgets_rect b,void*u,void**out)
{
    commit_binding*c=u;commit_state*s=calloc(1,sizeof(*s));xxwidgets_status status;
    if(!s)return XXWIDGETS_OUT_OF_MEMORY;s->binding=c;
    status=c->page.create(w,b,c->page.user,&s->page_state);
    if(status!=XXWIDGETS_OK){free(s);return status;}*out=s;return status;
}
static void commit_event(void*u,const xxwidgets_event*e){commit_state*s=u;if(s->binding->page.event)s->binding->page.event(s->page_state,e);}
static xxwidgets_status commit_process(void*u){commit_state*s=u;return s->binding->page.process?s->binding->page.process(s->page_state):XXWIDGETS_OK;}
static xxwidgets_status commit_reset(void*u){commit_state*s=u;return s->binding->page.reset?s->binding->page.reset(s->page_state):XXWIDGETS_OK;}
static int commit_validate(void*u,char*e,size_t n){commit_state*s=u;return !s->binding->page.validate||s->binding->page.validate(s->page_state,e,n);}
/* The backend's reason in the user's language, naming the folder concerned. */
static void configuration_error(const options_session*c,char*error,size_t capacity)
{
    const char*paths[GUI_DB_COUNT]={c->edited->databases.main_path,c->edited->databases.custom_path};int slot=-1;
    gui_backend_configuration_failure failure=gui_backend_configuration_failed(&slot);
    if(!error||!capacity)return;
    if(failure==GUI_BACKEND_CONFIGURATION_OPTIONS)xx_rt_snprintf(error,capacity,"%s",CDIE_TR("These scan options cannot be used: the scan engine is not available."));
    else if(failure!=GUI_BACKEND_CONFIGURATION_OK&&slot>=0&&slot<GUI_DB_COUNT)
        gui_scan_options_database_error(slot,failure==GUI_BACKEND_CONFIGURATION_MISSING,paths[slot],error,capacity);
}
/* With the commit on the Scan page a later page can still reject OK; a second
 * OK commits again, so a reload is remembered until the dialog ends. */
static int commit_apply(void*u,char*error,size_t capacity)
{
    commit_state*s=u;options_session*c=s->binding->session;gui_backend_scan_preferences scan;
    gui_backend_optimization_options optimization;int changed;
    if(s->binding->page.apply&&!s->binding->page.apply(s->page_state,error,capacity))return 0;
    scan.recursive=c->edited->scan.recursive;scan.profiling=c->edited->scan.profiling;scan.sort=c->edited->scan.sort;scan.use_cache=c->edited->scan.use_cache;
    memcpy(scan.engine,c->edited->scan.engine,sizeof(scan.engine));
    changed=!gui_backend_db_paths_current(c->edited->databases.main_path,c->edited->databases.custom_path);
    if(!gui_backend_apply_configuration(&scan,c->edited->databases.main_path,c->edited->databases.custom_path,error,capacity)){
        configuration_error(c,error,capacity);return 0;}
    optimization_from_page(&c->optimization->options,&optimization);
    if(!gui_backend_set_optimization_options(&optimization)) {
        (void)gui_backend_apply_configuration(&c->previous_scan,c->previous_paths[GUI_DB_MAIN],c->previous_paths[GUI_DB_CUSTOM],NULL,0);
        if(error&&capacity)xx_rt_snprintf(error,capacity,"%s",CDIE_TR("Cannot apply optimization settings."));c->reloaded=0;return 0;
    }
    c->reloaded|=changed;c->committed=1;return 1;
}
static xxwidgets_status commit_destroy(void*u){commit_state*s=u;xxwidgets_status result=s->binding->page.destroy?s->binding->page.destroy(s->page_state):XXWIDGETS_OK;free(s);return result;}
static void bind_commit(xxwidgets_options_page*page,commit_binding*binding,options_session*session)
{
    binding->page=*page;binding->session=session;page->create=commit_create;page->event=commit_event;page->process=commit_process;
    page->reset=commit_reset;page->validate=commit_validate;page->apply=commit_apply;page->destroy=commit_destroy;page->user=binding;
}
xxwidgets_status gui_options_dialog(xxwidgets_widget*owner,const gui_options_host*host,gui_options_page_id*page,
    gui_options_values*values,int*accepted,int*databases_reloaded)
{
    static const gui_options_page_id order[]={GUI_OPTIONS_APPEARANCE,GUI_OPTIONS_FILE,GUI_OPTIONS_FONTS,GUI_OPTIONS_OPTIMIZATION,
        GUI_OPTIONS_SCAN,GUI_OPTIONS_SIGNATURES,GUI_OPTIONS_HEX,GUI_OPTIONS_DISASM,GUI_OPTIONS_ONLINE,GUI_OPTIONS_INFO};
    xxwidgets_options_page pages[GUI_OPTIONS_PAGE_COUNT];gui_options_page_id ids[GUI_OPTIONS_PAGE_COUNT];
    gui_options_extra_values extra[GUI_OPTIONS_PAGE_COUNT];gui_options_values edited,defaults;
    xxwidgets_hex_page_values hex;xxwidgets_optimization_page_values optimization;gui_backend_optimization_capabilities features;
    xxwidgets_database_paths_callbacks database_callbacks={0};gui_scan_options_page_context scan_context;commit_binding binding;options_session session={0};
    xxwidgets_status status=XXWIDGETS_OK;size_t count=0,shown=0,i;
    if(accepted)*accepted=0;if(databases_reloaded)*databases_reloaded=0;
    if(!owner||!host||!page||!values||!accepted||!databases_reloaded)return XXWIDGETS_INVALID_ARGUMENT;
    if(!gui_backend_get_optimization_options(&session.previous_optimization)||!gui_backend_get_optimization_capabilities(&features)||
       !gui_backend_get_scan_preferences(&session.previous_scan))return XXWIDGETS_PLATFORM_ERROR;
    session.host=host;session.edited=&edited;session.optimization=&optimization;edited=*values;gui_options_values_init(&defaults);
    /* A rollback restores default slots as defaults ("") rather than pinning their current folder. */
    xx_rt_snprintf(session.previous_paths[GUI_DB_MAIN],sizeof(session.previous_paths[GUI_DB_MAIN]),"%s",gui_backend_db_path_is_default(GUI_DB_MAIN)?"":gui_backend_main_db_path());
    xx_rt_snprintf(session.previous_paths[GUI_DB_CUSTOM],sizeof(session.previous_paths[GUI_DB_CUSTOM]),"%s",gui_backend_db_path_is_default(GUI_DB_CUSTOM)?"":gui_backend_custom_db_path());
    xx_rt_snprintf(edited.databases.main_path,sizeof(edited.databases.main_path),"%s",gui_backend_db_path_is_default(GUI_DB_MAIN)?"":gui_backend_main_db_path());
    xx_rt_snprintf(edited.databases.custom_path,sizeof(edited.databases.custom_path),"%s",gui_backend_db_path_is_default(GUI_DB_CUSTOM)?"":gui_backend_custom_db_path());
    hex.options=edited.hex;hex.show_editable=0;optimization_to_page(&session.previous_optimization,&features,&optimization);
    database_callbacks.browse=host->browse?browse_database:NULL;
    scan_context=(gui_scan_options_page_context){&edited.scan,&edited.databases,database_callbacks,&session};
#ifdef _WIN32
    if(host->executable){xxwidgets_context_config context={"cdie","Scan with cdie","*",host->executable,XXWIDGETS_CONTEXT_CURRENT_USER};
        int registered=0;if(xxwidgets_context_is_registered(&context,&registered)==XXWIDGETS_OK)edited.file.context_menu=registered;}
#endif
    for(i=0;status==XXWIDGETS_OK&&i<GUI_OPTIONS_PAGE_COUNT;++i){gui_options_page_id id=order[i];
        if(!(host->pages&GUI_OPTIONS_PAGE(id)))continue;
        extra[count]=(gui_options_extra_values){id,&edited,&defaults,host};
        switch(id){
        case GUI_OPTIONS_FONTS:status=xxwidgets_options_page_fonts(&pages[count],CDIE_TR("Fonts"),&edited.fonts);pages[count].height=23;break;
        case GUI_OPTIONS_SCAN:status=gui_scan_options_page_ex(&pages[count],CDIE_TR("Scan"),&scan_context);break;
        case GUI_OPTIONS_HEX:status=xxwidgets_options_page_hex(&pages[count],CDIE_TR("Hex"),&hex);break;
        case GUI_OPTIONS_OPTIMIZATION:status=xxwidgets_options_page_optimization(&pages[count],CDIE_TR("Features"),&optimization);break;
        default:{const char*title=id==GUI_OPTIONS_APPEARANCE?CDIE_N_("Appearance"):id==GUI_OPTIONS_FILE?CDIE_N_("File"):id==GUI_OPTIONS_SIGNATURES?CDIE_N_("Signatures"):
                id==GUI_OPTIONS_DISASM?CDIE_N_("Disasm"):id==GUI_OPTIONS_ONLINE?CDIE_N_("Online tools"):CDIE_N_("Info");
            status=gui_options_extra_page(&pages[count],CDIE_TR(title),&extra[count]);break;}
        }
        if(status==XXWIDGETS_OK){if(id==*page)shown=count;ids[count++]=id;}
    }
    if(status==XXWIDGETS_OK&&!count)status=XXWIDGETS_INVALID_ARGUMENT;
    /* The commit rides on the Scan page when there is one, so a database that
     * fails to load is reported where its folders are edited. */
    if(status==XXWIDGETS_OK){size_t commit=count-1;for(i=0;i<count;++i)if(ids[i]==GUI_OPTIONS_SCAN)commit=i;
        bind_commit(&pages[commit],&binding,&session);status=xxwidgets_options_pages_dialog(owner,CDIE_TR("Options"),pages,count,&shown,accepted);}
    if(status==XXWIDGETS_OK&&count)*page=ids[shown];
    if(status!=XXWIDGETS_OK||!*accepted){
        if(session.committed){(void)gui_backend_apply_configuration(&session.previous_scan,session.previous_paths[GUI_DB_MAIN],session.previous_paths[GUI_DB_CUSTOM],NULL,0);(void)gui_backend_set_optimization_options(&session.previous_optimization);}
        *accepted=0;return status;
    }
    edited.hex=hex.options;
    if(!memcmp(&edited.hex.font,&values->hex.font,sizeof(edited.hex.font)))edited.hex.font=edited.fonts.fonts[XXWIDGETS_FONT_HEX_VIEWS];
    else edited.fonts.fonts[XXWIDGETS_FONT_HEX_VIEWS]=edited.hex.font;
    if(!memcmp(&edited.disasm.font,&values->disasm.font,sizeof(edited.disasm.font)))edited.disasm.font=edited.fonts.fonts[XXWIDGETS_FONT_DISASM_VIEWS];
    else edited.fonts.fonts[XXWIDGETS_FONT_DISASM_VIEWS]=edited.disasm.font;
    if(!edited.file.save_last_directory)memset(edited.file.last_directory,0,sizeof(edited.file.last_directory));
    if(!edited.file.save_recent_files)memset(edited.file.recent_files,0,sizeof(edited.file.recent_files));
    *databases_reloaded=session.reloaded;*values=edited;return status;
}
