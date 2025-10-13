#include <esp_camera.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>           // <-- Add this line
#include <UniversalTelegramBot.h>

//
// WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//
//            You must select partition scheme from the board menu that has at least 3MB APP space.
//            Face Recognition is DISABLED for ESP32 and ESP32-S2, because it takes up from 15
//            seconds to process single frame. Face Detection is ENABLED if PSRAM is enabled as well

// ===================
// Select camera model
// ===================
//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
// #define CAMERA_MODEL_ESP_EYE  // Has PSRAM
//#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_CAMS3_UNIT  // Has PSRAM
#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
//#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
// ** Espressif Internal Boards **
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD
//#define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3 // Has PSRAM
//#define CAMERA_MODEL_DFRobot_Romeo_ESP32S3 // Has PSRAM
#include "camera_pins.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "qwerty";
const char *password = "eprom2022";

// Telegram Bot credentials
const char* botToken = "7950984672:AAGn7jHn4fqM12_8pwgR6wqFZzz_GNpvyYo";
// const char* chatId = "8084143922";
const char* chatId = "5638142909";
WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

void startCameraServer();
void setupLedFlash(int pin);

// --- Helper callbacks for streaming photo ---
camera_fb_t *fb_g = nullptr;
int fb_pos = 0;

bool moreDataAvailable() {
  return fb_pos < fb_g->len;
}

uint8_t getNextByte() {
  return fb_g->buf[fb_pos++];
}

unsigned char* getNextBuffer() {
  return nullptr; // Not used, but required by API
}

int resetFunc() {
  fb_pos = 0;
  return 0;
}
// --- End helper callbacks ---

void sendGreetingToTelegram() {
  String greeting = "ESP32-CAM Online!\n";
  greeting += "Selamat datang di Bot Kotak Amal Nurul Ilmi.\n\n";
  greeting += "Sistem siap menerima perintah. Gunakan tombol di bawah atau ketik perintah manual.\n\n";
  greeting += "Untuk bantuan lengkap, ketik /help";
  sendTelegram(greeting);
}

unsigned long lastBotCheck = 0;
const unsigned long botCheckInterval = 2000; // 2 seconds
String pendingCommand = "";
unsigned long gpsRequestTime = 0;
const unsigned long gpsTimeout = 30000; // 30 seconds timeout for GPS requests

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    text.trim();
    
    if (text == "/capture") {
      Serial.println("[Bot] /capture command received");
      captureAndSendPhoto();
    } else if (text == "/gps") {
      Serial.println("[Bot] /gps command received");
      Serial.println("REQUEST_GPS"); // Send request to Mega
      pendingCommand = "gps";
      gpsRequestTime = millis(); // Set request time for timeout
      sendTelegram("📍 Meminta data GPS dari Arduino Mega...\nTunggu sebentar untuk mendapatkan lokasi.");
    } else if (text.startsWith("/enroll")) {
      Serial.println("[Bot] /enroll command received");
      handleEnrollCommand(text);
    } else if (text.startsWith("/delete")) {
      Serial.println("[Bot] /delete command received");
      handleDeleteCommand(text);
    } else if (text == "/enroll") {
      Serial.println("[Bot] /enroll button pressed");
      sendTelegram("Untuk mendaftarkan sidik jari, gunakan format:\n/enroll <id>\n\nContoh: /enroll 1\nRentang ID: 1-127");
    } else if (text == "/delete") {
      Serial.println("[Bot] /delete button pressed");
      sendTelegram("Untuk menghapus sidik jari, gunakan format:\n/delete <id>\n\nContoh: /delete 1\nRentang ID: 1-127");
    } else if (text == "/status") {
      Serial.println("[Bot] /status command received");
      sendSystemStatus();
    } else if (text == "/test") {
      Serial.println("[Bot] /test command received");
      Serial.println("test_comm");
      Serial.flush();
      sendTelegram("Menguji komunikasi dengan Arduino Mega...");
    } else if (text == "/help") {
      Serial.println("[Bot] /help command received");
      sendHelpMessage();
    }
  }
}

void captureAndSendPhoto() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    sendTelegram("Gagal mengambil gambar");
    return;
  }
  fb_g = fb;
  fb_pos = 0;
  String result = bot.sendPhotoByBinary(
    String(chatId),
    "image/jpeg",
    fb->len,
    moreDataAvailable,
    getNextByte,
    getNextBuffer,
    resetFunc
  );
  esp_camera_fb_return(fb);
  if (result.indexOf("true") > 0) {
    Serial.println("[Bot] Foto berhasil dikirim ke Telegram");
  } else {
    Serial.println("[Bot] Gagal mengirim foto");
    sendTelegram("Gagal mengirim foto ke Telegram");
  }
}

void sendTelegram(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    bool sent = bot.sendMessage(chatId, message, "");
    if (sent) {
      Serial.print("[Telegram] Terkirim: ");
      Serial.println(message);
    } else {
      Serial.println("[Telegram] Gagal mengirim pesan");
    }
  } else {
    Serial.println("[Telegram] WiFi tidak terhubung, tidak dapat mengirim pesan");
  }
}

void handleEnrollCommand(String text) {
  // Parse /enroll <id> command
  int spaceIndex = text.indexOf(' ');
  if (spaceIndex == -1) {
    sendTelegram("Penggunaan: /enroll <id>\nContoh: /enroll 1\nRentang ID: 1-127");
    return;
  }
  
  String idStr = text.substring(spaceIndex + 1);
  int id = idStr.toInt();
  
  if (id < 1 || id > 127) {
    sendTelegram("ID tidak valid. Silakan gunakan ID antara 1-127");
    return;
  }
  
  // Send enrollment command to Arduino Mega
  String enrollCommand = "enroll " + String(id);
  Serial.println(enrollCommand);
  Serial.flush(); // Ensure the command is sent immediately
  
  sendTelegram("Memulai pendaftaran sidik jari untuk ID: " + String(id) + "\n\n" +
               "Silakan ikuti instruksi di layar LCD.\n" +
               "1. Letakkan jari Anda di sensor\n" +
               "2. Hapus jari saat diminta\n" +
               "3. Letakkan jari yang sama lagi\n" +
               "4. Tunggu konfirmasi");
}

void handleDeleteCommand(String text) {
  // Parse /delete <id> command
  int spaceIndex = text.indexOf(' ');
  if (spaceIndex == -1) {
    sendTelegram("Penggunaan: /delete <id>\nContoh: /delete 1\nRentang ID: 1-127");
    return;
  }
  
  String idStr = text.substring(spaceIndex + 1);
  int id = idStr.toInt();
  
  if (id < 1 || id > 127) {
    sendTelegram("ID tidak valid. Silakan gunakan ID antara 1-127");
    return;
  }
  
  // Send deletion command to Arduino Mega
  String deleteCommand = "delete " + String(id);
  Serial.println(deleteCommand);
  Serial.flush(); // Ensure the command is sent immediately
  
  sendTelegram("Memulai penghapusan sidik jari untuk ID: " + String(id) + "\n\n" +
               "Ini akan menghapus sidik jari secara permanen dari sistem.");
}

void sendSystemStatus() {
  String status = "📊 Status Sistem:\n\n";
  status += "🟢 ESP32-CAM: Online\n";
  status += "📶 WiFi: Terhubung\n";
  status += "📷 Kamera: Siap\n";
  status += "🤖 Bot Telegram: Aktif\n";
  status += "📱 R308 Sidik Jari: Siap\n";
  status += "🛰️ GPS Neo 8M: Terintegrasi\n\n";
  status += "Perintah yang Tersedia:\n";
  status += "• /enroll <id> - Daftar sidik jari baru\n";
  status += "• /delete <id> - Hapus sidik jari\n";
  status += "• /capture - Ambil foto\n";
  status += "• /gps - Dapatkan lokasi GPS\n";
  status += "• /test - Uji komunikasi\n";
  status += "• /help - Bantuan lengkap";
  
  sendTelegram(status);
}

void sendHelpMessage() {
  String help = "Bantuan Bot Kotak Amal Nurul Ilmi\n\n";
  help += "Perintah yang Tersedia:\n\n";
  help += "/enroll <id> - Mendaftar sidik jari baru\n";
  help += "   Contoh: /enroll 1\n";
  help += "   Rentang ID: 1-127\n\n";
  help += "/delete <id> - Menghapus sidik jari\n";
  help += "   Contoh: /delete 1\n";
  help += "   Rentang ID: 1-127\n\n";
  help += "/test - Uji komunikasi dengan Arduino Mega\n\n";
  help += "/capture - Ambil dan kirim foto\n\n";
  help += "/gps - Dapatkan lokasi GPS saat ini (dengan timeout 30 detik)\n\n";
  help += "/status - Dapatkan status sistem\n\n";
  help += "/help - Tampilkan pesan bantuan ini\n\n";
  help += "Tips:\n";
  help += "• Jaga jari Anda bersih dan kering untuk pendaftaran\n";
  help += "• Tekan jari dengan kuat di sensor\n";
  help += "• Ikuti instruksi LCD dengan cermat\n";
  help += "• Penghapusan adalah permanen dan tidak dapat dibatalkan";
  
  sendTelegram(help);
}

void sendIPToTelegram() {
  String ip = WiFi.localIP().toString();
  String msg = "IP ESP32-CAM: <a href=\"http://" + ip + ":81/stream\">http://" + ip + ":81/stream</a>\n";
  msg += "Klik tautan untuk melihat stream kamera.\n\n";
  msg += "Gunakan tombol di bawah untuk mengirim perintah.";
  String keyboard = "{\"inline_keyboard\":[[";
  keyboard += "{\"text\":\"Ambil Foto\",\"callback_data\":\"/capture\"},";
  keyboard += "{\"text\":\"Dapatkan GPS\",\"callback_data\":\"/gps\"}";
  keyboard += "],[";
  keyboard += "{\"text\":\"Daftar Sidik Jari\",\"callback_data\":\"/enroll\"},";
  keyboard += "{\"text\":\"Hapus Sidik Jari\",\"callback_data\":\"/delete\"}";
  keyboard += "],[";
  keyboard += "{\"text\":\"Uji Komunikasi\",\"callback_data\":\"/test\"},";
  keyboard += "{\"text\":\"Status Sistem\",\"callback_data\":\"/status\"}";
  keyboard += "],[";
  keyboard += "{\"text\":\"Bantuan\",\"callback_data\":\"/help\"}";
  keyboard += "]]}";
  String payload = "chat_id=" + String(chatId) + "&text=" + msg + "&parse_mode=HTML&reply_markup=" + keyboard;
  String url = "https://api.telegram.org/bot" + String(botToken) + "/sendMessage";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http; // <-- Use HTTPClient, not HttpClient
  http.begin(client, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    Serial.println("[Telegram] Alamat IP berhasil dikirim ke chat");
  } else {
    Serial.println("[Telegram] Gagal mengirim alamat IP");
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;  // Reduce from 20MHz to 10MHz
  config.frame_size = FRAMESIZE_VGA;  // Reduce from UXGA to VGA (640x480)
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 20;  // Increase quality number (lower quality = less power)
  config.fb_count = 1;  // Reduce frame buffer count

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  // Connect to WiFi
  Serial.print("Menghubungkan ke WiFi");
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  // Add power management for WiFi
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // Reduce WiFi power
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi terhubung");
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.localIP());

  client.setInsecure(); // For Telegram HTTPS
  Serial.println("Bot Telegram siap.");

  sendGreetingToTelegram();
  sendIPToTelegram();

  startCameraServer();

  Serial.print("Kamera Siap! Gunakan 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' untuk terhubung");
}

void loop() {
  // Check for GPS request timeout
  if (pendingCommand == "gps" && millis() - gpsRequestTime > gpsTimeout) {
    sendTelegram("⏰ Timeout: GPS request tidak mendapat respons dalam 30 detik.\nCoba lagi atau periksa koneksi Arduino Mega.");
    pendingCommand = "";
    Serial.println("[GPS] Request timeout");
  }
  
  // Listen for Serial commands from Arduino Mega
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      if (command.startsWith("GPS_DATA:")) {
        // GPS_DATA: Lat=..., Lng=..., Alt=...m, Sat=...
        if (pendingCommand == "gps") {
          String gpsInfo = command.substring(9); // Remove "GPS_DATA:" prefix
          sendTelegram("📍 Lokasi GPS:\n" + gpsInfo);
          pendingCommand = "";
        }
      } else if (command.startsWith("GPS_STATUS:")) {
        // GPS_STATUS: No satellite signal, Satellites=...
        if (pendingCommand == "gps") {
          String gpsStatus = command.substring(11); // Remove "GPS_STATUS:" prefix
          sendTelegram("📍 Status GPS:\n" + gpsStatus);
          pendingCommand = "";
        }
      } else if (command.startsWith("GPS_DIAGNOSTIC:")) {
        // GPS_DIAGNOSTIC: ...
        String gpsDiag = command.substring(15); // Remove "GPS_DIAGNOSTIC:" prefix
        Serial.println("[GPS Diagnostic] " + gpsDiag);
      } else if (command.startsWith("GPS_CRITICAL:")) {
        // GPS_CRITICAL: ...
        String gpsCritical = command.substring(13); // Remove "GPS_CRITICAL:" prefix
        sendTelegram("🚨 GPS Critical: " + gpsCritical);
        Serial.println("[GPS Critical] " + gpsCritical);
      } else if (command.startsWith("GPS_BAUD_DISCOVERY:")) {
        // GPS_BAUD_DISCOVERY: ...
        String gpsBaud = command.substring(19); // Remove "GPS_BAUD_DISCOVERY:" prefix
        sendTelegram("🔧 GPS Baud Discovery: " + gpsBaud);
        Serial.println("[GPS Baud Discovery] " + gpsBaud);
      } else if (command.startsWith("GPS_FINAL_DIAGNOSTIC:")) {
        // GPS_FINAL_DIAGNOSTIC: ...
        String gpsFinal = command.substring(21); // Remove "GPS_FINAL_DIAGNOSTIC:" prefix
        sendTelegram("🔍 GPS Final Diagnostic: " + gpsFinal);
        Serial.println("[GPS Final Diagnostic] " + gpsFinal);
      } else if (command.startsWith("R308_ENROLL_START:")) {
        // Enrollment started
        sendTelegram("🔄 " + command.substring(18));
      } else if (command.startsWith("R308_ENROLL_SUCCESS:")) {
        // Enrollment successful
        sendTelegram("✅ " + command.substring(20));
      } else if (command.startsWith("R308_ENROLL_FAILED:")) {
        // Enrollment failed
        sendTelegram("❌ " + command.substring(19));
      } else if (command.startsWith("R308_ENROLL_ERROR:")) {
        // Enrollment error
        sendTelegram("⚠️ " + command.substring(18));
      } else if (command.startsWith("R308_DELETE_START:")) {
        // Deletion started
        sendTelegram("🗑️ " + command.substring(18));
      } else if (command.startsWith("R308_DELETE_SUCCESS:")) {
        // Deletion successful
        sendTelegram("✅ " + command.substring(20));
      } else if (command.startsWith("R308_DELETE_FAILED:")) {
        // Deletion failed
        sendTelegram("❌ " + command.substring(19));
      } else if (command.startsWith("R308_DELETE_ERROR:")) {
        // Deletion error
        sendTelegram("⚠️ " + command.substring(18));
      } else if (command.startsWith("R308_ACCESS_GRANTED:")) {
        // Access granted
        sendTelegram("🔓 " + command.substring(20));
      } else if (command.startsWith("R308_ACCESS_DENIED:")) {
        // Access denied
        sendTelegram("🚫 " + command.substring(19));
      } else if (command.startsWith("R308_STATUS:")) {
        // R308 status
        sendTelegram("📱 " + command.substring(12));
      } else if (command.startsWith("R308_COMM_TEST:")) {
        // Communication test response
        sendTelegram("✅ " + command.substring(15));
      } else if (command.startsWith("R308_HEARTBEAT:")) {
        // Heartbeat from Arduino Mega
        Serial.println("[Heartbeat] Arduino Mega is running");
      } else if (command.startsWith("VIBRATION_ALERT")) {
        // Vibration detected
        sendTelegram("⚠️ Vibrasi terdeteksi! Mungkin upaya penyusupan.");
      } else if (command.startsWith("DOOR_UNLOCKED")) {
        // Door unlocked
        sendTelegram("🔓 Pintu telah dibuka");
      } else if (command.startsWith("DOOR_LOCKED")) {
        // Door locked
        sendTelegram("🔒 Pintu telah dikunci");
      } else {
        // Generic command
        sendTelegram(command);
      }
    }
  }
  // Poll Telegram bot for new messages (reduced frequency)
  if (millis() - lastBotCheck > botCheckInterval) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastBotCheck = millis();
  }
  delay(50); // Increased delay to reduce CPU usage and heat
}
