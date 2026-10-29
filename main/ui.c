#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

#define UI_WIDTH 60

static bool color_enabled = true;

#define C_RESET "\033[0m"
#define C_BOLD "\033[1m"
#define C_DIM "\033[2m"
#define C_CYAN "\033[36m"
#define C_GREEN "\033[32m"
#define C_RED "\033[31m"
#define C_YELLOW "\033[33m"
#define C_BLUE "\033[34m"
#define C_MAGENTA "\033[35m"
#define C_GRAY "\033[90m"

static const char *color(const char *code)
{
	return color_enabled ? code : "";
}

void ui_set_color(bool enabled)
{
	color_enabled = enabled;
}

bool ui_color_enabled(void)
{
	return color_enabled;
}

static void rule(const char *left, const char *fill, const char *right)
{
	printf("%s%s", color(C_GRAY), left);
	for (int index = 0; index < UI_WIDTH + 2; ++index) {
		printf("%s", fill);
	}
	printf("%s\n", right);
}

static void top(const char *title)
{
	printf("%s┌─", color(C_GRAY));
	printf("%s %s%s %s", color(C_BOLD), color(C_CYAN), title, color(C_GRAY));
	int used = (int)strlen(title) + 3;
	for (int index = used; index < UI_WIDTH + 2; ++index) {
		printf("─");
	}
	printf("┐%s\n", color(C_RESET));
}

static void bottom(void)
{
	rule("└", "─", "┘");
}

static void separator(void)
{
	rule("├", "─", "┤");
}

static void row(const char *format, ...)
{
	char text[256];
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	printf("%s│%s %-*.*s %s│%s\n", color(C_GRAY), color(C_RESET), UI_WIDTH, UI_WIDTH,
		   text, color(C_GRAY), color(C_RESET));
}

static void label_row(const char *label, const char *value_format, ...)
{
	char value[192];
	va_list arguments;
	va_start(arguments, value_format);
	vsnprintf(value, sizeof(value), value_format, arguments);
	va_end(arguments);
	char line[256];
	snprintf(line, sizeof(line), "%-12s %s", label, value);
	row("%s", line);
}

static void format_clock(time_t now, bool clock_set, char *buffer, size_t size)
{
	if (!clock_set) {
		snprintf(buffer, size, "NOT SET  (use: time YYYY-MM-DD HH:MM:SS)");
		return;
	}
	struct tm local_time;
	if (localtime_r(&now, &local_time) == NULL) {
		snprintf(buffer, size, "NOT AVAILABLE");
		return;
	}
	strftime(buffer, size, "%a %Y-%m-%d %H:%M:%S EAT", &local_time);
}

static void format_duration(uint32_t seconds, char *buffer, size_t size)
{
	uint32_t hours = seconds / 3600;
	uint32_t minutes = (seconds % 3600) / 60;
	uint32_t remainder = seconds % 60;
	snprintf(buffer, size, "%02u:%02u:%02u", (unsigned)hours, (unsigned)minutes,
			 (unsigned)remainder);
}

void ui_banner(const char *room_name, uint8_t room_id, bool entrance, bool exit_node)
{
	printf("%s", color(C_CYAN));
	printf("\n");
	printf("  ██████╗ ██████╗  ██████╗██╗███████╗\n");
	printf(" ██╔════╝██╔═══██╗██╔════╝██║██╔════╝\n");
	printf(" ██║     ██║   ██║██║     ██║███████╗\n");
	printf(" ██║     ██║   ██║██║     ██║╚════██║\n");
	printf(" ╚██████╗╚██████╔╝╚██████╗██║███████║\n");
	printf("  ╚═════╝ ╚═════╝  ╚═════╝╚═╝╚══════╝\n");
	printf("%s", color(C_RESET));
	printf("%s  SMART CONTROL AND MONITORING SYSTEM%s\n", color(C_BOLD), color(C_RESET));
	printf("%s  Makerere University · College of Computing and Information Sciences%s\n\n",
		   color(C_GRAY), color(C_RESET));
	const char *role = entrance ? "ENTRY + EXIT" : (exit_node ? "EXIT" : "ROOM");
	printf("  Terminal : %s%s%s  ·  %s%s%s\n", color(C_CYAN), room_name, color(C_RESET),
		   color(C_CYAN), role, color(C_RESET));
	printf("  Room ID  : %s%u%s\n\n", color(C_CYAN), (unsigned)room_id, color(C_RESET));
}

void ui_help(void)
{
	top("COMMAND REFERENCE");
	row("%sMONITORING%s", color(C_BOLD), color(C_RESET));
	row("  status                Full system dashboard");
	row("  who                   People currently inside (entrance)");
	row("  rooms                 Known ESP-NOW terminals");
	row("  events [count]        Latest access records");
	row("  logs                  Complete CSV access history");
	row("  stats                 Access counters and log size");
	row("  uptime                Time since boot");
	row("  menu                  Show the management menu");
	row("  clear                 Clear the serial screen");
	separator();
	row("%sCONFIGURATION  (login required)%s", color(C_BOLD), color(C_RESET));
	row("  login <PIN>           Start an administrator session");
	row("  logout                End the administrator session");
	row("  enroll [seconds]      Tap a card to add it to the allowlist");
	row("  allow <8-hex-UID>     Add a card manually");
	row("  deny  <8-hex-UID>     Remove a card");
	row("  schedule <HH:MM> <HH:MM>   Daily access window");
	row("  time <YYYY-MM-DD> <HH:MM:SS>   Set the clock");
	row("  pin <digits>          Change the administrator PIN");
	row("  lockdown on|off       Deny all entry (exit still allowed)");
	row("  emergency on|off      Release both doors immediately");
	row("  override <minutes>    Open the schedule temporarily");
	row("  color on|off          ANSI colour output");
	row("  export                Print the CSV log header and rows");
	row("  wipe                  Delete all access records");
	row("  reboot                Restart the terminal");
	row("  help                  Show this reference");
	bottom();
}

void ui_menu(void)
{
	top("MANAGEMENT MENU");
	row("%sMONITOR%s", color(C_CYAN), color(C_RESET));
	row("  status     Live dashboard     who       People inside");
	row("  rooms      ESP-NOW terminals  events    Recent records");
	row("  stats      Counters           logs      Full CSV log");
	separator();
	row("%sADMINISTRATION%s", color(C_MAGENTA), color(C_RESET));
	row("  login      Start session      enroll    Add a card");
	row("  allow      Add a UID          deny      Remove a UID");
	row("  schedule   Access window      time      Set the clock");
	row("  lockdown   Deny entry         emergency Release doors");
	row("  override   Open temporarily   pin       Change PIN");
	separator();
	row("%sTIPS%s", color(C_YELLOW), color(C_RESET));
	row("  Type any command directly, for example: status");
	row("  Default PIN is 1234 - change it after first login.");
	bottom();
}

void ui_clear_screen(void)
{
	printf("\033[2J\033[H");
	fflush(stdout);
}

void ui_print_status(const ui_status_t *status)
{
	char clock_text[48];
	char uptime_text[16];
	format_clock(status->now, status->clock_set, clock_text, sizeof(clock_text));
	format_duration(status->uptime_seconds, uptime_text, sizeof(uptime_text));

	const char *role = status->entrance ? "ENTRY + EXIT"
									   : (status->exit_node ? "EXIT" : "ROOM");
	const char *access = status->emergency ? "EMERGENCY RELEASE"
						 : status->lockdown ? "LOCKDOWN"
						 : (status->schedule_open || status->override_active) ? "OPEN"
																			   : "CLOSED";
	const char *security = status->emergency ? "EMERGENCY"
						   : status->lockdown ? "LOCKDOWN"
											  : "NORMAL";
	const char *light = status->light_on ? "ON" : "OFF";
	const char *motion = status->motion ? "MOTION" : "CLEAR";

	printf("\n");
	top("COCIS SMART CONTROL - LIVE STATUS");
	row("%s%-12s%s %s", color(C_GRAY), "Terminal", color(C_RESET), status->room_name);
	row("%s%-12s%s %s  ·  Room ID %u", color(C_GRAY), "Role", color(C_RESET), role,
		(unsigned)status->room_id);
	row("%s%-12s%s %s", color(C_GRAY), "Clock", color(C_RESET), clock_text);
	separator();

	printf("%s│%s %-12s ", color(C_GRAY), color(C_RESET), "Access");
	if (status->emergency) {
		printf("%s%s%s", color(C_RED), access, color(C_RESET));
	} else if (status->lockdown) {
		printf("%s%s%s", color(C_RED), access, color(C_RESET));
	} else if (status->schedule_open || status->override_active) {
		printf("%s%s%s", color(C_GREEN), access, color(C_RESET));
	} else {
		printf("%s%s%s", color(C_YELLOW), access, color(C_RESET));
	}
	printf("  ·  Window %02u:%02u-%02u:%02u", status->schedule_start_minute / 60,
		   status->schedule_start_minute % 60, status->schedule_end_minute / 60,
		   status->schedule_end_minute % 60);
	if (status->override_active) {
		printf(" %s(override active)%s", color(C_YELLOW), color(C_RESET));
	}
	printf("\n");

	label_row("Security", "%s", security);
	label_row("Occupancy", "%d inside building  ·  local %d", status->global_occupancy,
			  status->local_occupancy);
	label_row("Inside list", "%u identified people", (unsigned)status->inside_count);
	row("Lighting     %s  ·  PIR %s  ·  Allowlist %u cards", light, motion,
		(unsigned)status->allowed_count);
	row("Network      ESP-NOW sync  ·  Web dashboard %s",
		status->web_active ? "ONLINE" : "disabled");
	row("Log storage  %u bytes  ·  Uptime %s", (unsigned)status->log_bytes, uptime_text);
	if (status->authenticated) {
		row("%sAdministrator session active%s", color(C_GREEN), color(C_RESET));
	} else {
		row("Administrator session: not authenticated");
	}
	separator();
	row("%sGranted %u   Denied %u   Entries %u   Exits %u%s", color(C_BOLD),
		(unsigned)status->grants, (unsigned)status->denials, (unsigned)status->entries,
		(unsigned)status->exits, color(C_RESET));
	if (status->last_event[0] != '\0') {
		char when[32] = "time-not-set";
		if (status->last_event_time >= 1760000000) {
			struct tm local_time;
			if (localtime_r(&status->last_event_time, &local_time) != NULL) {
				strftime(when, sizeof(when), "%H:%M:%S", &local_time);
			}
		}
		printf("%s│%s Last %s  %s%s%s\n", color(C_GRAY), color(C_RESET), when,
			   status->last_event_ok ? color(C_GREEN) : color(C_RED),
			   status->last_event, color(C_RESET));
	} else {
		row("Last event   none since boot");
	}
	bottom();
	printf("\n");
}

static void format_uid(const uint8_t uid[4], char *buffer, size_t size)
{
	snprintf(buffer, size, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
}

void ui_print_who(const inside_record_t *records, uint8_t count)
{
	printf("\n");
	top("PEOPLE CURRENTLY INSIDE");
	if (count == 0) {
		row("No identified people are currently inside.");
		bottom();
		printf("\n");
		return;
	}
	row("%s%-3s %-10s %-20s %s%s", color(C_BOLD), "#", "UID", "Entered", "Duration",
		color(C_RESET));
	separator();
	time_t now = time(NULL);
	for (uint8_t index = 0; index < count; ++index) {
		if (!records[index].valid) {
			continue;
		}
		char uid_text[12];
		char entered[24] = "unknown";
		char duration[16] = "unknown";
		format_uid(records[index].uid, uid_text, sizeof(uid_text));
		if (records[index].entered_at >= 1760000000) {
			time_t entered_at = (time_t)records[index].entered_at;
			struct tm local_time;
			if (localtime_r(&entered_at, &local_time) != NULL) {
				strftime(entered, sizeof(entered), "%Y-%m-%d %H:%M:%S", &local_time);
			}
			format_duration((uint32_t)(now - entered_at), duration, sizeof(duration));
		}
		row("%-3u %-10s %-20s %s", (unsigned)(index + 1), uid_text, entered, duration);
	}
	bottom();
	printf("\n");
}

void ui_print_rooms(const remote_node_info_t *nodes, size_t count)
{
	printf("\n");
	top("ESP-NOW TERMINAL NETWORK");
	row("This terminal broadcasts a signed occupancy contribution every second.");
	if (count == 0) {
		row("No remote terminals have been heard yet.");
		bottom();
		printf("\n");
		return;
	}
	separator();
	row("%s%-18s %-9s %-10s %s%s", color(C_BOLD), "MAC", "Room ID", "Occupancy",
		"Age", color(C_RESET));
	separator();
	for (size_t index = 0; index < count; ++index) {
		char mac[20];
		snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", nodes[index].mac[0],
				 nodes[index].mac[1], nodes[index].mac[2], nodes[index].mac[3],
				 nodes[index].mac[4], nodes[index].mac[5]);
		char age[16];
		snprintf(age, sizeof(age), "%u ms", (unsigned)nodes[index].age_ms);
		row("%-18s %-9u %-10d %s", mac, (unsigned)nodes[index].room_id,
			(int)nodes[index].occupancy, age);
	}
	bottom();
	printf("\n");
}

void ui_print_stats(const ui_status_t *status)
{
	char uptime_text[16];
	format_duration(status->uptime_seconds, uptime_text, sizeof(uptime_text));
	printf("\n");
	top("ACCESS STATISTICS");
	row("Granted entries/exits   %u", (unsigned)(status->entries + status->exits));
	row("Granted events          %u", (unsigned)status->grants);
	row("Denied events           %u", (unsigned)status->denials);
	row("Allowlisted cards       %u", (unsigned)status->allowed_count);
	row("People inside           %u", (unsigned)status->inside_count);
	row("Log size                %u bytes", (unsigned)status->log_bytes);
	row("Uptime                  %s", uptime_text);
	bottom();
	printf("\n");
}

static void log_message(const char *prefix, const char *code, const char *format,
						va_list arguments)
{
	printf("%s[%s]%s ", color(code), prefix, color(C_RESET));
	vprintf(format, arguments);
	printf("\n");
	fflush(stdout);
}

void ui_ok(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	log_message("OK", C_GREEN, format, arguments);
	va_end(arguments);
}

void ui_error(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	log_message("ERROR", C_RED, format, arguments);
	va_end(arguments);
}

void ui_warn(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	log_message("WARN", C_YELLOW, format, arguments);
	va_end(arguments);
}

void ui_info(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	log_message("INFO", C_CYAN, format, arguments);
	va_end(arguments);
}

void ui_event(bool granted, const char *event, const char *uid, const char *detail,
			  const char *room)
{
	char when[16] = "--:--:--";
	time_t now = time(NULL);
	if (now >= 1760000000) {
		struct tm local_time;
		if (localtime_r(&now, &local_time) != NULL) {
			strftime(when, sizeof(when), "%H:%M:%S", &local_time);
		}
	}
	printf("%s[%s]%s %s  %-7s %s  %-10s %s\n", color(C_GRAY), when, color(C_RESET),
		   granted ? "ACCESS" : "DENIED", event, room != NULL ? room : "", uid,
		   detail != NULL ? detail : "");
	fflush(stdout);
}
