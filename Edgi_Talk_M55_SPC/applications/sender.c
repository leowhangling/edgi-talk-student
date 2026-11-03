#include <rtthread.h>
#include <webclient.h>
#include "aht10.h"

#define SENDER_URL      "http://192.168.102.82:8765/"   /* your PC running relay.py */
#define SEND_PERIOD_MS  15000                            /* demo speed; use 60000 normally */
#define I2C_BUS_NAME    "i2c1"

static rt_thread_t sender_tid = RT_NULL;
// Declare that sender_start exists in another file
extern int sender_start(int argc, char **argv);

static void sender_entry(void *parameter)
{
    aht10_device_t dev = aht10_init(I2C_BUS_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("[sender] AHT20 init failed on %s\n", I2C_BUS_NAME);
        sender_tid = RT_NULL;
        return;
    }

    int n = 0;
    while (1)
    {
        float t = aht10_read_temperature(dev);
        float h = aht10_read_humidity(dev);

        /* integers only: this printf can't print floats (positive values only) */
        int t10 = (int)(t * 10.0f);
        int h10 = (int)(h * 10.0f);

        char body[300];
        rt_snprintf(body, sizeof(body),
            "{\"vals_list\":[{\"x_name\":\"Edgi Talk #%03d\","
            "\"x_studio_temperature\":%d.%d,\"x_studio_humidity\":%d.%d}]}",
            n, t10 / 10, t10 % 10, h10 / 10, h10 % 10);

        struct webclient_session *session = webclient_session_create(1024);
        if (session == RT_NULL)
        {
            rt_kprintf("[sender] session create failed\n");
        }
        else
        {
            webclient_header_fields_add(session, "Content-Length: %d\r\n", (int)rt_strlen(body));
            webclient_header_fields_add(session, "Content-Type: application/json\r\n");

            int resp = webclient_post(session, SENDER_URL, body, rt_strlen(body));
            rt_kprintf("[sender] #%d T=%d.%d H=%d.%d -> HTTP %d\n",
                       n, t10 / 10, t10 % 10, h10 / 10, h10 % 10, resp);
            webclient_close(session);
        }

        n++;
        rt_thread_mdelay(SEND_PERIOD_MS);
    }
}

//shorten the original sender_start to
int sender_start(int argc, char **argv)
{
    if (sender_tid != RT_NULL)
    {
        rt_kprintf("[sender] already running\n");
        return 0;
    }
    sender_tid = rt_thread_create("sender", sender_entry, RT_NULL, 4096, 20, 10);
    if (sender_tid != RT_NULL) rt_thread_startup(sender_tid);
    return 0;
}
MSH_CMD_EXPORT(sender_start, start posting readings to the relay);
