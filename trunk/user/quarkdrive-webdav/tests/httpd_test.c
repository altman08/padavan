/* Compile against the real httpd headers and implementation; no router state. */
#include <assert.h>
#include "../../httpd/src/quarkdrive.c"
#include "../../httpd/src/common.h"
static struct { const char *key, *value; } saved[] = {
    {"quarkdrive_enable","1"}, {"qd_cookie","a=1; b=two words"},
    {"qd_binary","/usr/bin/quarkdrive-webdav"}, {"qd_auth_user",""}, {"qd_auth_password",""},
    {"qd_host","0.0.0.0"}, {"qd_port","8088"}, {"qd_root","/"}, {"qd_strip_prefix",""},
    {"qd_read_buffer_size","1048576"}, {"qd_upload_buffer_size","1048576"},
    {"qd_cache_size","1000"}, {"qd_cache_ttl","600"}, {"qd_refresh_cache_secs_interval","300"},
    {"qd_upload_wait_timeout","280"}, {"qd_auto_index","1"}, {"qd_no_trash","0"}, {"qd_read_only","0"},
    {"qd_skip_upload_same_size","0"}, {"qd_prefer_http_download","0"}, {"qd_redirect","0"}, {"qd_debug","0"}
};
static const char *request_key, *request_value;
static int trusted = 1, calls;
static char action[32];
char *get_cgi(char *name) { return request_key && !strcmp(name, request_key) ? (char *)request_value : NULL; }
char *nvram_safe_get(const char *name) {
    unsigned int i;
    for (i=0; i<sizeof(saved)/sizeof(saved[0]); i++) if (!strcmp(name, saved[i].key)) return (char *)saved[i].value;
    return "";
}
int nvram_get_int(const char *name) { return atoi(nvram_safe_get(name)); }
int get_login_safe(void) { return trusted; }
int _eval(char *const argv[], char *path, int timeout, pid_t *ppid) {
    assert(!strcmp(argv[0], "/usr/bin/quarkdrive-webdav.sh"));
    assert(argv[2] == NULL);
    snprintf(action, sizeof(action), "%s", argv[1]);
    calls++;
    return 0;
}
static void rejects(const char *key, const char *value) {
    request_key = key; request_value = value;
    assert(quarkdrive_validate(NULL) != NULL);
}
int main(void) {
    FILE *output = tmpfile();
    char cookie[4097];
    unsigned int i;
    unsigned long long flags[] = {EVM_RESTART_QUARKDRIVE, EVM_RESTART_REBOOT, EVM_BLOCK_UNSAFE, EVM_RESTART_ALDRIVER};
    assert(output);
    assert(quarkdrive_validate(NULL) == NULL);
    rejects("qd_cookie", "");
    rejects("qd_cookie", "a=1\r\nb=2");
    rejects("qd_port", "65536");
    rejects("qd_port", "1;id");
    rejects("qd_read_buffer_size", "0");
    rejects("qd_cache_ttl", "9999999999");
    rejects("qd_upload_buffer_size", "16777217");
    rejects("qd_refresh_cache_secs_interval", "0");
    rejects("qd_host", "999.0.0.1");
    rejects("qd_host", "::::");
    rejects("qd_auth_user", "alone");
    rejects("qd_binary", "relative");
    rejects("qd_root", "relative");
    rejects("qd_debug", "yes");
    memset(cookie, 'x', 4096); cookie[4096] = 0;
    rejects("qd_cookie", cookie);
    cookie[4095] = 0;
    assert(quarkdrive_validate(NULL) == NULL);
    request_key = "qd_host"; request_value = "::1";
    assert(quarkdrive_validate(NULL) == NULL);
    request_key = "quark_action"; request_value = "restart;id";
    quarkdrive_control(output); assert(calls == 0);
    request_value = "start";
    trusted = 0;
    assert(quarkdrive_validate(NULL) != NULL);
    quarkdrive_control(output); assert(calls == 0);
    trusted = 1; quarkdrive_control(output); assert(calls == 1 && !strcmp(action, "start_now"));
    request_value = "stop"; quarkdrive_control(output); assert(calls == 2 && !strcmp(action,"stop"));
    request_value = "restart"; quarkdrive_control(output); assert(calls == 3 && !strcmp(action,"restart_now"));
    for (i=0; i<sizeof(flags)/sizeof(flags[0]); i++) {
        unsigned int j;
        assert(flags[i] && !(flags[i] & (flags[i]-1)));
        for (j=0; j<i; j++) assert(!(flags[i] & flags[j]));
    }
    fclose(output);
    puts("QuarkDrive httpd: server validation, credential limits, action allowlist and event bits passed");
    return 0;
}
