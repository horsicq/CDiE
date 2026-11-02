/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_about.h"
#include "gui_backend.h"
#include "gui_png.h"
#include "app/cdie_translation.h"
#include <xxfclib/rt/xx_rt.h>

static const char license[] =
    "MIT License\n\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy "
    "of this software and associated documentation files (the Software), to deal "
    "in the Software without restriction, including without limitation the rights "
    "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
    "copies of the Software, and to permit persons to whom the Software is "
    "furnished to do so, subject to the following conditions:\n\n"
    "The above copyright notice and this permission notice shall be included in all "
    "copies or substantial portions of the Software.\n\n"
    "THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
    "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
    "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
    "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
    "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";

xxwidgets_status gui_about_create(const unsigned char *png, size_t png_size, xxwidgets_about_dialog **out)
{
    /* Values that are not URLs (PayPal, BTC) are copied to the clipboard on click. */
    static const char *const social[][2] = {
        {"GitHub", "https://github.com/horsicq"}, {"Twitter", "https://twitter.com/horsicq"},
        {"YouTube", "https://www.youtube.com/@funreverseengineering"}};
    static const char *const info[][3] = {
        {"Bugreports:", "horsicq@gmail.com", "mailto:horsicq@gmail.com"},
        {"Website:", "http://ntinfo.biz", "http://ntinfo.biz"},
        {"Donate(Paypal):", "ntinfo.re@gmail.com", "ntinfo.re@gmail.com"},
        {"Donate(BTC):", "3DqddVBX9PKqMvNPXZ3gPHBNNRtD9CnmJo", "3DqddVBX9PKqMvNPXZ3gPHBNNRtD9CnmJo"},
        {"Source code:", "https://github.com/horsicq/DIE-engine", "https://github.com/horsicq/DIE-engine"}};
    static const char *const libraries[][2] = {
        {"xxfclib:", "formats, scan engine and runtime"}, {"xxwidgets:", "native widgets and dialogs"}, {"cdisasm:", "disassembler"}};
    xxwidgets_about_dialog *about = NULL;
    const char *version = gui_backend_app_title();
    char line[128];
    xxwidgets_status status;
    size_t i;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    while (*version && *version != ' ') ++version;
    if (*version) ++version;
    status = xxwidgets_about_dialog_create(&about);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_TITLE, CDIE_TR("About"));
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_CLOSE_LABEL, CDIE_TR("OK"));
    /* Text-only layouts show these fields followed by the items below. */
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_PROGRAM_NAME, "Detect It Easy - cdie");
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_VERSION, version);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_LICENSE, license);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_BUILD_DATE, __DATE__);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_THANKS_URL, "https://github.com/horsicq/DIE-engine/blob/master/THANKS.md");
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(about, XXWIDGETS_ABOUT_UPDATES_URL, "https://github.com/horsicq/DIE-engine/releases");
    for (i = 0; status == XXWIDGETS_OK && i < sizeof(social) / sizeof(social[0]); ++i)
        status = xxwidgets_about_dialog_add_item(about, XXWIDGETS_ABOUT_ITEM_SOCIAL, social[i][0], NULL, social[i][1]);
    xx_rt_snprintf(line, sizeof(line), "Detect It Easy (cdie) v%s", version);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_add_item(about, XXWIDGETS_ABOUT_ITEM_INFO, line, NULL, NULL);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_add_item(about, XXWIDGETS_ABOUT_ITEM_INFO, "Copyright (C) 2006-2008 Hellsp@wn", NULL, NULL);
    /* __DATE__ is "Mmm dd yyyy": the copyright runs to the build year. */
    xx_rt_snprintf(line, sizeof(line), "Copyright (C) 2012-%s Hors", &__DATE__[7]);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_add_item(about, XXWIDGETS_ABOUT_ITEM_INFO, line, NULL, NULL);
    for (i = 0; status == XXWIDGETS_OK && i < sizeof(info) / sizeof(info[0]); ++i)
        status = xxwidgets_about_dialog_add_item(about, XXWIDGETS_ABOUT_ITEM_INFO, info[i][0], info[i][1], info[i][2]);
    for (i = 0; status == XXWIDGETS_OK && i < sizeof(libraries) / sizeof(libraries[0]); ++i)
        status = xxwidgets_about_dialog_add_item(about, XXWIDGETS_ABOUT_ITEM_LIBRARY, libraries[i][0], libraries[i][1], NULL);
    if (status == XXWIDGETS_OK && png) {
        unsigned char *rgba = NULL;
        unsigned int width = 0, height = 0;
        /* An undecodable picture leaves the text layout intact. */
        if (gui_png_decode_rgba(png, png_size, &rgba, &width, &height)) {
            status = xxwidgets_about_dialog_set_image(about, rgba, width, height, (size_t)width * 4);
            xx_rt_free(rgba);
        }
    }
    if (status != XXWIDGETS_OK) {
        if (about) xxwidgets_about_dialog_destroy(about);
        return status;
    }
    *out = about;
    return XXWIDGETS_OK;
}
