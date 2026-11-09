#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cli.h"

char *shell_quote(const char *s) {
    Buf b = {0};
    buf_putc(&b, '\'');
    for (const char *p = s; *p; p++) {
        if (*p == '\'') buf_put(&b, "'\\''");
        else buf_putc(&b, *p);
    }
    buf_putc(&b, '\'');
    return buf_take(&b);
}

int cc_run(const CcOpts *o) {
    Buf cmd = {0};
    const char *cc = o->cc && *o->cc ? o->cc : getenv("CC") ? getenv("CC") : "cc";
    buf_fmt(&cmd, "%s -std=c11", cc);
    buf_put(&cmd, o->release ? " -O2 -DNDEBUG" : " -g -O0");
    if (o->sanitize) buf_put(&cmd, " -fsanitize=address,undefined -fno-omit-frame-pointer");
    const char *env = getenv("CFLAGS");
    if (env && *env) buf_fmt(&cmd, " %s", env);
    if (o->cflags && *o->cflags) buf_fmt(&cmd, " %s", o->cflags);
    for (int i = 0; i < o->includes.len; i++) buf_fmt(&cmd, " -I%s", shell_quote(o->includes.items[i]));
    if (o->object_only) buf_put(&cmd, " -c");
    buf_fmt(&cmd, " -o %s", shell_quote(o->output));
    for (int i = 0; i < o->sources.len; i++) buf_fmt(&cmd, " %s", shell_quote(o->sources.items[i]));
    if (!o->object_only) {
        if (o->ldflags && *o->ldflags) buf_fmt(&cmd, " %s", o->ldflags);
        const char *ld = getenv("LDFLAGS");
        if (ld && *ld) buf_fmt(&cmd, " %s", ld);
        if (o->threads) buf_put(&cmd, " -lpthread");
        buf_put(&cmd, " -lm");
    }
    if (getenv("MOXY_VERBOSE")) fprintf(stderr, "%s\n", buf_str(&cmd));
    int status = system(buf_str(&cmd));
    buf_free(&cmd);
    if (status == -1) return 1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

char *make_temp_dir(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    char *tmpl = path_join(base, "moxyXXXXXX");
    if (!mkdtemp(tmpl)) {
        free(tmpl);
        return NULL;
    }
    return tmpl;
}

void remove_tree(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            char *child = path_join(path, e->d_name);
            struct stat st;
            if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(child);
            else unlink(child);
            free(child);
        }
        closedir(d);
    }
    rmdir(path);
}

int run_binary(const char *path, int argc, char **argv) {
    char **args = xcalloc((size_t)argc + 2, sizeof(char *));
    args[0] = (char *)path;
    for (int i = 0; i < argc; i++) args[i + 1] = argv[i];
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        execv(path, args);
        perror("moxy: exec failed");
        _exit(127);
    }
    free(args);
    if (pid < 0) return 1;
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "moxy: program terminated by signal %d\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return 1;
}
