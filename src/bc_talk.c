#include "bc_talk.h"
#include "bc_os.h"

static bc_os_mutex_t bc_talk_mutex;

void bc_talk_init(void)
{
    bc_os_mutex_init(&bc_talk_mutex);
}

void bc_talk_publish_begin(char *topic)
{
    bc_os_mutex_lock(&bc_talk_mutex);

    fprintf(stdout, "[\"%s\", {", topic);
}

void bc_talk_publish_add_quantity(char *name, char *unit, char *value, ...)
{
    va_list ap;

    fprintf(stdout, "\"%s\": [", name);

    va_start(ap, value);
    vfprintf(stdout, value, ap);
    va_end(ap);

    fprintf(stdout, ", \"%s\"], ", unit);
}

void bc_talk_publish_add_quantity_final(char *name, char *unit, char *value, ...)
{
    va_list ap;

    fprintf(stdout, "\"%s\": [", name);

    va_start(ap, value);
    vfprintf(stdout, value, ap);
    va_end(ap);

    fprintf(stdout, ", \"%s\"]", unit);
}

void bc_talk_publish_end(void)
{
    fprintf(stdout, "}]\n");

    bc_os_mutex_unlock(&bc_talk_mutex);
}
