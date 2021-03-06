#ifndef DUNK__ESP32__GETURL_H
#define DUNK__ESP32__GETURL_H

#include <curl/curl.h>

namespace mrdunk {

struct Match {
  const char* string;
  uint8_t result;
};

class HttpRequest {
 public:
  static uint8_t globalInit;
  HttpRequest(const char* url);
  ~HttpRequest();
  void curlTest();
  uint8_t get();
  uint8_t getHttps();
  void setPostFields(const char* postFields);
  struct Match match;

 private:
  static size_t writeData(void *buffer, size_t size, size_t nmemb, void *userp);
	CURL *handle;
  char* _url;
};

} // namespace mrdunk
#endif  // DUNK__ESP32__GETURL_H

