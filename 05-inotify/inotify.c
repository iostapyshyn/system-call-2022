#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>

/* With each inotify event, the kernel supplies us with a bit mask
 * that indicates the cause of the event. With the following table,
 * with one flag per line, you can decode the mask of events.

 If you wonder about the wild syntax of the definition here, a word of
 explanation: Each definition has the form of

    TYPE VARNAME (= INITIALIZER);

 Usually, we use named typed for TYPE (e.g., int, struct bar, long).
 However, C also allows to use unnamed types that are declared in
 place. So in the following

   TYPE        = 'struct { .... } []'
   VARNAME     = inotify_event_flags
   INITIALIZER = { { IN_ACCESS, ...}, ...}
 */
struct {
    int mask;
    char *name;
} inotify_event_flags[] = {
    {IN_ACCESS, "access"},
    {IN_ATTRIB, "attrib"},
    {IN_CLOSE_WRITE, "close_write"},
    {IN_CLOSE_NOWRITE, "close_nowrite"},
    {IN_CREATE, "create"},
    {IN_DELETE, "delete"},
    {IN_DELETE_SELF, "delete_self"},
    {IN_MODIFY, "modify"},
    {IN_MOVE_SELF, "move_self"},
    {IN_MOVED_FROM, "move_from"},
    {IN_MOVED_TO, "moved_to"},
    {IN_OPEN, "open"},
    {IN_MOVE, "move"},
    {IN_CLOSE, "close"},
    {IN_MASK_ADD, "mask_add"},
    {IN_IGNORED, "ignored"},
    {IN_ISDIR, "directory"},
    {IN_UNMOUNT, "unmount"},
};

// We already know this macro from yesterday.
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(*(arr)))
#define BUFFER_SIZE 4096

int main(void) {
    // We allocate a buffer to hold the inotify events, which are
    // variable in size.
    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        perror("malloc");
        return 1;
    }

    int fd = inotify_init();
    if (fd == -1) {
        perror("inotify_init");
        return 1;
    }

    int ret = inotify_add_watch(fd, ".", IN_OPEN|IN_ACCESS|IN_CLOSE);
    if (ret == -1) {
        perror("inotify_add_watch");
        return 1;
    }

    int len;
    while ((len = read(fd, buffer, BUFFER_SIZE)) >= 0) {
        struct inotify_event *event = (void *) buffer;

        /* There can be multiple events in the buffer */
        for (char *ptr = buffer;
             ptr < buffer+len;
             ptr += sizeof(struct inotify_event) + event->len) {

            bool sep = false;   /* Separate flags */
            event = (void *) ptr;

            printf("./%s: [", event->name);
            for (size_t i = 0; i < ARRAY_SIZE(inotify_event_flags); i++) {
                if (event->mask & inotify_event_flags[i].mask) {
                    if (sep)
                        printf(",");
                    printf("%s", inotify_event_flags[i].name);
                    sep = true;
                }
            }
            printf("]\n");
        }
    }


    // As we are nice, we free the buffer again.

    close(fd);
    free(buffer);
    return 0;
}
