/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <arpa/inet.h>
#include <notify_rc.h>
#include "httpd.h"
#include "quarkdrive.h"

#define QUARK_RUNDIR "/tmp/quarkdrive-webdav/"

static const char *config_value(webs_t wp, char *key)
{
	char *value = websGetVar(wp, key, NULL);
	return value ? value : nvram_safe_get(key);
}

/* Validate the entire submitted configuration before changing any NVRAM. */
const char *quarkdrive_validate(webs_t wp)
{
	static const struct { char *key; unsigned long min, max; } numbers[] = {
		{"qd_port", 1, 65535}, {"qd_read_buffer_size", 1, 16777216},
		{"qd_upload_buffer_size", 1, 16777216}, {"qd_cache_size", 0, 100000},
		{"qd_cache_ttl", 0, 2147483647}, {"qd_refresh_cache_secs_interval", 1, 2147483647},
		{"qd_upload_wait_timeout", 0, 2147483647}
	};
	static char *switches[] = {"quarkdrive_enable", "qd_auto_index", "qd_no_trash", "qd_read_only",
		"qd_skip_upload_same_size", "qd_prefer_http_download", "qd_redirect", "qd_debug"};
	static const struct { char *key; size_t max; } strings[] = {
		{"qd_binary", 255}, {"qd_cookie", 4095}, {"qd_auth_user", 255},
		{"qd_auth_password", 255}, {"qd_root", 1023}, {"qd_strip_prefix", 1023}
	};
	unsigned int i;
	const char *value, *p;
	unsigned char address[16];
	int has_user, has_password;
	if (!get_login_safe()) return "A trusted login is required";
	for (i = 0; i < sizeof(strings)/sizeof(strings[0]); i++) {
		value = config_value(wp, strings[i].key);
		if (strlen(value) > strings[i].max || strpbrk(value, "\r\n"))
			return "Text is too long or contains a line break";
	}
	for (i = 0; i < sizeof(switches)/sizeof(switches[0]); i++) {
		value = config_value(wp, switches[i]);
		if (strcmp(value, "0") && strcmp(value, "1")) return "Invalid switch value";
	}
	for (i = 0; i < sizeof(numbers)/sizeof(numbers[0]); i++) {
		unsigned long n;
		value = config_value(wp, numbers[i].key);
		if (!*value || strlen(value) > 10) return "Invalid numeric setting";
		for (p = value; *p; p++) if (!isdigit((unsigned char)*p)) return "Invalid numeric setting";
		/* All accepted maxima are below ULONG_MAX, including on MIPS o32. */
		if (strlen(value) == 10 && strcmp(value, "2147483647") > 0) return "Numeric setting is out of range";
		n = strtoul(value, NULL, 10);
		if (n < numbers[i].min || n > numbers[i].max) return "Numeric setting is out of range";
	}
	value = config_value(wp, "qd_host");
	if (inet_pton(AF_INET, value, address) != 1 && inet_pton(AF_INET6, value, address) != 1)
		return "Listen address must be IPv4 or IPv6";
	if (config_value(wp, "qd_binary")[0] != '/' || config_value(wp, "qd_root")[0] != '/')
		return "Binary path and drive root must start with /";
	value = config_value(wp, "qd_strip_prefix");
	if (*value && *value != '/') return "URL prefix must start with /";
	has_user = config_value(wp, "qd_auth_user")[0] != '\0';
	has_password = config_value(wp, "qd_auth_password")[0] != '\0';
	if (has_user != has_password) return "Specify both WebDAV username and password";
	if (!strcmp(config_value(wp, "quarkdrive_enable"), "1") && !config_value(wp, "qd_cookie")[0])
		return "QuarkDrive cookie is required";
	return NULL;
}

static int read_line(const char *path, char *buffer, size_t size)
{
	FILE *fp = fopen(path, "r");
	int ok;
	if (!fp) return 0;
	ok = fgets(buffer, size, fp) != NULL;
	fclose(fp);
	if (ok) buffer[strcspn(buffer, "\r\n")] = '\0';
	return ok;
}

/* Custom binary names can exceed Linux's 15-byte comm field. Check the exe. */
static int service_pid(void)
{
	char pid_text[24], path[64], expected[PATH_MAX], actual[PATH_MAX];
	char *end;
	long pid;
	ssize_t n;
	if (!read_line(QUARK_RUNDIR "pid", pid_text, sizeof(pid_text))) return 0;
	pid = strtol(pid_text, &end, 10);
	if (*end || pid <= 1 || pid > INT_MAX || kill((pid_t)pid, 0)) return 0;
	if (!read_line(QUARK_RUNDIR "exe", expected, sizeof(expected))) return 0;
	snprintf(path, sizeof(path), "/proc/%ld/exe", pid);
	n = readlink(path, actual, sizeof(actual)-1);
	if (n < 0) return 0;
	actual[n] = '\0';
	if (n > 10 && !strcmp(actual+n-10, " (deleted)")) actual[n-10] = '\0';
	return !strcmp(actual, expected) ? (int)pid : 0;
}

int quarkdrive_status_hook(int eid, webs_t wp, int argc, char **argv)
{
	char state[24] = "off";
	int pid = service_pid();
	if (pid) read_line(QUARK_RUNDIR "eip93", state, sizeof(state));
	if (strcmp(state, "off") && strcmp(state, "pending") && strcmp(state, "active") && strcmp(state, "software"))
		snprintf(state, sizeof(state), "%s", "off");
	websWrite(wp, "{\"pid\":%d,\"enabled\":%d,\"eip93\":\"%s\",\"can_control\":%d}",
		pid, nvram_get_int("quarkdrive_enable") == 1, state, get_login_safe());
	return 0;
}

int quarkdrive_log_hook(int eid, webs_t wp, int argc, char **argv)
{
	FILE *fp = fopen("/tmp/syslog.log", "r");
	char line[2048];
	long length;
	if (!fp) return 0;
	fseek(fp, 0, SEEK_END);
	length = ftell(fp);
	if (length > 32768) { fseek(fp, length-32768, SEEK_SET); fgets(line, sizeof(line), fp); }
	else rewind(fp);
	while (fgets(line, sizeof(line), fp)) {
		unsigned char *p;
		if (!strstr(line, "quarkdrive-webdav:")) continue;
		for (p = (unsigned char *)line; *p; p++) {
			switch (*p) {
			case '&': fputs("&amp;", wp); break;
			case '<': fputs("&lt;", wp); break;
			case '>': fputs("&gt;", wp); break;
			case '"': fputs("&quot;", wp); break;
			default: fputc(*p, wp);
			}
		}
	}
	fclose(fp);
	return 0;
}

void quarkdrive_control(webs_t wp)
{
	char *action = websGetVar(wp, "quark_action", "");
	int result = 1;
	if (get_login_safe()) {
		if (!strcmp(action, "stop")) result = eval("/usr/bin/quarkdrive-webdav.sh", "stop");
		else if (!strcmp(action, "start")) result = eval("/usr/bin/quarkdrive-webdav.sh", "start_now");
		else if (!strcmp(action, "restart")) result = eval("/usr/bin/quarkdrive-webdav.sh", "restart_now");
	}
	websWrite(wp, "{\"sys_result\":%d}", result);
}
