#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define BUTTON1_PIN 1   // adjust to your wiring
#define BUTTON2_PIN 2   // adjust to your wiring

static bool nextIsStart = true;  // Next command will be 'start'

// BLE Config
static BLEUUID serviceUUID("12345678-1234-1234-1234-1234567890ab");
static BLEUUID charUUID("abcd1234-abcd-1234-abcd-1234567890ab");

static boolean connected = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("Connected to server");
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("Disconnected from server");
  }
};

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.haveName() && advertisedDevice.getName() == "ESP32S3_BLE") {
        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
      } else if (advertisedDevice.haveServiceUUID() && 
                 advertisedDevice.isAdvertisingService(serviceUUID)) {
        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
      }
    }
};

bool connectToServer() {
  Serial.println("Connecting to BLE Server...");
  BLEClient* pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  if (!pClient->connect(myDevice)) {
    Serial.println("Failed to connect.");
    return false;
  }

  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.println("Failed to find service UUID.");
    pClient->disconnect();
    return false;
  }

  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Failed to find characteristic UUID.");
    pClient->disconnect();
    return false;
  }

  connected = true;
  Serial.println("Connected and ready!");
  return true;
}

void sendCommand(const char* cmd) {
  if (connected && pRemoteCharacteristic->canWrite()) {
    pRemoteCharacteristic->writeValue(cmd);
    Serial.print("Sent command: ");
    Serial.println(cmd);
  } else {
    Serial.println("Not connected or characteristic not writable.");
  }
}

void goToSleep() {
  Serial.println("Going to deep sleep... Press a button to wake.");
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON1_PIN, 0); // wake on button1
  esp_sleep_enable_ext1_wakeup((1ULL << BUTTON2_PIN), ESP_EXT1_WAKEUP_ANY_HIGH); // wake on button2
  delay(100);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  BLEDevice::init("");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pScan->setInterval(1349);
  pScan->setWindow(449);
  pScan->setActiveScan(true);
  pScan->start(5, false);
}

void loop() {
  if (!connected) {
    if (myDevice != nullptr) {   // only connect if we actually found one
      if (connectToServer()) {
        Serial.println("We are now connected to the BLE Server.");
        delay(1000);
        // goToSleep();   // sleep after connecting
      } else {
        Serial.println("Failed to connect, will retry...");
        myDevice = nullptr;  // reset so we scan again
        BLEDevice::getScan()->start(5, false);
        delay(2000);
      }
    } else {
      // Keep scanning until we find the target
      BLEDevice::getScan()->start(5, false);
      delay(1000);
    }
  }

  // (shouldn’t run if sleeping, but keeps logic safe)
  if (connected) {
    if (digitalRead(BUTTON1_PIN) == LOW) {
      if (nextIsStart) {
        sendCommand("xstart");
        nextIsStart = !nextIsStart;
      } else {
        sendCommand("xstop");
        nextIsStart = !nextIsStart;
      }
      delay(500);
    }
    if (digitalRead(BUTTON2_PIN) == LOW) {
      sendCommand("xcancel");
      delay(500);
    }
  }
}
