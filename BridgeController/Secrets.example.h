#pragma once
// Copy to Secrets.h, edit, and DO NOT commit your Secrets.h.
#define BRIDGE_WIFI_SSID "YOUR_WIFI_SSID"
#define BRIDGE_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define BRIDGE_ADMIN_USER "admin"
#define BRIDGE_ADMIN_PASSWORD "REPLACE_WITH_A_LONG_UNIQUE_PASSWORD"
// API key for other devices (minimum 16 characters). Send as X-API-Key.
#define BRIDGE_API_KEY "REPLACE_WITH_A_RANDOM_API_KEY_OF_32_CHARACTERS"
#define BRIDGE_OTA_PASSWORD "REPLACE_WITH_ANOTHER_LONG_UNIQUE_PASSWORD"

// Home Assistant is an optional local Matter controller, not a cloud dependency.
// Generate a long-lived token in Home Assistant > Profile > Security.
// This token is a secret: NEVER enter it in the dashboard or commit Secrets.h.
#define BRIDGE_HA_TOKEN "REPLACE_WITH_HOME_ASSISTANT_LONG_LIVED_ACCESS_TOKEN"
