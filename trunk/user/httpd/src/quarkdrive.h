/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HTTPD_QUARKDRIVE_H
#define HTTPD_QUARKDRIVE_H
#ifdef APP_QUARKDRIVE
const char *quarkdrive_validate(webs_t wp);
int quarkdrive_status_hook(int eid, webs_t wp, int argc, char **argv);
int quarkdrive_log_hook(int eid, webs_t wp, int argc, char **argv);
void quarkdrive_control(webs_t wp);
#endif
#endif
