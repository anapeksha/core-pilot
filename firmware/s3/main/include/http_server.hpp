#pragma once

#include "esp_http_server.h"

#include <ctime>

namespace core_security {
class RandomSource;
class StorageService;
} // namespace core_security

namespace core_pilot::ai {
class AiStorageService;
}

namespace core_pilot {

class ActivityService;
class AiChatService;
class AutomationService;
class CoreMatter;
class CoreNetwork;
class DeviceRegistry;
class ModelRepository;
class OtaService;
class SmtpService;
class WifiProvisionService;

struct HttpRuntime final {
  CoreNetwork& network;
  ActivityService& activity;
  AutomationService& automations;
  CoreMatter& matter;
  DeviceRegistry& devices;
  ModelRepository& models;
  WifiProvisionService& wifi_provision;
  ai::AiStorageService& ai_storage;
  AiChatService& ai_chat;
  OtaService& ota;
  SmtpService& smtp;
  core_security::StorageService& storage;
  core_security::RandomSource& random;
};

esp_err_t initializeTimeService(const HttpRuntime& runtime);
void servicePendingTimeNetworkEvent();
bool consumePendingTimeSync(std::time_t& timestamp);
void publishRtcReady(bool ready);
httpd_handle_t startHttpServer(const HttpRuntime& runtime);

} // namespace core_pilot
