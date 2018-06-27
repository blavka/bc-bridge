#include "bc_log.h"
#include "bc_os.h"
#include <sys/time.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>


#define BC_LOG_DUMP_WIDTH 8

static bc_log_level_t bc_log_level;
static bc_os_mutex_t bc_log_mutex;

static void bc_log_head(bc_log_level_t level);
static void bc_log_message(const char *format, va_list ap);
static void bc_log_tail(void);

void bc_log_init(bc_log_level_t level)
{
    bc_os_mutex_init(&bc_log_mutex);

    bc_log_level = level;
}

void bc_log_dump(const void *buffer, uint32_t length, const char *format, ...)
{
    va_list ap;

    uint32_t position;

    bc_os_mutex_lock(&bc_log_mutex);

    if ((int32_t) bc_log_level > (int32_t) BC_LOG_LEVEL_DUMP)
    {
        bc_os_mutex_unlock(&bc_log_mutex);

        return;
    }

    bc_log_head(BC_LOG_LEVEL_DUMP);

    va_start(ap, format);
    bc_log_message(format, ap);
    va_end(ap);

    bc_log_tail();

    if (buffer != NULL && length != 0)
    {
        for (position = 0; position < length; position += BC_LOG_DUMP_WIDTH)
        {
            static char hex[BC_LOG_DUMP_WIDTH * 3 + 2 + 1];
            static char text[BC_LOG_DUMP_WIDTH + 1];

            char *ptr_hex;
            char *ptr_text;

            uint32_t line_size;

            uint32_t i;

            ptr_hex = hex;
            ptr_text = text;

            if ((position + BC_LOG_DUMP_WIDTH) <= length)
            {
                line_size = BC_LOG_DUMP_WIDTH;
            }
            else
            {
                line_size = length - position;
            }

            for (i = 0; i < line_size; i++)
            {
                uint8_t value;

                value = ((uint8_t *) buffer)[position + i];

                if (i == (BC_LOG_DUMP_WIDTH / 2))
                {
                    *ptr_hex++ = '|';
                    *ptr_hex++ = ' ';
                }

                snprintf(ptr_hex, 4, "%02X ", value);

                ptr_hex += 3;

                if (value < 32 || value > 126)
                {
                    *ptr_text++ = '.';
                }
                else
                {
                    *ptr_text++ = value;
                }
            }

            *ptr_hex = '\0';
            *ptr_text = '\0';

            bc_log_head(BC_LOG_LEVEL_DUMP);

            fprintf(stderr, "%5d:  %-*s %-*s", position,
                    BC_LOG_DUMP_WIDTH * 3 + 2, hex,
                    BC_LOG_DUMP_WIDTH, text
            );

            bc_log_tail();
        }
    }

    bc_os_mutex_unlock(&bc_log_mutex);
}

void bc_log_debug(const char *format, ...)
{
    va_list ap;

    bc_os_mutex_lock(&bc_log_mutex);

    if ((int32_t) bc_log_level > (int32_t) BC_LOG_LEVEL_DEBUG)
    {
        bc_os_mutex_unlock(&bc_log_mutex);

        return;
    }

    bc_log_head(BC_LOG_LEVEL_DEBUG);

    va_start(ap, format);
    bc_log_message(format, ap);
    va_end(ap);

    bc_log_tail();

    bc_os_mutex_unlock(&bc_log_mutex);
}

void bc_log_info(const char *format, ...)
{
    va_list ap;

    bc_os_mutex_lock(&bc_log_mutex);

    if ((int32_t) bc_log_level > (int32_t) BC_LOG_LEVEL_INFO)
    {
        bc_os_mutex_unlock(&bc_log_mutex);

        return;
    }

    bc_log_head(BC_LOG_LEVEL_INFO);

    va_start(ap, format);
    bc_log_message(format, ap);
    va_end(ap);

    bc_log_tail();

    bc_os_mutex_unlock(&bc_log_mutex);
}

void bc_log_warning(const char *format, ...)
{
    va_list ap;

    bc_os_mutex_lock(&bc_log_mutex);

    if ((int32_t) bc_log_level > (int32_t) BC_LOG_LEVEL_WARNING)
    {
        bc_os_mutex_unlock(&bc_log_mutex);

        return;
    }

    bc_log_head(BC_LOG_LEVEL_WARNING);

    va_start(ap, format);
    bc_log_message(format, ap);
    va_end(ap);

    bc_log_tail();

    bc_os_mutex_unlock(&bc_log_mutex);
}

void bc_log_error(const char *format, ...)
{
    va_list ap;

    bc_os_mutex_lock(&bc_log_mutex);

    if ((int32_t) bc_log_level > (int32_t) BC_LOG_LEVEL_ERROR)
    {
        bc_os_mutex_unlock(&bc_log_mutex);

        return;
    }

    bc_log_head(BC_LOG_LEVEL_ERROR);

    va_start(ap, format);
    bc_log_message(format, ap);
    va_end(ap);

    bc_log_tail();

    bc_os_mutex_unlock(&bc_log_mutex);
}

void bc_log_fatal(const char *format, ...)
{
    va_list ap;

    bc_os_mutex_lock(&bc_log_mutex);

    if ((int32_t) bc_log_level > (int32_t) BC_LOG_LEVEL_FATAL)
    {
        bc_os_mutex_unlock(&bc_log_mutex);

        return;
    }

    bc_log_head(BC_LOG_LEVEL_FATAL);

    va_start(ap, format);
    bc_log_message(format, ap);
    va_end(ap);

    bc_log_tail();

    bc_os_mutex_unlock(&bc_log_mutex);

    exit(EXIT_FAILURE);
}

static void bc_log_head(bc_log_level_t level)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) == 0)
    {
        struct tm tm;

        if (localtime_r(&tv.tv_sec, &tm) != NULL)
        {
            char buffer[64];

            if (strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S.%%03d ", &tm) != 0)
            {
                fprintf(stderr, buffer, tv.tv_usec / 1000);
            }
        }
    }

    fprintf(stderr, "<%*ld> ", 5, syscall(SYS_gettid));

    switch (level)
    {
        case BC_LOG_LEVEL_DUMP:
            fprintf(stderr, "[ DUMP  ] ");
            break;
        case BC_LOG_LEVEL_DEBUG:
            fprintf(stderr, "[ DEBUG ] ");
            break;
        case BC_LOG_LEVEL_INFO:
            fprintf(stderr, "[ INFO  ] ");
            break;
        case BC_LOG_LEVEL_WARNING:
            fprintf(stderr, "[ WARN  ] ");
            break;
        case BC_LOG_LEVEL_ERROR:
            fprintf(stderr, "[ ERROR ] ");
            break;
        case BC_LOG_LEVEL_FATAL:
            fprintf(stderr, "[ FATAL ] ");
            break;
        default:
            fprintf(stderr, "[ ????? ] ");
            break;
    }
}

static void bc_log_message(const char *format, va_list ap)
{
    vfprintf(stderr, format, ap);
}

static void bc_log_tail(void)
{
    fprintf(stderr, "\n");
    fflush(stderr);
}
