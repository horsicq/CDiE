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
#ifndef CDIE_UPDATER_H
#define CDIE_UPDATER_H

enum { UPD_IDLE, UPD_CHECKING, UPD_READY, UPD_UPTODATE, UPD_RUNNING, UPD_DONE, UPD_ERROR };

typedef struct Updater {
  volatile int phase, pct, cancel, busy, db_changed;
  char installed[32], latest[32], url[512], label[48], error[200];
  int added, changed;

  char main_db[1024], extra_db[1024], custom_db[1024];
  void (*notify)(void* user);
  void* user;
  void* thread;
  int job;
} Updater;

void updater_init(Updater* u, void (*notify)(void*), void* user);
void updater_check(Updater* u);     
void updater_install(Updater* u);    
void updater_cancel(Updater* u);
void updater_shutdown(Updater* u); 

#endif