#include <stdio.h>
#include <string.h>

#include "webui.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_SMART_SYSTEM_ENABLE_WEB
#include "esp_http_server.h"
#include "esp_wifi.h"
#endif

static const char *TAG = "smart_web";
static volatile bool web_active;
static web_status_t current_status;
static SemaphoreHandle_t status_mutex;

#if CONFIG_SMART_SYSTEM_ENABLE_WEB
static const char dashboard_html[] =
	"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
	"<title>CoCIS Smart Control</title><style>"
	":root{--bg:#0b1220;--panel:#121d33;--panel2:#0f1729;--line:#22304d;--text:#e8eefc;"
	"--muted:#8fa3c8;--cyan:#38d9f5;--green:#3ddc97;--red:#ff5d73;--yellow:#ffcc66;"
	"--violet:#a78bfa}*{box-sizing:border-box}body{margin:0;font-family:'Segoe UI',"
	"system-ui,-apple-system,sans-serif;background:radial-gradient(1200px 600px at 15% "
	"-10%,#17325c 0%,transparent 55%),radial-gradient(900px 500px at 110% 10%,#2a1c5e "
	"0%,transparent 50%),var(--bg);color:var(--text);min-height:100vh}"
	"header{display:flex;align-items:center;justify-content:space-between;gap:20px;"
	"padding:26px 34px;border-bottom:1px solid var(--line);backdrop-filter:blur(8px);"
	"background:rgba(9,15,28,.6);position:sticky;top:0;z-index:5}"
	".brand{display:flex;align-items:center;gap:14px}.logo{width:46px;height:46px;"
	"border-radius:14px;display:grid;place-items:center;font-weight:800;font-size:18px;"
	"background:linear-gradient(135deg,var(--cyan),var(--violet));color:#07101f;"
	"box-shadow:0 8px 24px rgba(56,217,245,.25)}h1{font-size:19px;margin:0;"
	"letter-spacing:.4px}.sub{color:var(--muted);font-size:12px;margin-top:3px}"
	".clock{font-variant-numeric:tabular-nums;color:var(--muted);font-size:13px}"
	"main{padding:28px 34px 46px;max-width:1240px;margin:0 auto}"
	".hero{display:flex;justify-content:space-between;align-items:flex-end;gap:16px;"
	"margin-bottom:22px;flex-wrap:wrap}.hero h2{margin:0;font-size:26px}"
	".pill{display:inline-flex;align-items:center;gap:8px;padding:7px 13px;border-radius:"
	"999px;font-size:12px;font-weight:700;letter-spacing:.4px;border:1px solid var(--line);"
	"background:var(--panel2)}.dot{width:8px;height:8px;border-radius:50%;background:"
	"var(--green);box-shadow:0 0 12px var(--green)}.pill.closed .dot{background:var(--red);"
	"box-shadow:0 0 12px var(--red)}.pill.warn .dot{background:var(--yellow);"
	"box-shadow:0 0 12px var(--yellow)}"
	".grid{display:grid;grid-template-columns:repeat(12,1fr);gap:16px}"
	".card{background:linear-gradient(180deg,rgba(28,44,76,.85),rgba(16,25,45,.92));"
	"border:1px solid var(--line);border-radius:18px;padding:20px;box-shadow:0 18px 40px "
	"rgba(0,0,0,.28)}.card h3{margin:0 0 12px;font-size:12px;letter-spacing:1.4px;"
	"text-transform:uppercase;color:var(--muted);font-weight:700}"
	".big{font-size:52px;font-weight:800;line-height:1;letter-spacing:-2px}"
	".big small{font-size:15px;font-weight:600;color:var(--muted);letter-spacing:0;"
	"margin-left:8px}.span4{grid-column:span 4}.span6{grid-column:span 6}"
	".span8{grid-column:span 8}.span12{grid-column:span 12}"
	".kv{display:flex;justify-content:space-between;padding:9px 0;border-bottom:1px dashed "
	"rgba(34,48,77,.85);font-size:14px}.kv:last-child{border-bottom:0}.kv span:first-child"
	"{color:var(--muted)}.kv b{font-weight:650;font-variant-numeric:tabular-nums}"
	".pillrow{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}.tag{padding:6px 10px;"
	"border-radius:9px;font-size:12px;font-weight:700;background:rgba(56,217,245,.12);"
	"color:var(--cyan);border:1px solid rgba(56,217,245,.25)}.tag.off{background:rgba(143,"
	"163,200,.1);color:var(--muted);border-color:var(--line)}.tag.green{background:rgba("
	"61,220,151,.12);color:var(--green);border-color:rgba(61,220,151,.25)}.tag.red{"
	"background:rgba(255,93,115,.12);color:var(--red);border-color:rgba(255,93,115,.25)}"
	".event{margin-top:8px;padding:12px 14px;border-radius:12px;background:var(--panel2);"
	"border:1px solid var(--line);font-size:13px;color:var(--muted)}"
	".event b{color:var(--text)}table{width:100%;border-collapse:collapse;font-size:13px}"
	"th{text-align:left;color:var(--muted);font-size:11px;letter-spacing:1px;"
	"text-transform:uppercase;padding:0 10px 10px;border-bottom:1px solid var(--line)}"
	"td{padding:10px;border-bottom:1px solid rgba(34,48,77,.55);font-variant-numeric:"
	"tabular-nums}tr:last-child td{border-bottom:0}.scroll{max-height:320px;overflow:auto}"
	".foot{color:var(--muted);font-size:12px;margin-top:22px;display:flex;"
	"justify-content:space-between;gap:12px;flex-wrap:wrap}"
	"@media(max-width:980px){.span4,.span6,.span8{grid-column:span 12}"
	"header,main{padding-left:18px;padding-right:18px}}"
	"</style></head><body><header><div class=\"brand\"><div class=\"logo\">C</div><div>"
	"<h1>CoCIS Smart Control</h1><div class=\"sub\">Makerere University · Security, "
	"occupancy and lighting</div></div></div><div class=\"clock\" id=\"clock\">--</div>"
	"</header><main><div class=\"hero\"><div><div class=\"sub\" id=\"role\">Terminal</div>"
	"<h2 id=\"room\">Loading…</h2></div><div id=\"accessPill\" class=\"pill\"><span "
	"class=\"dot\"></span><span id=\"accessText\">CHECKING</span></div></div>"
	"<div class=\"grid\"><section class=\"card span4\"><h3>People inside</h3>"
	"<div class=\"big\" id=\"occupancy\">0<small>building</small></div>"
	"<div class=\"pillrow\"><span class=\"tag\" id=\"insideTag\">0 identified</span>"
	"<span class=\"tag off\" id=\"localTag\">local 0</span></div>"
	"<div class=\"event\" id=\"lastEvent\">No access event yet.</div></section>"
	"<section class=\"card span4\"><h3>Lighting</h3>"
	"<div class=\"big\" id=\"light\">OFF</div>"
	"<div class=\"pillrow\"><span class=\"tag off\" id=\"motionTag\">PIR CLEAR</span>"
	"<span class=\"tag off\" id=\"emergencyTag\">NORMAL</span></div>"
	"<div class=\"event\">Lights follow room occupancy and the PIR hold timer.</div>"
	"</section><section class=\"card span4\"><h3>Access schedule</h3>"
	"<div class=\"big\" id=\"schedule\">--:--</div>"
	"<div class=\"event\" id=\"scheduleDetail\">Schedule not loaded.</div>"
	"<div class=\"pillrow\"><span class=\"tag\" id=\"cardsTag\">0 cards</span>"
	"<span class=\"tag off\" id=\"lockdownTag\">No lockdown</span></div></section>"
	"<section class=\"card span8\"><h3>Recent access records</h3><div class=\"scroll\">"
	"<table><thead><tr><th>Timestamp</th><th>Room</th><th>Event</th><th>UID</th>"
	"<th>Detail</th></tr></thead><tbody id=\"logs\"><tr><td colspan=\"5\">Loading…"
	"</td></tr></tbody></table></div></section>"
	"<section class=\"card span4\"><h3>System health</h3>"
	"<div class=\"kv\"><span>Granted</span><b id=\"grants\">0</b></div>"
	"<div class=\"kv\"><span>Denied</span><b id=\"denials\">0</b></div>"
	"<div class=\"kv\"><span>Allowlist</span><b id=\"cards\">0</b></div>"
	"<div class=\"kv\"><span>Log size</span><b id=\"logsize\">0 B</b></div>"
	"<div class=\"kv\"><span>Dashboard</span><b style=\"color:var(--green)\">ONLINE</b>"
	"</div><div class=\"foot\"><span>Configuration and card enrolment use the serial "
	"console.</span></div></section></div>"
	"<div class=\"foot\"><span>© 2026 CoCIS Smart Control and Monitoring System</span>"
	"<span>Auto-refresh every 3 seconds</span></div></main><script>"
	"const $=id=>document.getElementById(id);"
	"function pad(n){return String(n).padStart(2,'0')}"
	"function human(bytes){if(bytes<1024)return bytes+' B';if(bytes<1048576)return "
	"(bytes/1024).toFixed(1)+' KiB';return (bytes/1048576).toFixed(2)+' MiB'}"
	"async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});"
	"const s=await r.json();$('room').textContent=s.room_name;$('role').textContent="
	"'Room ID '+s.room_id+(s.entrance?' · entrance terminal':' · room terminal');"
	"$('occupancy').innerHTML=s.global_occupancy+'<small>building</small>';"
	"$('insideTag').textContent=s.inside_count+' identified';"
	"$('localTag').textContent='local '+s.local_occupancy;"
	"$('light').textContent=s.light_on?'ON':'OFF';"
	"$('motionTag').textContent=s.motion?'PIR MOTION':'PIR CLEAR';"
	"$('motionTag').className='tag '+(s.motion?'green':'off');"
	"$('emergencyTag').textContent=s.emergency?'EMERGENCY':(s.lockdown?'LOCKDOWN':"
	"'NORMAL');$('emergencyTag').className='tag '+(s.emergency||s.lockdown?'red':'off');"
	"$('schedule').textContent=s.schedule_start+' - '+s.schedule_end;"
	"$('scheduleDetail').textContent=s.override_active?'Temporary override is active.':"
	"'Daily access window configured by the administrator.';"
	"$('cardsTag').textContent=s.allowed_count+' cards';"
	"$('cards').textContent=s.allowed_count;"
	"$('lockdownTag').textContent=s.lockdown?'Lockdown active':'No lockdown';"
	"$('lockdownTag').className='tag '+(s.lockdown?'red':'off');"
	"$('grants').textContent=s.grants;$('denials').textContent=s.denials;"
	"$('logsize').textContent=human(s.log_bytes);"
	"const open=s.schedule_open||s.override_active;const pill=$('accessPill');"
	"$('accessText').textContent=s.emergency?'EMERGENCY RELEASE':s.lockdown?'LOCKDOWN':"
	"open?'ACCESS OPEN':'ACCESS CLOSED';pill.className='pill '+(s.emergency||s.lockdown?"
	"'closed':open?'':'warn');"
	"$('lastEvent').innerHTML=s.last_event?'<b>'+s.last_event_time+'</b> · '+"
	"s.last_event:'No access event yet.';"
	"$('clock').textContent=s.clock;"
	"}catch(e){$('accessText').textContent='OFFLINE';"
	"$('accessPill').className='pill closed';}}"
	"async function logs(){try{const r=await fetch('/api/logs',{cache:'no-store'});"
	"const text=await r.text();const lines=text.trim().split(/\\r?\\n/);"
	"const rows=lines.slice(1).slice(-40).reverse();"
	"$('logs').innerHTML=rows.length?rows.map(l=>{const c=l.split(',');"
	"return '<tr><td>'+(c[0]||'')+'</td><td>'+(c[2]||'')+'</td><td>'+(c[3]||'')+"
	"'</td><td>'+(c[4]||'')+'</td><td>'+(c[5]||'')+'</td></tr>'}).join(''):"
	"'<tr><td colspan=\"5\">No records yet.</td></tr>';}catch(e){"
	"$('logs').innerHTML='<tr><td colspan=\"5\">Log unavailable.</td></tr>';}}"
	"refresh();logs();setInterval(refresh,3000);setInterval(logs,5000);"
	"</script></body></html>";

static esp_err_t root_handler(httpd_req_t *request)
{
	httpd_resp_set_type(request, "text/html; charset=utf-8");
	httpd_resp_set_hdr(request, "Cache-Control", "no-store");
	return httpd_resp_send(request, dashboard_html, HTTPD_RESP_USE_STRLEN);
}

static void snapshot_status(web_status_t *status)
{
	if (status_mutex != NULL) {
		xSemaphoreTake(status_mutex, portMAX_DELAY);
	}
	*status = current_status;
	if (status_mutex != NULL) {
		xSemaphoreGive(status_mutex);
	}
}

static esp_err_t status_handler(httpd_req_t *request)
{
	web_status_t status;
	snapshot_status(&status);
	char clock_text[32] = "not-set";
	char schedule_start[8];
	char schedule_end[8];
	char last_time[16] = "--:--:--";
	if (status.clock_set) {
		struct tm local_time;
		if (localtime_r(&status.now, &local_time) != NULL) {
			strftime(clock_text, sizeof(clock_text), "%Y-%m-%d %H:%M:%S", &local_time);
			strftime(last_time, sizeof(last_time), "%H:%M:%S", &local_time);
		}
	}
	snprintf(schedule_start, sizeof(schedule_start), "%02u:%02u",
			 status.schedule_start_minute / 60, status.schedule_start_minute % 60);
	snprintf(schedule_end, sizeof(schedule_end), "%02u:%02u",
			 status.schedule_end_minute / 60, status.schedule_end_minute % 60);

	char payload[1024];
	snprintf(payload, sizeof(payload),
			 "{\"room_name\":\"%s\",\"room_id\":%u,\"entrance\":%s,\"clock\":\"%s\","
			 "\"local_occupancy\":%d,\"global_occupancy\":%d,\"inside_count\":%u,"
			 "\"allowed_count\":%u,\"schedule_start\":\"%s\",\"schedule_end\":\"%s\","
			 "\"schedule_open\":%s,\"override_active\":%s,\"lockdown\":%s,"
			 "\"emergency\":%s,\"light_on\":%s,\"motion\":%s,\"grants\":%u,"
			 "\"denials\":%u,\"log_bytes\":%u,\"last_event\":\"%s\","
			 "\"last_event_time\":\"%s\"}",
			 status.room_name, (unsigned)status.room_id,
			 status.entrance ? "true" : "false", clock_text, (int)status.local_occupancy,
			 (int)status.global_occupancy, (unsigned)status.inside_count,
			 (unsigned)status.allowed_count, schedule_start, schedule_end,
			 status.schedule_open ? "true" : "false",
			 status.override_active ? "true" : "false",
			 status.lockdown ? "true" : "false", status.emergency ? "true" : "false",
			 status.light_on ? "true" : "false", status.motion ? "true" : "false",
			 (unsigned)status.grants, (unsigned)status.denials,
			 (unsigned)storage_log_size(), status.last_event, last_time);
	httpd_resp_set_type(request, "application/json");
	httpd_resp_set_hdr(request, "Cache-Control", "no-store");
	return httpd_resp_send(request, payload, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t logs_handler(httpd_req_t *request)
{
	static char logs[8192];
	size_t length = storage_read_logs(logs, sizeof(logs));
	httpd_resp_set_type(request, "text/csv; charset=utf-8");
	httpd_resp_set_hdr(request, "Cache-Control", "no-store");
	if (length == 0) {
		return httpd_resp_sendstr(request, "timestamp,room_id,room,event,uid,detail\n");
	}
	return httpd_resp_send(request, logs, length);
}
#endif

void webui_start(uint8_t room_id)
{
#if CONFIG_SMART_SYSTEM_ENABLE_WEB
	status_mutex = xSemaphoreCreateMutex();
	configASSERT(status_mutex != NULL);
	wifi_config_t ap_config = {0};
	snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "CoCIS-Smart-%u",
			 (unsigned)room_id);
	ap_config.ap.ssid_len = (uint8_t)strlen((char *)ap_config.ap.ssid);
	ap_config.ap.channel = 1;
	ap_config.ap.max_connection = 4;
	const char *password = CONFIG_SMART_SYSTEM_WEB_AP_PASSWORD;
	if (strlen(password) >= 8) {
		ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
		snprintf((char *)ap_config.ap.password, sizeof(ap_config.ap.password), "%s",
				 password);
	} else {
		ap_config.ap.authmode = WIFI_AUTH_OPEN;
		ESP_LOGW(TAG, "Web AP password is shorter than 8 characters; AP is open");
	}

	esp_err_t result = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
	if (result != ESP_OK) {
		ESP_LOGW(TAG, "Could not configure the access point: %s",
				 esp_err_to_name(result));
		return;
	}

	httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
	server_config.server_port = 80;
	server_config.lru_purge_enable = true;
	httpd_handle_t server = NULL;
	result = httpd_start(&server, &server_config);
	if (result != ESP_OK) {
		ESP_LOGW(TAG, "Web server failed to start: %s", esp_err_to_name(result));
		return;
	}

	httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
	httpd_uri_t status_uri = {
		.uri = "/api/status", .method = HTTP_GET, .handler = status_handler};
	httpd_uri_t logs_uri = {
		.uri = "/api/logs", .method = HTTP_GET, .handler = logs_handler};
	httpd_register_uri_handler(server, &root_uri);
	httpd_register_uri_handler(server, &status_uri);
	httpd_register_uri_handler(server, &logs_uri);
	web_active = true;
	ESP_LOGI(TAG, "Dashboard ready at http://192.168.4.1 (AP CoCIS-Smart-%u)",
			 (unsigned)room_id);
#else
	(void)room_id;
	ESP_LOGI(TAG, "Web dashboard disabled in this build");
#endif
}

bool webui_active(void)
{
	return web_active;
}

void webui_update_status(const web_status_t *status)
{
	if (status == NULL) {
		return;
	}
	if (status_mutex != NULL) {
		xSemaphoreTake(status_mutex, portMAX_DELAY);
	}
	current_status = *status;
	if (status_mutex != NULL) {
		xSemaphoreGive(status_mutex);
	}
}
