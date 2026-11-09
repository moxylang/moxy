/* moxy runtime: futures (one thread per async call). The thread owns its
   arguments; handles are reference counted. The last handle joins a finished
   thread or detaches a running one, which then frees the future itself. */
#ifdef MXY_THREADS
typedef struct mxy_future {
    int64_t rc;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int done;
    int orphaned;
    const mxy_vw *vw;
    void *args;
    void (*run)(void *args, void *out);
    void (*drop_args)(void *args);
    char *result;
} mxy_future;

MXY_FN void mxy_future_free(mxy_future *f) {
    if (f->vw->size) mxy_vw_drop(f->vw, f->result);
    pthread_mutex_destroy(&f->lock);
    pthread_cond_destroy(&f->cond);
    free(f->result);
    free(f);
}

MXY_FN void *mxy_future_main(void *p) {
    mxy_future *f = (mxy_future *)p;
    f->run(f->args, f->result);
    if (f->drop_args) f->drop_args(f->args);
    free(f->args);
    f->args = NULL;
    pthread_mutex_lock(&f->lock);
    f->done = 1;
    int orphaned = f->orphaned;
    pthread_cond_broadcast(&f->cond);
    pthread_mutex_unlock(&f->lock);
    if (orphaned) mxy_future_free(f);
    return NULL;
}

MXY_FN mxy_future *mxy_future_spawn(const mxy_vw *vw, void *args, void (*run)(void *, void *),
                                    void (*drop_args)(void *)) {
    mxy_future *f = (mxy_future *)mxy_zalloc(sizeof(mxy_future));
    f->rc = 1;
    f->vw = vw;
    f->args = args;
    f->run = run;
    f->drop_args = drop_args;
    f->result = (char *)mxy_zalloc(vw->size ? vw->size : 1);
    pthread_mutex_init(&f->lock, NULL);
    pthread_cond_init(&f->cond, NULL);
    if (pthread_create(&f->thread, NULL, mxy_future_main, f) != 0) {
        /* no thread available: run inline so the program still makes progress */
        f->orphaned = 0;
        mxy_future_main(f);
        f->thread = pthread_self();
        f->done = 2;
    }
    return f;
}

/* waits for completion; returns a borrowed pointer to the result */
MXY_FN void *mxy_future_await(mxy_future *f, const char *loc) {
    if (!f) mxy_panic(loc, "await on a null future");
    pthread_mutex_lock(&f->lock);
    while (!f->done) pthread_cond_wait(&f->cond, &f->lock);
    pthread_mutex_unlock(&f->lock);
    return f->result;
}

MXY_INLINE mxy_future *mxy_future_retain(mxy_future *f) {
    if (f) mxy_rc_inc(&f->rc);
    return f;
}

MXY_FN void mxy_future_release(mxy_future *f) {
    if (!f || !mxy_rc_dec(&f->rc)) return;
    pthread_mutex_lock(&f->lock);
    int done = f->done;
    if (!done) f->orphaned = 1;
    pthread_mutex_unlock(&f->lock);
    if (!done) {
        pthread_detach(f->thread);
        return;
    }
    if (done == 1) pthread_join(f->thread, NULL);
    mxy_future_free(f);
}

MXY_FN void mxy_copy_future(void *dst, const void *src) { *(mxy_future **)dst = mxy_future_retain(*(mxy_future *const *)src); }
MXY_FN void mxy_drop_future(void *p) { mxy_future_release(*(mxy_future **)p); }
static const mxy_vw mxy_vw_future __attribute__((unused)) = {sizeof(mxy_future *), mxy_copy_future, mxy_drop_future, mxy_eq_ptr, mxy_hash_ptr, NULL, NULL, "future"};
#else
typedef struct mxy_future mxy_future;
#endif

static const mxy_vw mxy_vw_void __attribute__((unused)) = {0, NULL, NULL, NULL, NULL, NULL, NULL, "void"};
