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
#include "jumplist.h"

#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shlobj.h>


#define JL_PATH 1024

/* Same value as PKEY_Title, spelled out so no extra GUID library is needed. */
static const PROPERTYKEY k_title = {
	{ 0xF29F85E0, 0x4FF9, 0x1068, { 0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9 } }, 2
};

void jumplist_init(void)
{
	SetCurrentProcessExplicitAppUserModelID(L"hors.cdie");
}

static IShellLinkW* make_link(const WCHAR* exe, const char* utf8)
{
	IShellLinkW* link = NULL;
	IPropertyStore* store = NULL;
	WCHAR path[JL_PATH], args[JL_PATH + 4];
	const WCHAR* name = path, * p;

	if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, path, JL_PATH)) return NULL;
	for (p = path; *p; ++p)
		if (*p == L'\\' || *p == L'/') name = p + 1;

	if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
		&IID_IShellLinkW, (void**)&link))) return NULL;

	wsprintfW(args, L"\"%s\"", path);
	IShellLinkW_SetPath(link, exe);
	IShellLinkW_SetArguments(link, args);
	IShellLinkW_SetIconLocation(link, exe, 0);
	IShellLinkW_SetDescription(link, path);

	if (SUCCEEDED(IShellLinkW_QueryInterface(link, &IID_IPropertyStore, (void**)&store))) {
		PROPVARIANT pv;
		SIZE_T bytes = ((SIZE_T)lstrlenW(name) + 1) * sizeof(WCHAR);
		PropVariantInit(&pv);
		pv.vt = VT_LPWSTR;
		pv.pwszVal = (LPWSTR)CoTaskMemAlloc(bytes);
		if (pv.pwszVal) {
			CopyMemory(pv.pwszVal, name, bytes);
			IPropertyStore_SetValue(store, &k_title, &pv);
			IPropertyStore_Commit(store);
		}
		PropVariantClear(&pv);   /* frees pwszVal */
		IPropertyStore_Release(store);
	}
	return link;
}

void jumplist_update(const char* const* paths, int count)
{
	ICustomDestinationList* list = NULL;
	IObjectArray* removed = NULL;
	IObjectCollection* col = NULL;
	IObjectArray* arr = NULL;
	WCHAR exe[JL_PATH];
	UINT slots = 0;
	int i, added = 0;

	if (!GetModuleFileNameW(NULL, exe, JL_PATH)) return;
	if (FAILED(CoCreateInstance(&CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER,
		&IID_ICustomDestinationList, (void**)&list))) return;

	if (SUCCEEDED(ICustomDestinationList_BeginList(list, &slots,
		&IID_IObjectArray, (void**)&removed))) {

		if (count > 0 && SUCCEEDED(CoCreateInstance(&CLSID_EnumerableObjectCollection,
			NULL, CLSCTX_INPROC_SERVER, &IID_IObjectCollection, (void**)&col))) {
			for (i = 0; i < count && (UINT)added < slots; ++i) {
				IShellLinkW* link = make_link(exe, paths[i]);
				if (!link) continue;
				IObjectCollection_AddObject(col, (IUnknown*)link);
				IShellLinkW_Release(link);
				++added;
			}
			if (added && SUCCEEDED(IObjectCollection_QueryInterface(col,
				&IID_IObjectArray, (void**)&arr))) {
				ICustomDestinationList_AppendCategory(list, L"Recent files", arr);
				IObjectArray_Release(arr);
			}
			IObjectCollection_Release(col);
		}
		ICustomDestinationList_CommitList(list);
		if (removed) IObjectArray_Release(removed);
	}
	ICustomDestinationList_Release(list);
}

#else /* Linux / macOS: nothing to do yet */

void jumplist_init(void) {}
void jumplist_update(const char* const* paths, int count) { (void)paths; (void)count; }

#endif
