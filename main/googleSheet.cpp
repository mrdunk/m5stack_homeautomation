#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_tls.h"
#include "googleSheet.h"

#include "esp_http_client.h"

static const char *TAG = "googleSheet";

static const char *REQUEST = "GET %s%s HTTP/1.0\r\n"
    "Host: " WEB_SERVER "\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

/* Root cert for google.com, taken from google_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect www.google.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const uint8_t google_root_cert_pem_start[] asm("_binary_google_root_cert_pem_start");
extern const uint8_t google_root_cert_pem_end[]   asm("_binary_google_root_cert_pem_end");

#define HEADER_LOCATION "Location: "

static char pageBuffer[1024];
static char locationBuffer[512];

uint8_t googleSheet_connect(const char* url, const char* data) {
  int ret, len, doneHeaders = 0;
  char statusCode[4] = "";

  esp_tls_cfg_t cfg = {};
  cfg.cacert_pem_buf = google_root_cert_pem_start;
  cfg.cacert_pem_bytes = google_root_cert_pem_end - google_root_cert_pem_start;

  ESP_LOGI(TAG, "Connect to %s", url);
  struct esp_tls *tls = esp_tls_conn_http_new(url, &cfg);

  if(tls == NULL) {
    ESP_LOGW(TAG, "Connection to %s failed.", url);
    return 0;
  }
  ESP_LOGI(TAG, "Connection established.");
  
  size_t written_bytes = 0;
  char request[strlen(url) + strlen(data) + strlen(REQUEST)] = "";
  do {
    snprintf(request, strlen(url) + strlen(data) + strlen(REQUEST), REQUEST, url, data);
    ESP_LOGD(TAG, "Request: %s", request);
    ret = esp_tls_conn_write(tls, 
                             request + written_bytes, 
                             strlen(request) - written_bytes);
    if (ret >= 0) {
      ESP_LOGD(TAG, "%d bytes written", ret);
      written_bytes += ret;
    } else if (ret != MBEDTLS_ERR_SSL_WANT_READ  && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      ESP_LOGW(TAG, "esp_tls_conn_write  returned 0x%x", ret);
      esp_tls_conn_delete(tls);    
      return 0;
    }
  } while(written_bytes < strlen(request));

  ESP_LOGD(TAG, "Reading HTTP response...");
  bzero(pageBuffer, sizeof(pageBuffer));
  do {
    len = sizeof(pageBuffer) - 1 - strlen(pageBuffer);
    ESP_LOGD(TAG, "%d buffer space", len);
    bzero(pageBuffer + strlen(pageBuffer), len + 1);
    ret = esp_tls_conn_read(tls, pageBuffer + strlen(pageBuffer), len);
    if(ret == MBEDTLS_ERR_SSL_WANT_WRITE  || ret == MBEDTLS_ERR_SSL_WANT_READ) {
      continue;
    }
    if(ret < 0) {
      ESP_LOGE(TAG, "esp_tls_conn_read  returned -0x%x", -ret);
      break;
    }
    if(ret == 0) {
      ESP_LOGI(TAG, "connection closed");
      break;
    }

    len = ret;
    ESP_LOGD(TAG, "%d bytes read", len);

    // Find Status code
    if(strlen(statusCode) == 0) {
      // TODO: Confirm first line is valid HTTP.
      ESP_LOGD(TAG, "%s", pageBuffer);
      for(int i = 0; pageBuffer[i] != '\n'; i++) {
        if(pageBuffer[i] == ' ') {
          strncpy(statusCode, &pageBuffer[i + 1], 3);
          ESP_LOGI(TAG, "statusCode: %s", statusCode);
          break;
        }
      }
      const char* lineEnd = strstr(pageBuffer, "\r");
      if(!lineEnd) {
        ESP_LOGW(TAG, "Reached end of first line in HTTP request before expected.");
        break;
      }
      memmove(pageBuffer, lineEnd + 2, strlen(lineEnd + 2) + 1);
    }

    while(!doneHeaders) {
      // Strip off a header.
      const char* lineColon = strstr(pageBuffer, ":");
      const char* lineEnd = strstr(pageBuffer, "\r\n");
      if(!lineColon || !lineEnd) {
        break;
      }

      while((*lineColon == ' ' || *lineColon == ':') && lineColon < lineEnd) {
        lineColon++;
      }

      if(strncmp(pageBuffer, "\r\n", 2) == 0) {
        ESP_LOGD(TAG, "Blank line means end of headers.");
        doneHeaders = 1;
      } else {
        ESP_LOGD(TAG, "header:\t%.*s\t=\t%.*s",
                      lineColon - pageBuffer - 1, pageBuffer,
                      lineEnd - lineColon, lineColon);
      }

      if(strncmp(pageBuffer, HEADER_LOCATION, strlen(HEADER_LOCATION)) == 0) {
        bzero(locationBuffer, sizeof(locationBuffer));
        strncpy(locationBuffer, lineColon, lineEnd - lineColon);
        if(lineEnd - lineColon > sizeof(locationBuffer)) {
          ESP_LOGW(TAG, "Location larger than buffer. %i vs %i",
                   lineEnd - lineColon, sizeof(locationBuffer));
        }
      }

      memmove(pageBuffer, lineEnd + 2, strlen(lineEnd + 2) + 1);
    }

    if(doneHeaders) {
      // Print page content directly to stdout as it is read
      ESP_LOGD(TAG, "HTML content: %s", pageBuffer);
      bzero(pageBuffer, sizeof(pageBuffer));
    }
  } while(1);

  esp_tls_conn_delete(tls);
  
  if(strncmp(statusCode, "302", 3) == 0 && strlen(locationBuffer) > 0) {
    ESP_LOGI(TAG, "Performing HTTP redirect to %s", locationBuffer);
    return googleSheet_connect(locationBuffer, "");
  }
  //if(strncmp(statusCode, "404", 3) == 0) {
  //  ESP_LOGW(TAG, "Request returned 404: %s", url);
  //  return 0;
  //}
  return 1;
}
