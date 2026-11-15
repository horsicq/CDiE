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
#ifndef CDIE_RECENT_H
#define CDIE_RECENT_H

#ifdef __cplusplus
extern "C" {
#endif

#define RECENT_MAX 10

	void        recent_init(void);              
	void        recent_shutdown(void);           
	void        recent_add(const char* path);    
	void        recent_clear(void);              
	int         recent_count(void);
	const char* recent_get(int index);           

#ifdef _WIN32
	typedef struct {
		void (*on_pick)(const char* path, void* user);   
		void (*on_open)(void* user);                     
		void (*on_clear)(void* user);                    
		void* user;
	} RecentFlyoutHost;

	void recent_flyout_create(void* parent_native, const RecentFlyoutHost* host);
	int  recent_flyout_toggle(int x, int bottom);   
	int  recent_flyout_is_open(void);
	void recent_flyout_close(void);
	void recent_flyout_filter(void* msg);          
#endif

#ifdef __cplusplus
}
#endif
#endif