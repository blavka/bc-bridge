#include <main.h>
#include <argp.h>
#include <unistd.h>
#include <bc_scheduler.h>

#define OPTION_LIST    1000
#define OPTION_DEV     1001
#define OPTION_HOST    1002
#define OPTION_PORT    1003
#define OPTION_PREFIX  1004

const char *argp_program_bug_address = "<support@bigclown.com>";
bc_bridge_t bridge;

void application_init(void);
void application_task(void *param);
static error_t parse_opt(int key, char *arg, struct argp_state *state);
static void _loop(bool *quit, parameters_t *parameters);
static void _application_bridge_flash_led(bc_bridge_t *bridge, int count);

int main(int argc, char **argv)
{
    bool quit;

    struct argp argp;

    char *doc;

    doc = "Software interface between Clown.Hub and Bridge Module";

    static struct argp_option options[] =
        {
            { "list",   OPTION_LIST,   0,         0, "Show list of available devices" },
            { "dev",    OPTION_DEV,    "ID|PATH", 0, "Select device by ID or PATH (beware that ID may change with USB ports' manipulation)" },
            { "host",   OPTION_HOST,   "HOST",    0, "MQTT host to connect to" },
            { "port",   OPTION_PORT,   "PORT",    0, "MQTT port to connect to (default is 1883)" },
            { "prefix", OPTION_PREFIX, "PREFIX",  0, "MQTT prefix topic" },
            { "furious", 'f',          0,         0, "Do not wait for the initial start string" },
            { "log",     'L',          "LEVEL",   0, "Set desired log level to one of the following options: dump|debug|info|warning|error|fatal" },
            { "version", 'v',          0,         0, "Give version information" },
            { 0 }
        };

    memset(&argp, 0, sizeof(argp));

    argp.options = options;
    argp.parser = parse_opt;
    argp.args_doc = 0;
    argp.doc = doc;

    parameters_t parameters =
        {
            .furious_mode = false,
            .log_level = BC_LOG_LEVEL_WARNING,
            .dev_list = false,
            .dev_id = -1,
            .dev_path = NULL,
            .host = NULL,
            .port = 1883,
            .prefix = NULL
        };

    argp_parse(&argp, argc, argv, 0, 0, &parameters);

    bc_log_init(parameters.log_level);

    if (parameters.dev_list)
    {
        bc_bridge_device_info_t devices[16];
        memset(devices, 0, sizeof(devices));
        uint8_t devices_count = sizeof(devices) / sizeof(bc_bridge_device_info_t);

        if (!bc_bridge_scan(devices, &devices_count))
        {
            bc_log_error("application_loop: call failed: bc_bridge_scan");
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < devices_count; i++)
        {
            printf("% 2d: %s\n", i, devices[i].usb_path);
        }
        exit(EXIT_SUCCESS);
    }

    memset(&bridge, 0, sizeof(bridge));

    quit = false;

    while (!quit)
    {
        _loop(&quit, &parameters);
        sleep(1);
    }

    exit(EXIT_SUCCESS);
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    parameters_t *parameters = state->input;

    switch (key)
    {
        case 'f':
        {
            parameters->furious_mode = true;
            break;
        }
        case 'v':
        {
            printf("bc-bridge " FIRMWARE_RELEASE " (built at " FIRMWARE_DATETIME ")\n");
            exit(EXIT_SUCCESS);
        }
        case 'L':
        {
            if (strcmp(arg, "dump") == 0)
            {
                parameters->log_level = BC_LOG_LEVEL_DUMP;
            }
            else if (strcmp(arg, "debug") == 0)
            {
                parameters->log_level = BC_LOG_LEVEL_DEBUG;
            }
            else if (strcmp(arg, "info") == 0)
            {
                parameters->log_level = BC_LOG_LEVEL_INFO;
            }
            else if (strcmp(arg, "warning") == 0)
            {
                parameters->log_level = BC_LOG_LEVEL_WARNING;
            }
            else if (strcmp(arg, "error") == 0)
            {
                parameters->log_level = BC_LOG_LEVEL_ERROR;
            }
            else if (strcmp(arg, "fatal") == 0)
            {
                parameters->log_level = BC_LOG_LEVEL_FATAL;
            }
            else
            {
                return ARGP_ERR_UNKNOWN;
            }

            break;
        }
        case OPTION_LIST:
        {
            parameters->dev_list = true;
            break;
        }
        case OPTION_DEV:
        {
            char *end;
            parameters->dev_id = (int) strtol(arg, &end, 10);
            if (*end != '\0')
            {
                parameters->dev_path = strdup(arg);
                parameters->dev_id = -1;
            }
            break;
        }
        case OPTION_HOST:
        {
            parameters->host = strdup(arg);
            break;
        }
        case OPTION_PORT:
        {
            char *end;
            parameters->port = (int) strtol(arg, &end, 10);
            if (*end != '\0')
            {
                return ARGP_ERR_UNKNOWN;
            }
            break;
        }
        case OPTION_PREFIX:
        {
            parameters->prefix = strdup(arg);
            break;
        }
        default:
        {
            return ARGP_ERR_UNKNOWN;
        }
    }

    return 0;
}

static void _loop(bool *quit, parameters_t *parameters)
{
    *quit = false;

    bc_bridge_device_info_t devices[16];
    memset(devices, 0, sizeof(devices));

    uint8_t devices_count;
    int selected_device;

    bc_log_info("application_loop: searching device");
    while (true)
    {

        devices_count = sizeof(devices) / sizeof(bc_bridge_device_info_t);
        selected_device = -1;

        if (!bc_bridge_scan(devices, &devices_count))
        {
            bc_log_error("application_loop: call failed: bc_bridge_scan");

            return;
        }

        if (devices_count > 0)
        {
            if (parameters->dev_id > -1)
            {
                if (parameters->dev_id < devices_count)
                {
                    selected_device = parameters->dev_id;
                }
            }
            else if (parameters->dev_path != NULL)
            {
                for (int i = 0; i < devices_count; i++)
                {
                    if (strcmp(devices[i].usb_path, parameters->dev_path) == 0)
                    {
                        selected_device = i;
                        break;
                    }
                }
            }
            else
            {
                selected_device = 0;
            }
        }

        if (selected_device > -1)
        {
            break;
        }

        bc_os_task_sleep(5000);

    }

    if (!bc_bridge_open(&bridge, &devices[selected_device]))
    {
        bc_log_error("application_loop: call failed: bc_bridge_open");

        return;
    }

    _application_bridge_flash_led(&bridge, 3);

    bc_scheduler_init();

    bc_scheduler_register(application_task, NULL, 0);

    application_init();

    while (bc_bridge_is_alive(&bridge))
    {
        bc_scheduler_run();
    }

    bc_log_error("application_loop: device disconnect");

    bc_bridge_close(&bridge);

    if (*quit)
    {
        for (int i = 0; i < sizeof(devices) / sizeof(bc_bridge_device_info_t); i++)
        {
            if (devices[i].usb_path != NULL)
            {
                free(devices[i].usb_path);
            }
            devices[i].usb_path = NULL;

            if (devices[i].path_i2c != NULL)
            {
                free(devices[i].path_i2c);
            }
            devices[i].path_i2c = NULL;

            if (devices[i].path_uart != NULL)
            {
                free(devices[i].path_uart);
            }
            devices[i].path_uart = NULL;
        }
    }
}

__attribute__((weak)) void application_init(void)
{

}

__attribute__((weak)) void application_task(void *param)
{
    (void) param;
}

static void _application_bridge_flash_led(bc_bridge_t *bridge, int count)
{
    int i;

    bc_bridge_led_set_state(bridge, BC_BRIDGE_LED_STATE_OFF);

    for (i = 0; i < count; i++)
    {
        bc_os_task_sleep(200);
        bc_bridge_led_set_state(bridge, BC_BRIDGE_LED_STATE_ON);
        bc_os_task_sleep(200);
        bc_bridge_led_set_state(bridge, BC_BRIDGE_LED_STATE_OFF);
    }
}
