#include "notify.h"
#include "wifi_setup.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

static String token_;
static String chat_;

void notifyBegin() {
  token_ = wifiSetupTgToken();
  chat_ = wifiSetupTgChat();
}

bool notifyIsConfigured() {
  return token_.length() > 0 && chat_.length() > 0;
}

// Percent-encodes a value for an application/x-www-form-urlencoded body.
// The message text carries spaces, colons and the "->" in a transition
// line, none of which can travel raw in a form body.
static String urlEncode(const String &s) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

bool notifySend(const String &text) {
  if (!notifyIsConfigured()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  NetworkClientSecure client;
  client.setInsecure(); // same reasoning as the price and chart fetchers:
                        // a pinned CA root would expire and silently break
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  String url = "https://api.telegram.org/bot" + token_ + "/sendMessage";
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String payload = "chat_id=" + urlEncode(chat_) + "&text=" + urlEncode(text);
  int code = http.POST(payload);
  http.end();
  return code == 200;
}
