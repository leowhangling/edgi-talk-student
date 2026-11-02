# Edgi Talk smart machine IIoT monitoring system

[Video demo](https://youtu.be/Upwc5Jk1VvE)

# Sending AHT20 Temperature and Humidity from RT-Thread (Edgi Talk M55) to the Odoo SPC Dashboard
Project documentation, step by step. Replace the placeholders in square brackets with your own details. Never write the API key or the Wi-Fi password into this document.
## 1. Overview
Goal: read temperature and humidity from the AHT20 sensor on the Edgi Talk M55 board, and show the readings as points on the Edgi Talk SPC control charts in Odoo.
Final data flow
AHT20 sensor -> Edgi Talk M55 board (RT-Thread, sender.c)
  -> Wi-Fi, plain HTTP POST (JSON) -> PC running relay.py (port 8765)
  -> HTTPS POST with API key -> Odoo JSON-2 API (x_cps_sensor_log)
  -> Odoo spreadsheet (filtered to "Edgi Talk") -> SPC control charts
Why a relay on the PC? The board's direct HTTPS connection to Odoo failed during the TLS handshake (error -0x7100). The relay receives the board's plain HTTP message and forwards it to Odoo over HTTPS. This also keeps the API key off the board.
Tools and versions
    • Board: Edgi Talk M55, project Edgi_Talk_M55_WIFI, RT-Thread Studio
    • Sensor: AHT20 on I2C (bus i2c1)
    • RT-Thread packages: WebClient v2.2.0 (with MbedTLS option tested)
    • PC: Windows, Python 3
    • Odoo: version 19.0 (database edu-lkl-test01), using the JSON-2 API
## 2. Step 1: Serial terminal and board check
    1. Connect the board with the KitProg3 USB cable.
    2. Open the serial terminal in RT-Thread Studio with these settings: COM3, 115200 baud, 8 data bits, no parity, 1 stop bit.
    3. Press Enter until the prompt msh /> appears.
This terminal shows the boot log and lets you type commands such as wifi join and sender_start.
## 3. Step 2: Enable the AHT20 in RT-Thread Settings
Open RT-Thread Settings, then Hardware, and set:
    • Enable AHT20: on
    • Enable AHT20 SAMPLE: off (the built-in sample prints its own readings and can clash with our program)
    • Bus name: i2c1
    • SOFT I2C1: on, with SCL pin 75 and SDA pin 74
Save the settings (Ctrl+S). After a rebuild, the boot log should show AHT10 has been initialized!.
## 4. Step 3: Enable the WebClient package
Open RT-Thread Settings, then Packages, then IoT, and make sure WebClient is on. For the final setup, the TLS mode can stay at Not support, because the board only sends plain HTTP to the PC. (MbedTLS support was tried for a direct HTTPS connection, see section 9.)
## 5. Step 4: Local test with a Python receiver
Before involving Odoo, we checked that the board can send data to the PC.
### 5.1 Receiver script. Save as receiver.py and run python receiver.py. A blank window with a blinking cursor means it is waiting for data.
from http.server import BaseHTTPRequestHandler, HTTPServer

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        print("Headers:", dict(self.headers))
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n) if n else b""
        print("Received:", self.path, len(body), "bytes")
        print(body.decode(errors="replace"), flush=True)
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *args):
        pass

HTTPServer(("0.0.0.0", 8765), Handler).serve_forever()
### 5.2 Port. Port 8080 failed with WinError 10013 (Windows refused it), so port 8765 is used. The same port must appear in the receiver, in sender.c, and in the firewall rule.
### 5.3 Firewall rule. Open Command Prompt as administrator and run (use the board's IP address):
netsh advfirewall firewall add rule name="Edgi test receiver" dir=in action=allow protocol=TCP localport=8765 remoteip=[board IP]
If the board gets a different IP later, delete the rule (netsh advfirewall firewall delete rule name="Edgi test receiver") and add it again with the new address.
### 5.4 Turn WireGuard off. The WireGuard VPN (tunnel fys, Allowed IPs 0.0.0.0/0) blocked local network traffic, and the board could not reach the PC (HTTP -6) until it was switched off. Keep both tunnels inactive while testing locally.
### 5.5 Content-Length header. At first the receiver showed Received: / 0 bytes, because the board's request had no Content-Length header. Adding the header in sender.c fixed it, and the full JSON then arrived.
## 6. Step 5: The Odoo side
### 6.1 Find the model. In Odoo, developer mode (add ?debug=1 to the URL) shows Settings, Technical, Models. The readings used by the dashboard are stored in the model x_cps_sensor_log (one record per reading).
Field	Type	Meaning
x_name	char (required)	Record name
x_studio_temperature	float	Temperature
x_studio_humidity	float	Humidity
x_studio_mixer_rpm	integer	Mixer RPM (not used by Edgi Talk)
x_studio_torque	float	Torque (not used)
x_studio_vacuum	float	Vacuum (not used)
A second model, x_spc, holds one row per parameter with its Center Line, UCL, LCL and Process Status.
### 6.2 API key. In Odoo, open the profile picture, then Preferences, then Account Security, then New API Key. Copy the key immediately, because Odoo shows it only once. Treat it like a password. If it ever appears in a photo, screenshot or chat, delete it and create a new one.
### 6.3 JSON-2 endpoint.
    • URL: https://edu-lkl-test01.odoo.com/json/2/x_cps_sensor_log/create
    • Headers: Authorization: Bearer [API key], Content-Type: application/json, X-Odoo-Database: edu-lkl-test01
    • Body: {"vals_list":[{"x_name":"Edgi Talk #000","x_studio_temperature":32.2,"x_studio_humidity":55.0}]}
The reply is a list with the new record id, for example [95]. A 401 means the key was not accepted, and a 403 means the user lacks access to the model.
### 6.4 Test from the PC. Save as odoo_edgi_test2.py. It asks for the key without showing it, creates one record named "Edgi Talk TEST", waits while you refresh the dashboards, then deletes the record.
import json, getpass, urllib.request

key = getpass.getpass("Paste API key (hidden), then Enter: ").strip()

def call(method, body):
    req = urllib.request.Request(
        "https://edu-lkl-test01.odoo.com/json/2/x_cps_sensor_log/" + method,
        data=json.dumps(body).encode(),
        headers={"Authorization": "Bearer " + key,
                 "Content-Type": "application/json",
                 "X-Odoo-Database": "edu-lkl-test01"},
        method="POST",
    )
    return json.loads(urllib.request.urlopen(req).read())

try:
    new_id = call("create", {"vals_list": [{
        "x_name": "Edgi Talk TEST",
        "x_studio_temperature": 32.2,
        "x_studio_humidity": 55.0,
    }]})
    print("Created record:", new_id)
    input("Refresh the dashboards (F5) and look. Press Enter here to delete the test record...")
    print("Deleted:", call("unlink", {"ids": new_id}))
except urllib.error.HTTPError as e:
    print(e.code, e.read().decode()[:400])
## 7. Step 6: How the dashboard works
    • The charts are built from an Odoo spreadsheet. The original CPS dashboard and the Edgi Talk SPC Dashboard are two separate spreadsheets on the same model.
    • The Edgi Talk sheet is filtered to records whose x_name starts with "Edgi Talk", so the board's names (Edgi Talk #000, Edgi Talk #001, ...) are included.
    • The charts show the latest 19 readings. Each new record shifts the line one place to the right.
    • The sheet is not live. Press F5 to see new data.
    • The control limits are not stored in each record. The sheet compares each reading with a center line and limits.
Important: the Edgi Talk limits are not configured yet. Until they are, the sheet still uses the CPS simulation values (Temperature center line about 37.85, limits about 37.14 to 38.57). The room sensor reads about 31 to 32 degrees C, so the Temperature points appear far below the band, at about -6. The In Control and Out of Control cards are blank because those formulas were cleared while the dashboards are being separated.
## 8. Step 7: The program on the board (sender.c)
This is the final version. It reads the AHT20 every SEND_PERIOD_MS milliseconds and posts the reading as JSON to the PC. The numbers are formatted as integers because the board's printf cannot print floats.
#include <rtthread.h>
#include <webclient.h>
#include "aht10.h"

#define SENDER_URL      "http://[PC IP]:8765/"   /* your PC running relay.py */
#define SEND_PERIOD_MS  60000                    /* 15000 for a demo */
#define I2C_BUS_NAME    "i2c1"

static rt_thread_t sender_tid = RT_NULL;

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

static int sender_start(void)
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
Put the file in the applications folder of the project. If the build does not pick it up, right-click the project and choose Sync Sconscript to project.
## 9. Step 8: Direct HTTPS attempt (not used)
We set WebClient, Select TLS mode, to MbedTLS support, and made the board post straight to https://edu-lkl-test01.odoo.com. The Wi-Fi and DNS worked, but the TLS handshake failed with mbedtls_ssl_handshake error, return -0x7100, so the board could not send. The cause was not isolated, and the relay was chosen as the working solution. The messages about a missing RTC device are only warnings about the board having no clock.
## 10. Step 9: The relay on the PC (relay.py)
The relay receives the board's POST and forwards it to Odoo with the API key. The key is typed when the script starts, so it is never saved in a file. It prints one line per forwarded record.
import getpass, urllib.request, urllib.error
from http.server import BaseHTTPRequestHandler, HTTPServer

KEY = getpass.getpass("Paste API key (hidden), then Enter: ").strip()
URL = "https://edu-lkl-test01.odoo.com/json/2/x_cps_sensor_log/create"

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        req = urllib.request.Request(URL, data=body, method="POST", headers={
            "Authorization": "Bearer " + KEY,
            "Content-Type": "application/json",
            "X-Odoo-Database": "edu-lkl-test01"})
        try:
            reply = urllib.request.urlopen(req).read().decode()
            print("Odoo OK:", reply, "<-", body.decode(errors="replace"), flush=True)
            code = 200
        except urllib.error.HTTPError as e:
            print("Odoo error", e.code, e.read().decode()[:200], flush=True)
            code = 502
        except Exception as e:
            print("Failed:", e, flush=True)
            code = 502
        self.send_response(code)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *args):
        pass

HTTPServer(("0.0.0.0", 8765), Handler).serve_forever()
After pasting the key and pressing Enter, the window stays blank. That is normal, and it means the relay is waiting.
## 11. Running the whole system
    1. Before starting: confirm with the dashboard owner that the CPS dashboard is separated from the Edgi Talk data, because every reading sent becomes a record in Odoo.
    2. On the PC, run ipconfig and note the IPv4 address of the Wi-Fi adapter. Make sure SENDER_URL in sender.c uses that address. The PC's address is assigned automatically and changed several times (.116, .121, .123).
    3. Turn WireGuard off (both tunnels inactive).
    4. Stop receiver.py if it is running, then start python relay.py and paste the API key. Leave the window open.
    5. In RT-Thread Studio, build the project (0 errors) and flash it.
    6. In the serial terminal, press Enter until msh /> appears. Run wifi join [SSID] [password] and wait for the line Got IP address. If the board's IP differs from the firewall rule, update the rule.
    7. Type sender_start.
    8. Expected result: the board prints [sender] #0 T=... H=... -> HTTP 200 each period, and the relay window prints Odoo OK: [id].
    9. In Odoo, open the Edgi Talk SPC Dashboard and press F5. A new point appears at the left of each chart, and the earlier points move right.
    10. To stop sending, press the board's reset button. There is no stop command. Press Ctrl+C in the relay window to close it.
Use SEND_PERIOD_MS 15000 for a demo and set it back to 60000 afterwards. Each reading creates a record in Odoo.
## 12. Troubleshooting
Symptom	Likely cause	Fix
WinError 10013 on the receiver	Windows refused the port	Use another port such as 8765, and use the same port everywhere
HTTP -6, connect socket(3) error	The board cannot reach the PC	Check the PC's current IP against SENDER_URL, WireGuard off, relay running, firewall rule, same Wi-Fi network
whd_wifi_join_wait_for_complete: not ready to transceive	Wi-Fi join did not finish	Reset or power-cycle the board, join again, and wait for Got IP address before sender_start
Receiver shows 0 bytes	Missing Content-Length header	Add the header in sender.c
Board shows AHT20 init failed	I2C settings	Recheck bus name i2c1, SOFT I2C1 pins 75 and 74, and AHT20 SAMPLE off
mbedtls_ssl_handshake error -0x7100	Direct HTTPS from the board failed	Use the relay on the PC
Relay shows Odoo error 401	Wrong, old or deleted API key	Create a new key and restart the relay
Relay shows Odoo error 403	The user has no access to the model	Ask the Odoo admin
Dashboard shows no new point	The sheet is not live	Press F5
Temperature points far below the band	Edgi limits are not set yet	Wait for the Edgi center line and limits
## 13. Security notes
    • Never put the API key in sender.c, in this document, in screenshots or in a chat. If it is exposed, delete it in Odoo and create a new one.
    • Do not show the Wi-Fi password in photos or screenshots of the terminal. Change it if it was exposed.
    • The link between the board and the PC is plain HTTP on the local network. Use it only on a trusted network.
    • Every reading creates a real record in Odoo. Delete test records after testing, and keep the sending period reasonable.
## 14. Current status and next steps
Working: reading the AHT20, sending to the PC, forwarding to Odoo, and showing the readings as points on the Edgi Talk SPC charts after a refresh.
Pending
    • Finish separating the CPS and Edgi Talk dashboards, so Edgi readings never appear on the CPS charts.
    • Collect about 20 or more Edgi readings, then set Edgi's own center line, control limits and In Control / Out of Control formulas. These are decided by the dashboard owner or supervisor.
    • Optional improvements: reserve a fixed IP for the PC in the router, add a stop command and automatic Wi-Fi join on the board, add time synchronisation (SNTP), and fix the direct HTTPS connection so the PC relay is no longer needed.
