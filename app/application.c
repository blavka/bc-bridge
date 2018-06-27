#include <application.h>
#include <bcl.h>
#include <bc_log.h>

void temperature_tag_event_handler(bc_tag_temperature_t *self, bc_tag_temperature_event_t event, void *event_param);

void application_init(void)
{
    static bc_tag_temperature_t temperature_tag;
    bc_tag_temperature_init(&temperature_tag, BC_I2C_I2C0, BC_TAG_TEMPERATURE_I2C_ADDRESS_DEFAULT);
    bc_tag_temperature_set_update_interval(&temperature_tag, 1000);
    bc_tag_temperature_set_event_handler(&temperature_tag, temperature_tag_event_handler, NULL);
}

void application_loop(void *param)
{
    (void) param;
}

void temperature_tag_event_handler(bc_tag_temperature_t *self, bc_tag_temperature_event_t event, void *event_param)
{
    (void) event_param;

    if (event == BC_TAG_TEMPERATURE_EVENT_UPDATE)
    {
        float temperature;
        bc_tag_temperature_get_temperature_celsius(self, &temperature);
        bc_log_info("temperature = %.1f C", temperature);
    }
    else if (event == BC_TAG_TEMPERATURE_EVENT_ERROR)
    {
    }
}
