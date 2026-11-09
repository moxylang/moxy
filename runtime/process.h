/* moxy runtime: process arguments and assertion support */
static int mxy_argc __attribute__((unused));
static char **mxy_argv __attribute__((unused));

MXY_FN mxy_list *mxy_args(void) {
    mxy_list *l = mxy_list_new(&mxy_vw_str, mxy_argc);
    for (int i = 0; i < mxy_argc; i++) {
        mxy_str s = mxy_str_from(mxy_argv[i]);
        mxy_list_push(&l, &mxy_vw_str, &s, 1);
    }
    return l;
}

MXY_FN MXY_NORETURN void mxy_assert_fail(const char *loc, const char *expr, mxy_str msg) {
    fflush(stdout);
    if (msg) fprintf(stderr, "assertion failed: %s\n  at %s\n", msg, loc);
    else fprintf(stderr, "assertion failed: %s\n  at %s\n", expr, loc);
    exit(1);
}

MXY_FN mxy_str mxy_read_line(void) {
    mxy_buf b = {0};
    int c;
    int any = 0;
    while ((c = fgetc(stdin)) != EOF) {
        any = 1;
        if (c == '\n') break;
        mxy_buf_putc(&b, (char)c);
    }
    if (!any) {
        free(b.data);
        return NULL;
    }
    if (b.len && b.data[b.len - 1] == '\r') b.data[--b.len] = '\0';
    return mxy_str_from_buf(&b);
}

MXY_FN mxy_str mxy_read_file(mxy_str path, int *ok) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) {
        *ok = 0;
        return NULL;
    }
    mxy_buf b = {0};
    char chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) mxy_buf_put(&b, chunk, (int64_t)n);
    fclose(f);
    *ok = 1;
    return mxy_str_from_buf(&b);
}

MXY_FN bool mxy_write_file(mxy_str path, mxy_str data, const char *mode) {
    FILE *f = path ? fopen(path, mode) : NULL;
    if (!f) return false;
    size_t n = (size_t)mxy_str_len(data);
    bool ok = fwrite(data ? data : "", 1, n, f) == n;
    return fclose(f) == 0 && ok;
}
