#include "wifi_diag.hpp"

#ifndef COYOPEDAL_WIFI_DIAG
#define COYOPEDAL_WIFI_DIAG 0
#endif

#if COYOPEDAL_WIFI_DIAG

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

namespace coyopedal::wifi_diag {
namespace {

constexpr const char* kTag = "wifi_diag";

std::uint8_t g_own[6]{};

bool mac_eq(const std::uint8_t* a, const std::uint8_t* b) { return std::memcmp(a, b, 6) == 0; }
bool is_broadcast(const std::uint8_t* a) {
    return a[0] == 0xff && a[1] == 0xff && a[2] == 0xff && a[3] == 0xff && a[4] == 0xff && a[5] == 0xff;
}
std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

const char* auth_mode_name(const wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN: return "open";
        case WIFI_AUTH_WEP: return "wep";
        case WIFI_AUTH_WPA_PSK: return "wpa-psk";
        case WIFI_AUTH_WPA2_PSK: return "wpa2-psk";
        case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2-psk";
        case WIFI_AUTH_WPA3_PSK: return "wpa3-sae";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3 transition";
        case WIFI_AUTH_OWE: return "owe";
        default: return "other";
    }
}

const char* cipher_name(const wifi_cipher_type_t cipher) {
    switch (cipher) {
        case WIFI_CIPHER_TYPE_NONE: return "none";
        case WIFI_CIPHER_TYPE_TKIP: return "tkip";
        case WIFI_CIPHER_TYPE_CCMP: return "ccmp";
        case WIFI_CIPHER_TYPE_TKIP_CCMP: return "tkip+ccmp";
        case WIFI_CIPHER_TYPE_GCMP: return "gcmp";
        case WIFI_CIPHER_TYPE_GCMP256: return "gcmp256";
        case WIFI_CIPHER_TYPE_AES_CMAC128: return "cmac128";
        default: return "other";
    }
}

const char* mgmt_subtype_name(const unsigned subtype) {
    switch (subtype) {
        case 0x0: return "assoc-req";
        case 0x1: return "assoc-resp";
        case 0x2: return "reassoc-req";
        case 0x3: return "reassoc-resp";
        case 0x4: return "probe-req";
        case 0x5: return "probe-resp";
        case 0x8: return "beacon";
        case 0xa: return "disassoc";
        case 0xb: return "auth";
        case 0xc: return "deauth";
        case 0xd: return "action";
        default: return "mgmt";
    }
}

// The EAPOL-Key message number follows from the key-information flags
// (IEEE 802.11-2020, 12.7.6): ACK without MIC is message 1, MIC without ACK
// and without Secure is message 2, ACK with MIC and Install is message 3,
// MIC with Secure and no ACK is message 4.
const char* eapol_message(const std::uint16_t key_info) {
    const bool ack = key_info & 0x0080;
    const bool mic = key_info & 0x0100;
    const bool secure = key_info & 0x0200;
    const bool install = key_info & 0x0040;
    if (ack && !mic) return "1/4";
    if (mic && !ack && !secure) return "2/4";
    if (ack && mic && install) return "3/4";
    if (mic && !ack && secure) return "4/4";
    return "?";
}

// Runs on the Wi-Fi task for every received frame that passes the filter
// (management and data). Only frames addressed to or from this station are
// logged, plus broadcast deauth/disassoc, so the console carries a handful of
// lines per join attempt rather than the channel's traffic.
void rx(void* buffer, const wifi_promiscuous_pkt_type_t type) {
    const auto* const packet = static_cast<wifi_promiscuous_pkt_t*>(buffer);
    const std::uint8_t* const frame = packet->payload;
    const unsigned length = packet->rx_ctrl.sig_len;
    if (length < 24) return;
    const unsigned frame_type = (frame[0] >> 2) & 0x3;
    const unsigned subtype = (frame[0] >> 4) & 0xf;
    const bool protected_frame = (frame[1] & 0x40) != 0;
    const std::uint8_t* const addr1 = frame + 4;
    const std::uint8_t* const addr2 = frame + 10;
    const bool to_us = mac_eq(addr1, g_own);
    const bool from_us = mac_eq(addr2, g_own);
    const int rssi = packet->rx_ctrl.rssi;
    const unsigned channel = packet->rx_ctrl.channel;

    if (frame_type == 0) {
        const bool broadcast_kick = is_broadcast(addr1) && (subtype == 0xa || subtype == 0xc);
        if (!to_us && !from_us && !broadcast_kick) return;
        const std::uint8_t* const body = frame + 24;
        const unsigned body_length = length - 24;
        const char* const direction = to_us ? "to us" : from_us ? "from us" : "broadcast";
        char detail[64] = "";
        if (subtype == 0xb && body_length >= 6) {
            std::snprintf(detail, sizeof detail, " alg=%u seq=%u status=%u", le16(body), le16(body + 2),
                          le16(body + 4));
        } else if ((subtype == 0x1 || subtype == 0x3) && body_length >= 4) {
            std::snprintf(detail, sizeof detail, " status=%u aid=%u", le16(body + 2),
                          body_length >= 6 ? (le16(body + 4) & 0x3fff) : 0U);
        } else if ((subtype == 0xa || subtype == 0xc) && body_length >= 2) {
            std::snprintf(detail, sizeof detail, " reason=%u", le16(body));
        } else if (subtype == 0xd && body_length >= 1) {
            std::snprintf(detail, sizeof detail, " category=%u", body[0]);
        }
        ESP_LOGI(kTag, "%s %s %02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x%s rssi=%d ch=%u%s",
                 mgmt_subtype_name(subtype), direction, addr2[0], addr2[1], addr2[2], addr2[3], addr2[4],
                 addr2[5], addr1[0], addr1[1], addr1[2], addr1[3], addr1[4], addr1[5], detail, rssi, channel,
                 (frame[1] & 0x08) ? " retry" : "");
        return;
    }

    if (frame_type == 2 && type == WIFI_PKT_DATA && (to_us || from_us) && !protected_frame) {
        // Unencrypted data to or from us is the four-way handshake, or nothing
        // we care about. QoS data carries two more header bytes.
        unsigned header = (subtype & 0x8) ? 26 : 24;
        if (frame[1] & 0x80) header += 4;  // HT control
        if (length < header + 8 + 4) return;
        const std::uint8_t* const llc = frame + header;
        if (llc[0] != 0xaa || llc[1] != 0xaa || llc[2] != 0x03) return;
        if (be16(llc + 6) != 0x888e) return;
        const std::uint8_t* const eapol = llc + 8;
        const unsigned eapol_length = length - header - 8;
        if (eapol[1] != 3 || eapol_length < 4 + 1 + 2) {
            ESP_LOGI(kTag, "eapol %s type=%u rssi=%d", to_us ? "to us" : "from us", eapol[1], rssi);
            return;
        }
        const std::uint16_t key_info = be16(eapol + 5);
        ESP_LOGI(kTag, "eapol-key %s message %s key_info=0x%04x descriptor=%u length=%u rssi=%d%s",
                 to_us ? "to us" : "from us", eapol_message(key_info), key_info, eapol[4], be16(eapol + 2),
                 rssi, (frame[1] & 0x08) ? " retry" : "");
    }
}

}  // namespace

void start(const std::uint8_t own_mac[6]) {
    std::memcpy(g_own, own_mac, sizeof g_own);
    // The supplicant logs through the "wpa" tag; the driver sets its own level
    // from the image's maximum at init. The maximum is DEBUG in this image.
    esp_log_level_set("wpa", ESP_LOG_DEBUG);
    esp_log_level_set(kTag, ESP_LOG_INFO);
    wifi_promiscuous_filter_t filter{};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_err_t err = esp_wifi_set_promiscuous_filter(&filter);
    if (err == ESP_OK) err = esp_wifi_set_promiscuous_rx_cb(&rx);
    if (err == ESP_OK) err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "frame tap unavailable: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(kTag, "frame tap on for %02x:%02x:%02x:%02x:%02x:%02x; supplicant at debug level", g_own[0],
             g_own[1], g_own[2], g_own[3], g_own[4], g_own[5]);
}

void log_scan_of(const char* ssid) {
    wifi_scan_config_t config{};
    config.ssid = reinterpret_cast<std::uint8_t*>(const_cast<char*>(ssid));
    config.show_hidden = true;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    const esp_err_t err = esp_wifi_scan_start(&config, true);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "scan for %s failed: %s", ssid, esp_err_to_name(err));
        return;
    }
    std::uint16_t count = 8;
    wifi_ap_record_t records[8]{};
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        ESP_LOGW(kTag, "scan records unavailable");
        return;
    }
    ESP_LOGI(kTag, "scan for %s: %u result(s)", ssid, static_cast<unsigned>(count));
    for (unsigned i = 0; i < count; i++) {
        const wifi_ap_record_t& ap = records[i];
        ESP_LOGI(kTag,
                 "  %02x:%02x:%02x:%02x:%02x:%02x ch=%u rssi=%d auth=%s(%d) pairwise=%s group=%s phy=%s%s%s%s bw=%d "
                 "country=%c%c",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5], ap.primary, ap.rssi,
                 auth_mode_name(ap.authmode), static_cast<int>(ap.authmode), cipher_name(ap.pairwise_cipher),
                 cipher_name(ap.group_cipher), ap.phy_11b ? "b" : "", ap.phy_11g ? "g" : "", ap.phy_11n ? "n" : "",
                 ap.phy_11ax ? "ax" : "", static_cast<int>(ap.bandwidth), ap.country.cc[0] ? ap.country.cc[0] : '?',
                 ap.country.cc[1] ? ap.country.cc[1] : '?');
    }
}

}  // namespace coyopedal::wifi_diag

#else

namespace coyopedal::wifi_diag {
void start(const std::uint8_t*) {}
void log_scan_of(const char*) {}
}  // namespace coyopedal::wifi_diag

#endif
