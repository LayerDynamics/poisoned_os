#include "esp32_marauder_driver.h"

#include <furi.h>

#include <stdio.h>
#include <string.h>

#define OBSERVE(id, label, command) \
    {id, label, command, Esp32MarauderCapabilityObserve, false, false}
#define CONTROL(id, label, command) \
    {id, label, command, Esp32MarauderCapabilityControl, false, false}
#define CONTROL_ARG(id, label, command) \
    {id, label, command, Esp32MarauderCapabilityControl, true, false}
#define CAPTURE(id, label, command) \
    {id, label, command, Esp32MarauderCapabilityControl, false, true}
#define ACTIVE(id, label, command) \
    {id, label, command, Esp32MarauderCapabilityActive, false, false}
#define ACTIVE_ARG(id, label, command) \
    {id, label, command, Esp32MarauderCapabilityActive, true, false}
#define ADMIN(id, label, command) {id, label, command, Esp32MarauderCapabilityAdmin, false, false}

static const Esp32MarauderCommandDescriptor esp32_marauder_commands[] = {
    OBSERVE("info", "Device info", "info"),
    OBSERVE("help", "Command help", "help"),
    OBSERVE("list.ap", "List access points", "list -a"),
    OBSERVE("list.ssid", "List SSIDs", "list -s"),
    OBSERVE("list.station", "List stations", "list -c"),
    OBSERVE("list.airtag", "List AirTags", "list -t"),
    OBSERVE("list.ip", "List IP addresses", "list -i"),
    OBSERVE("list.probe", "List probes", "list -p"),
    OBSERVE("settings.show", "Show settings", "settings"),
    OBSERVE("channel.get", "Show channel", "channel"),
    OBSERVE("gps.fix", "GPS fix", "gps -g fix"),
    OBSERVE("gps.satellites", "GPS satellites", "gps -g sat"),
    OBSERVE("gps.latitude", "GPS latitude", "gps -g lat"),
    OBSERVE("gps.longitude", "GPS longitude", "gps -g lon"),
    OBSERVE("gps.altitude", "GPS altitude", "gps -g alt"),
    OBSERVE("gps.date", "GPS date", "gps -g date"),
    CONTROL("scan.all", "Scan all", "scanall"),
    CONTROL("scan.ping", "Ping scan", "pingscan"),
    CONTROL("scan.arp", "ARP scan", "arpscan"),
    CONTROL_ARG("ssid.add", "Add named SSID", "ssid -a -n {arg}"),
    CONTROL("ssid.random", "Add random SSID", "ssid -a -g"),
    CONTROL_ARG("ssid.remove", "Remove SSID", "ssid -r {arg}"),
    CONTROL_ARG("select.ap", "Select access point", "select -a {arg}"),
    CONTROL_ARG("select.ssid", "Select SSID", "select -s {arg}"),
    CONTROL_ARG("select.station", "Select station", "select -c {arg}"),
    CONTROL_ARG("ap.info", "Access point details", "info -a {arg}"),
    CONTROL("mac.random.ap", "Randomize AP MAC", "randapmac"),
    CONTROL("mac.random.station", "Randomize station MAC", "randstamac"),
    CONTROL_ARG("mac.clone.ap", "Clone AP MAC", "cloneapmac -a {arg}"),
    CONTROL_ARG("mac.clone.station", "Clone station MAC", "clonestamac -s {arg}"),
    CONTROL_ARG("wifi.join", "Join access point", "join {arg}"),
    CONTROL("wifi.join.saved", "Join saved network", "join -s"),
    CONTROL("clear.ap", "Clear AP list", "clearlist -a"),
    CONTROL("clear.ssid", "Clear SSID list", "clearlist -s"),
    CONTROL("clear.station", "Clear station list", "clearlist -c"),
    CONTROL("wardrive.ap", "Wardrive access points", "wardrive"),
    CONTROL("wardrive.station", "Wardrive stations", "wardrive -s"),
    CONTROL("wardrive.flock", "Wardrive Flock devices", "wardrive -f"),
    CONTROL("gps.tracker", "GPS tracker", "gps -t"),
    CONTROL("gps.stream", "GPS stream", "gpsdata"),
    CONTROL("gps.poi.start", "Start GPS POI", "gpspoi -s"),
    CONTROL("gps.poi.mark", "Mark GPS POI", "gpspoi -m"),
    CONTROL("gps.poi.end", "End GPS POI", "gpspoi -e"),
    CONTROL_ARG("channel.set", "Set channel", "channel -s {arg}"),
    CONTROL_ARG("led.color", "Set LED color", "led -s {arg}"),
    CONTROL_ARG("led.pattern", "Set LED pattern", "led -p {arg}"),
    CONTROL_ARG("settings.set", "Set setting", "settings -s {arg}"),
    CONTROL("settings.restore", "Restore settings", "settings -r"),
    CONTROL("stop", "Stop current operation", "stopscan"),
    CAPTURE("sniff.beacon", "Capture beacons", "sniffbeacon"),
    CAPTURE("sniff.deauth", "Capture deauthentication", "sniffdeauth"),
    CAPTURE("sniff.pmkid", "Capture PMKID", "sniffpmkid"),
    CAPTURE("sniff.probe", "Capture probes", "sniffprobe"),
    CAPTURE("sniff.pwn", "Capture Pwnagotchi", "sniffpwn"),
    CAPTURE("sniff.raw", "Capture raw packets", "sniffraw"),
    CAPTURE("sniff.bt", "Capture Bluetooth", "sniffbt"),
    CAPTURE("sniff.skim", "Capture skimmers", "sniffskim"),
    CAPTURE("sniff.airtag", "Capture AirTags", "sniffbt -t airtag"),
    CAPTURE("sniff.flipper", "Capture Flippers", "sniffbt -t flipper"),
    CAPTURE("sniff.flock", "Capture Flock devices", "sniffbt -t flock"),
    CAPTURE("sniff.meta", "Capture Meta devices", "sniffbt -t meta"),
    CAPTURE("sniff.mac", "Track MAC addresses", "mactrack"),
    CAPTURE("sniff.packet_count", "Count packets", "packetcount"),
    CAPTURE("sniff.pineapple", "Capture PineAP", "sniffpinescan"),
    CAPTURE("sniff.multissid", "Capture multiple SSIDs", "sniffmultissid"),
    CAPTURE("sniff.sae", "Capture SAE", "sniffsae"),
    CONTROL("signal.monitor", "Signal monitor", "sigmon"),
    ACTIVE("attack.deauth", "Deauthentication attack", "attack -t deauth"),
    ACTIVE("attack.probe", "Probe attack", "attack -t probe"),
    ACTIVE("attack.rickroll", "Rickroll beacon", "attack -t rickroll"),
    ACTIVE("attack.funny", "Funny beacon", "attack -t funny"),
    ACTIVE("attack.badmsg", "Malformed message attack", "attack -t badmsg"),
    ACTIVE("attack.sleep", "Sleep attack", "attack -t sleep"),
    ACTIVE("attack.sae", "SAE flood", "attack -t sae"),
    ACTIVE("attack.csa", "Channel switch attack", "attack -t csa"),
    ACTIVE("attack.quiet", "Quiet attack", "attack -t quiet"),
    ACTIVE("spam.apple.sour", "Sour Apple spam", "blespam -t sourapple"),
    ACTIVE("spam.apple.juice", "AppleJuice spam", "blespam -t applejuice"),
    ACTIVE("spam.windows", "Swift Pair spam", "blespam -t windows"),
    ACTIVE("spam.samsung", "Samsung spam", "blespam -t samsung"),
    ACTIVE("spam.google", "Google spam", "blespam -t google"),
    ACTIVE("spam.flipper", "Flipper spam", "blespam -t flipper"),
    ACTIVE("spam.all", "All Bluetooth spam", "blespam -t all"),
    ACTIVE_ARG("spoof.airtag", "Spoof AirTag", "spoofat -t {arg}"),
    ACTIVE("beacon.ap", "Beacon AP list", "attack -t beacon -a"),
    ACTIVE("beacon.ssid", "Beacon SSID list", "attack -t beacon -l"),
    ACTIVE("beacon.random", "Beacon random SSIDs", "attack -t beacon -r"),
    ACTIVE_ARG("attack.target.ap", "Target AP", "attack -t deauth -c {arg}"),
    ACTIVE_ARG("attack.target.station", "Target station", "attack -t deauth -s {arg}"),
    ACTIVE_ARG("attack.karma", "Karma probe", "karma -p {arg}"),
    ACTIVE_ARG("evil_portal.start", "Start evil portal", "evilportal -c start {arg}"),
    ACTIVE_ARG("evil_portal.html", "Set evil portal HTML", "evilportal -c sethtml {arg}"),
    ACTIVE_ARG("evil_portal.ap", "Set evil portal AP", "evilportal -c setap {arg}"),
    CONTROL_ARG("port_scan.all", "Scan all ports", "portscan -a -t {arg}"),
    CONTROL_ARG("port_scan.service", "Scan service port", "portscan -s {arg}"),
    ADMIN("shutdown", "Shut down WiFi board", "stopscan -f"),
    ADMIN("update.sd", "Update from board SD", "update -s"),
    ADMIN("reboot", "Reboot WiFi board", "reboot"),
};

void esp32_marauder_driver_on_system_start(void* context) {
    UNUSED(context);
}

void esp32_marauder_driver_start(void) {
    esp32_marauder_driver_on_system_start(NULL);
}

size_t esp32_marauder_command_count(void) {
    return COUNT_OF(esp32_marauder_commands);
}

const Esp32MarauderCommandDescriptor* esp32_marauder_command_at(size_t index) {
    return index < COUNT_OF(esp32_marauder_commands) ? &esp32_marauder_commands[index] : NULL;
}

const Esp32MarauderCommandDescriptor* esp32_marauder_command_find(const char* id) {
    if(!id || id[0] == '\0') return NULL;
    for(size_t index = 0; index < COUNT_OF(esp32_marauder_commands); ++index) {
        if(strcmp(id, esp32_marauder_commands[index].id) == 0) {
            return &esp32_marauder_commands[index];
        }
    }
    return NULL;
}

static bool esp32_marauder_argument_valid(const char* argument) {
    if(!argument) return false;
    const size_t length = strnlen(argument, ESP32_MARAUDER_ARGUMENT_MAX + 1u);
    if(length == 0u || length > ESP32_MARAUDER_ARGUMENT_MAX) return false;
    for(size_t index = 0; index < length; ++index) {
        const uint8_t value = (uint8_t)argument[index];
        if(value < 0x20u || value == 0x7fu) return false;
    }
    return true;
}

bool esp32_marauder_command_format(
    const char* id,
    const char* argument,
    char* output,
    size_t output_capacity) {
    if(!output || output_capacity == 0u) return false;
    const Esp32MarauderCommandDescriptor* descriptor = esp32_marauder_command_find(id);
    if(!descriptor) return false;
    const char* placeholder = strstr(descriptor->command_template, "{arg}");
    if(descriptor->argument_required != (placeholder != NULL)) return false;
    if(descriptor->argument_required && !esp32_marauder_argument_valid(argument)) return false;
    if(!descriptor->argument_required && argument && argument[0] != '\0') return false;

    int written;
    if(placeholder) {
        const size_t prefix = (size_t)(placeholder - descriptor->command_template);
        written = snprintf(
            output,
            output_capacity,
            "%.*s%s%s\n",
            (int)prefix,
            descriptor->command_template,
            argument,
            placeholder + sizeof("{arg}") - 1u);
    } else {
        written = snprintf(output, output_capacity, "%s\n", descriptor->command_template);
    }
    return written > 0 && (size_t)written < output_capacity;
}

#undef ADMIN
#undef ACTIVE_ARG
#undef ACTIVE
#undef CAPTURE
#undef CONTROL_ARG
#undef CONTROL
#undef OBSERVE
