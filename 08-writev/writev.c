#define _GNU_SOURCE
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <sys/uio.h>
#include <stdio.h>
#include <memory.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#define die(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)

struct iovec_v {
    struct iovec *iov;
    size_t size, capacity;
};

void iovec_v_init(struct iovec_v *v, size_t capacity) {
    v->iov = malloc(sizeof(struct iovec) * capacity);
    assert(v->iov);
    v->size = 0;
    v->capacity = capacity;
}

void iovec_v_push(struct iovec_v *v, struct iovec iovec) {
    if (v->size >= v->capacity) {
        v->capacity *= 2;
        v->iov = realloc(v->iov, v->capacity * sizeof(struct iovec));
        assert(v->iov);
    }

    v->iov[v->size++] = iovec;
}

int iovec_compar(const void *_a, const void *_b) {
    struct iovec
        *a = (struct iovec *) _a,
        *b = (struct iovec *) _b;
    return strcmp(a->iov_base, b->iov_base);
}

void iovec_v_sort(struct iovec_v *v) {
    qsort(v->iov, v->size, sizeof(struct iovec), iovec_compar);
}

void iovec_v_free(struct iovec_v *v) {
    for (size_t i = 0; i < v->size; i++)
        free(v->iov[i].iov_base);

    free(v->iov);
}

int main() {
    struct iovec_v v;
    iovec_v_init(&v, 1);

    ssize_t len;
    size_t n = 0;
    char *ptr = NULL;
    while ((len = getline(&ptr, &n, stdin)) > 0) {
        struct iovec iovec = {
            .iov_base = ptr,
            .iov_len  = len,
        };

        iovec_v_push(&v, iovec);

        ptr = NULL;
        n = 0;
    }
    if (ptr)
        free(ptr);              /* Weird, right? */

    iovec_v_sort(&v);

    writev(STDOUT_FILENO, v.iov, v.size);

    iovec_v_free(&v);

    return 0;
}
