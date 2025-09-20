#include <WiFi.h>
#include <HTTPClient.h>
#include <base64.h>
#include "esp_camera.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include "ESP_I2S.h"
#include "wav_header.h"
#include <WiFiClientSecure.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ===== WIFI CONFIG ======
const char* ssid = "WiFi-";
const char* password = "";

// ===== LLM CONFIG ======
String groqApiKey = "";
String  geminiApiKey = "";  

// ===== ALTERNATING VLMs ======
bool vlmModel = true;

// ===== AUDIO CONFIG ======
#define SAMPLE_RATE     (16000)
#define DATA_PIN        (GPIO_NUM_39)
#define CLOCK_PIN       (GPIO_NUM_38)
bool isRecording = false;
std::vector<uint8_t> audioBuffer;
I2SClass i2s;

// ===== CAMERA CONFIG ======
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM        15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// ===== BLE CONFIG ======
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcd1234-abcd-1234-abcd-1234567890ab"
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;

// ===== OBJECT SEARCHING CONFIG ======
TaskHandle_t objectSearchTaskHandle = NULL;
bool cancelObjectSearch = false;
String objectToSearch = "";

// ===== APP COMMAND ======
String receivedCommand = ""; 

HTTPClient sharedHttpClient;

/*
****************************************************************
*******    CAMERA INITIALISATION
****************************************************************
*/
void initCamera(){
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 16;
  config.pin_d1 = 18;
  config.pin_d2 = 21;
  config.pin_d3 = 17;
  config.pin_d4 = 14;
  config.pin_d5 = 7;
  config.pin_d6 = 6;
  config.pin_d7 = 4;
  config.pin_xclk = 5;
  config.pin_pclk = 15;
  config.pin_vsync = 1;
  config.pin_href = 2;
  config.pin_sccb_sda = 8;
  config.pin_sccb_scl = 9;
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QVGA; 
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 6;
  config.fb_count = 1;

  if (psramFound()) {
    config.jpeg_quality = 6;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed with error 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 2);
    s->set_saturation(s, -2);
  }
}

/*
****************************************************************
*******    SETUP
****************************************************************
*/
void setup() {
  //--------------------- Initialise Camera
  Serial.begin(115200);
  delay(1000);
  initCamera();

  //--------------------- Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");

  //--------------------- Initialise BLE
  BLEDevice::init("ESP32S3_BLE");

  pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |  // <-- REQUIRED to receive commands from app buttons
    BLECharacteristic::PROPERTY_NOTIFY
  );

  // Command callback class
  class CommandCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) override {
      std::string rx = std::string(pChar->getValue().c_str());
      if (!rx.empty()) {
        String rxValue = String(rx.c_str());
        Serial.println("BLE received: " + rxValue);
        receivedCommand = rxValue;
      }
    }
  };

  pCharacteristic->setCallbacks(new CommandCallback());
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
  Serial.println("BLE Server started");
  // Request higher MTU (maximum is 517 bytes)
  BLEDevice::setMTU(517);

}


/*
****************************************************************
*******    BLE SEND TO PHONE
****************************************************************
*/
void sendLongStringOverBLE(const String &msg) {
  const size_t chunkSize = 20;  // BLE max per packet (can increase with negotiated MTU)
  size_t len = msg.length();
  for (size_t i = 0; i < len; i += chunkSize) {
    String chunk = msg.substring(i, i + chunkSize);
    pCharacteristic->setValue(chunk.c_str());
    pCharacteristic->notify();
    Serial.println("Sent chunk: " + chunk);
    delay(50); // give client time to process
  }
}



/*
****************************************************************
*******    IMAGE CAPTURE
****************************************************************
*/
camera_fb_t* captureFinalFrameBuffer(int totalCaptures = 10) {
  camera_fb_t* fb = nullptr;

  for (int i = 0; i < totalCaptures; i++) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("❌ Failed to capture image");
      return nullptr;
    }

    Serial.println("📸 Captured photo " + String(i + 1));
    if (i < totalCaptures - 1) {
      esp_camera_fb_return(fb);  // Release early frames
      fb = nullptr;
    }
  }

  return fb;  
}



/*
****************************************************************
*******    GEMINI
****************************************************************
*/
String askGeminiWithSearch(const String& prompt) {
  
  String endpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + geminiApiKey;
  sharedHttpClient.begin(endpoint);
  sharedHttpClient.addHeader("Content-Type", "application/json");
  sharedHttpClient.setTimeout(5000);

  camera_fb_t* fb = captureFinalFrameBuffer();

  if (!fb) {
    Serial.println("Failed to capture image");
    return "";
  }

  Serial.println("Image captured, encoding to base64...");
  // Encode image to base64
  String base64Image = base64::encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);  // Clean up

  // JSON body
  String body = "{";
  body += "\"contents\": [{\"parts\": [";
  // Text prompt
  body += "{ \"text\": \"" + prompt + ", use the image if required \" },";
  // Image part
  body += "{";
  body += "\"inline_data\": {";
  body += "\"mime_type\": \"image/jpeg\",";
  body += "\"data\": \"" + base64Image + "\"}}]}],";
  body += "\"tools\": [{ \"google_search\": {} }]}";

  Serial.println("Sending request to Gemini API...");
  int httpCode = sharedHttpClient.POST(body);

  if (httpCode > 0) {
    Serial.printf("HTTP %d\n", httpCode);
    String response = sharedHttpClient.getString();
    Serial.println("Response: ");
    Serial.println(response);

    DynamicJsonDocument doc(8192); // Adjust size as needed
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.print(F("JSON parse error: "));
      Serial.println(error.f_str());
      sharedHttpClient.end();
      return "";
    }
    // Extract text from response
    const char* reply = doc["candidates"][0]["content"]["parts"][0]["text"];
    sharedHttpClient.end();
    return reply ? String(reply) : "No text found";
  } else {
    Serial.printf("HTTP error: %s\n", sharedHttpClient.errorToString(httpCode).c_str());
    sharedHttpClient.end();
    return "";
  }
}

/*
****************************************************************
*******    GROQ
****************************************************************
*/
String send_to_groq(String model, const String& input_message, bool image_required) {
  String payload = "";

  if (image_required) {
    // Capture image
    camera_fb_t* fb = captureFinalFrameBuffer();
    if (!fb) {
      Serial.println("Failed to capture image");
      return "";
    }

    Serial.println("Image captured, encoding to base64...");
    // Encode image to base64
    String base64Image = base64::encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    // Build JSON payload
    payload = "{\"model\":\"" + model + "\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"" + input_message + ", in less than 50 words\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64," + base64Image + "\"}}]}],\"temperature\":1,\"max_completion_tokens\":1024,\"top_p\":1,\"stream\":false}";
  } else {
    payload = "{\"model\":\"" + model + "\",\"messages\":[{\"role\":\"user\",\"content\":\"" + input_message + "\"}]}";
  }

  // Send to Groq
  sharedHttpClient.begin("https://api.groq.com/openai/v1/chat/completions");
  sharedHttpClient.setTimeout(10000);
  sharedHttpClient.addHeader("Content-Type", "application/json");
  sharedHttpClient.addHeader("Authorization", "Bearer " + String(groqApiKey));
  Serial.println("🚀 Sending request to Groq...");
  int httpCode = sharedHttpClient.POST(payload);

  if (httpCode > 0) {
    Serial.printf("HTTP %d\n", httpCode);
    String response = sharedHttpClient.getString();
    DynamicJsonDocument doc(4096);
    // Parse the JSON string
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.print(F("JSON deserialization failed: "));
      Serial.println(error.f_str());
      return "";
    }
    // Extract and print the content field
    const char* content = doc["choices"][0]["message"]["content"];
    Serial.println("✅ Assistant Response:");
    Serial.println(content);
    return String(content);
  } else {
    Serial.printf("HTTP error: %s\n", sharedHttpClient.errorToString(httpCode).c_str());
  }

  sharedHttpClient.end();
}


/*
****************************************************************
*******    WHISPER
****************************************************************
*/
String sendToWhisper(uint8_t* wav_buffer, size_t wav_size) {
  sharedHttpClient.begin("https://api.groq.com/openai/v1/audio/transcriptions");
  sharedHttpClient.addHeader("Authorization", "Bearer " + String(groqApiKey));
  sharedHttpClient.setTimeout(5000);

  String boundary = "----ESP32FormBoundary7MA4YWxkTrZu0gW";
  String formStart = "--" + boundary + "\r\n";
  formStart += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  formStart += "Content-Type: audio/wav\r\n\r\n";
  String formMiddle = "\r\n--" + boundary + "\r\n";
  formMiddle += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  formMiddle += "whisper-large-v3-turbo\r\n";
  String formEnd = "--" + boundary + "--\r\n";

  uint8_t* fullPayload = (uint8_t*)malloc(formStart.length() + wav_size + formMiddle.length() + formEnd.length());
  size_t offset = 0;
  memcpy(fullPayload + offset, formStart.c_str(), formStart.length()); offset += formStart.length();
  memcpy(fullPayload + offset, wav_buffer, wav_size); offset += wav_size;
  memcpy(fullPayload + offset, formMiddle.c_str(), formMiddle.length()); offset += formMiddle.length();
  memcpy(fullPayload + offset, formEnd.c_str(), formEnd.length()); offset += formEnd.length();

  sharedHttpClient.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  int httpCode = sharedHttpClient.POST(fullPayload, offset);
  free(fullPayload);

  if (httpCode > 0 && httpCode == 200) {
    String response = sharedHttpClient.getString();
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, response) == DeserializationError::Ok) {
      return String((const char*)doc["text"]);
    }
  }
  sharedHttpClient.end();
  return "Failed to transcribe";
}

/*
****************************************************************
*******    START AUDIO RECORDING
****************************************************************
*/
void startRecording() {
  audioBuffer.clear();
  i2s.setPinsPdmRx(CLOCK_PIN, DATA_PIN);
  if (!i2s.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Failed to initialize I2S PDM RX");
    return;
  }
  isRecording = true;
}

/*
****************************************************************
*******    STOP AUDIO RECORDING
****************************************************************
*/
String stopRecordingAndSend(bool sending) {
  isRecording = false;
  i2s.end();

  if (!sending) {
    Serial.println("Cancelled");
    return "";
  }
  if (audioBuffer.size() == 0) {
    Serial.println("No audio data captured.");
    return "";
  }

  Serial.println("Sending audio to Whisper...");

  // Create WAV header
  uint32_t totalAudioLen = audioBuffer.size();
  uint32_t totalDataLen = totalAudioLen + 36;
  uint32_t byteRate = SAMPLE_RATE * 2;
  uint8_t wavHeader[44];

  memcpy(wavHeader, "RIFF", 4);
  *(uint32_t*)(wavHeader + 4) = totalDataLen;
  memcpy(wavHeader + 8, "WAVEfmt ", 8);
  *(uint32_t*)(wavHeader + 16) = 16;
  *(uint16_t*)(wavHeader + 20) = 1;
  *(uint16_t*)(wavHeader + 22) = 1;
  *(uint32_t*)(wavHeader + 24) = SAMPLE_RATE;
  *(uint32_t*)(wavHeader + 28) = byteRate;
  *(uint16_t*)(wavHeader + 32) = 2;
  *(uint16_t*)(wavHeader + 34) = 16;
  memcpy(wavHeader + 36, "data", 4);
  *(uint32_t*)(wavHeader + 40) = totalAudioLen;

  // Allocate final WAV buffer
  size_t wav_size = totalAudioLen + 44;
  uint8_t* wav_buffer = (uint8_t*)malloc(wav_size);
  memcpy(wav_buffer, wavHeader, 44);
  memcpy(wav_buffer + 44, audioBuffer.data(), totalAudioLen);

  // Call upload logic
  String result = sendToWhisper(wav_buffer, wav_size);
  Serial.println("Transcription:");
  Serial.println(result);
  free(wav_buffer);
  return result;
  
}


/*
****************************************************************
*******    TASK CLASSIFICATION
****************************************************************
*/
String task_classification(String voice_input){
  String input_message = "Classify the following user input into one of these categories by providing only the corresponding number.";
  input_message += "Do not include any additional text or explanation. User Input: " + voice_input + " Categories:";
  input_message += "1. **Live Task Execution:** The user wants an action performed immediately based on a detected event or condition. (e.g., Tell me when you see a cat, Notify me if the light turns red. Can you tell me when you see a tree)";
  input_message += "2. **Image Analysis/Question:** The user is requesting a description of an image, or asking a question about its content. (e.g., Describe what's in front of me, Is there a laptop in this picture?, Whats in front of me?, Can you see a tree?)";
  input_message += "3. **General Conversation/Information Retrieval:** The user is engaging in a general conversation or asking a factual question. (e.g., What's the weather like?,Tell me a joke,Who is the prime minister?)";
  input_message += "4. **Image Reading:** The user wants something read or some information from text in the image. (e.g., Can you read this sign?, Does this have gluten?, What are the ingredients in this?, How much salt is in this?)";
  input_message += "5. **Help:** The user wants an explanation of the functions available (e.g., Help)";
  
  String task = send_to_groq("llama-3.3-70b-versatile", input_message, false);

  return task;
}


/*
****************************************************************
*******    OBJECT SEARCHING
****************************************************************
*/
void objectSearchTask(void* parameter) {
  String objectName = objectToSearch; // Copy the global variable
  
  Serial.println("Object search task started for: " + objectName);
  
  bool found = false;
  int attempts = 0;
  const int maxAttempts = 50; // Reduced max attempts

  while (!found && !cancelObjectSearch && attempts < maxAttempts) {
    attempts++;
    Serial.println("Search attempt " + String(attempts));
    
    // Add watchdog reset to prevent reboot
    yield();

    // Simple model selection
    String model = vlmModel ? "meta-llama/llama-4-maverick-17b-128e-instruct" : "meta-llama/llama-4-scout-17b-16e-instruct";
    vlmModel = !vlmModel;

    // Shorter prompt to reduce payload size
    String prompt = "Is there a " + objectName + " in this image? Answer yes or no only,  if yes only tell me where it is using clockface coordinates using only between 10 and 2 o'clock with 10 o'clock being leftmost and 2 o'clock being rightmost and its relative position, give distance estimate in metres";
    String response = send_to_groq(model, prompt, true);

    if (response.indexOf("yes") != -1 || response.indexOf("Yes") != -1) {
      found = true;
      Serial.println("Object found!");
      sendLongStringOverBLE("Found " + objectName + ". " + response + "<EOM>");
    } else {
      Serial.println("Object not found, retrying...");
    }

    delay(10000); // Wait before next attempt
    yield(); // Allow other tasks to run
  }

  if (attempts >= maxAttempts) {
    Serial.println("Max attempts reached");
    sendLongStringOverBLE("Search timeout<EOM>");
  }

  Serial.println("Object search task ending");
  objectSearchTaskHandle = NULL;
  vTaskDelete(NULL);
}

void performTask(String task_selected, String transcription) {
  if (task_selected == "1") {    
    // Extract object from transcription
    String input_prompt = "What object should I search for in: " + transcription + "? Reply with just the object name.";
    String objectName = send_to_groq("llama-3.3-70b-versatile", input_prompt, false);
    objectName.trim();
    
    if (objectName.length() > 2 && objectName.indexOf("no object") == -1) {
      Serial.println("Will search for: " + objectName);
      
      // Check if we already have a task running
      if (objectSearchTaskHandle != NULL) {
        Serial.println("Updating existing search to: " + objectName);
        objectToSearch = objectName;  // Just update the search target
        cancelObjectSearch = false;   // Make sure it's not cancelled
        sendLongStringOverBLE("Updated search to: " + objectName + "<EOM>");
      } else {
        // Create new task
        objectToSearch = objectName;
        cancelObjectSearch = false;
        
        BaseType_t result = xTaskCreatePinnedToCore(
          objectSearchTask,
          "ObjSearch",
          8192,
          NULL,
          1,
          &objectSearchTaskHandle,
          0
        );
        
        if (result == pdPASS) {
          Serial.println("Task created");
          sendLongStringOverBLE("Searching for: " + objectName + "<EOM>");
        } else {
          Serial.println("Task creation failed");
          sendLongStringOverBLE("Search failed to start<EOM>");
        }
      }
    } else {
      Serial.println("No valid object found");
      sendLongStringOverBLE("No object to search for<EOM>");
    }
  }
  else if (task_selected == "2" || task_selected == "4") {
    String model = vlmModel ? "meta-llama/llama-4-maverick-17b-128e-instruct" : "meta-llama/llama-4-scout-17b-16e-instruct";
    vlmModel = !vlmModel;
    String content = send_to_groq(model, transcription, true);
    sendLongStringOverBLE(content + "<EOM>");
  } else if (task_selected == "3") {
    String content = askGeminiWithSearch(transcription);
    sendLongStringOverBLE(content + "<EOM>");
  }
}



/*
****************************************************************
*******    LOOP
****************************************************************
*/
void loop() {

  /*
  ****************************************************************
  *******    RECEIVED FROM SERIAL
  ****************************************************************
  */
  if (Serial.available()) {

    char received = Serial.read();

    if (received == '1' && !isRecording) {
      Serial.println(" Starting streaming-like recording (press 's' to stop)");
      startRecording();
    } else if (received == 's' && isRecording) {
      Serial.println("Stopping recording");
      String transcription = stopRecordingAndSend(true);
      
      if (transcription.length() == 0) {
        Serial.println("No transcription received");
        return;
      }
      
      String task_selected = task_classification(transcription);
      task_selected.trim();
      Serial.println("Selected task: " + task_selected);
      performTask(task_selected, transcription);

    } else if (received == 'c') {
      if (objectSearchTaskHandle != NULL) {
        cancelObjectSearch = true;
        Serial.println("Cancelling object search...");
        sendLongStringOverBLE("Object search cancelled<EOM>");
        stopRecordingAndSend(false);
      } else {
        Serial.println("No active search to cancel");
        stopRecordingAndSend(false);
      }
    }
  }



  if (isRecording) {
    size_t chunkSize;
    uint8_t* chunk = i2s.recordWAV(1, &chunkSize);
    if (chunk && chunkSize > 0) {
      audioBuffer.insert(audioBuffer.end(), chunk, chunk + chunkSize);
      Serial.print("Recorded chunk: ");
      Serial.println(chunkSize);
      free(chunk);
    } else {
      Serial.println("Failed to record chunk");
    }
  }



  /*
  ****************************************************************
  *******    RECIEVED BLE COMMAND
  ****************************************************************
  */
  if (receivedCommand.length() > 0) {
    String command = receivedCommand;
    receivedCommand = "";  

    command.trim();
    command.toLowerCase();

    if (command == "start" && !isRecording) {
      Serial.println("Starting streaming-like recording (press 's' to stop)");
      startRecording();
    } else if (command == "s" && isRecording) {
      Serial.println("Stopping recording");
      String transcription = stopRecordingAndSend(true);
      
      if (transcription.length() == 0) {
        Serial.println("No transcription received");
        return;
      }
      
      String task_selected = task_classification(transcription);
      task_selected.trim();
      Serial.println("Selected task: " + task_selected);

      performTask(task_selected, transcription);
      
    } else if (command == "cancel") {
      if (objectSearchTaskHandle != NULL) {
        cancelObjectSearch = true;
        Serial.println("Cancelling object search...");
        sendLongStringOverBLE("Object search cancelled<EOM>");
        stopRecordingAndSend(false);
      } else {
        stopRecordingAndSend(false);
        Serial.println("No active search to cancel");
      }
    }
  }
}



