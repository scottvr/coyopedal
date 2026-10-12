#pragma once

#include <cstdint>

// Bench diagnostics for the maintenance-mode Wi-Fi join. Compiled in only when
// the board's defines set COYOPEDAL_WIFI_DIAG=1; every call is a no-op
// otherwise, so remote_service.cpp can call them unconditionally.
//
// What the diagnostic image adds to the console:
//   - the supplicant's debug output ("wpa"): SAE commit/confirm building, the
//     parse of every reply, the four-way handshake checks;
//   - the driver's own debug output ("wifi"), which follows the image's
//     maximum log level;
//   - a targeted scan before the first connect, logging what the station
//     sees of the network (channel, signal, auth mode, ciphers, PHY);
//   - every management frame the radio hears addressed to or from this
//     station, with the fields that matter (auth algorithm, sequence and
//     status; association status; deauth/disassoc reason) and the unencrypted
//     EAPOL-Key frames of the four-way handshake, from the pedal's own antenna.
//     The radio stays a station; promiscuous reception only adds a tap.
namespace coyopedal::wifi_diag {

// After esp_wifi_start(), before the first esp_wifi_connect().
void start(const std::uint8_t own_mac[6]);

// Blocking targeted scan for `ssid`, logged record by record.
void log_scan_of(const char* ssid);

}  // namespace coyopedal::wifi_diag
