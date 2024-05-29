#include <HX711_ADC.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>

#define SERVICE_UUID "8a39df89-82c9-47d6-b16f-86d97d1ca78b"
#define COMMAND_CHAR_UUID "86dce9e0-818b-4183-ba5c-6395e30addfe"
#define NOTIFICATION_CHAR_UUID "86dce9e0-818b-4183-ba5c-6395e30addff"

const int dout = 4;
const int sck = 5;

HX711_ADC loadCell(dout, sck);
float calibrationValue = -27.62f;

NimBLECharacteristic *commandCharacteristic;
NimBLECharacteristic *notificationCharacteristic;
bool deviceConnected = false;

StaticJsonDocument<64> doc;

class MyServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Client connected");
    };

    void onDisconnect(NimBLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Client disconnected");
      NimBLEDevice::startAdvertising();
    }
};

StaticJsonDocument<48> currentStatusJson;
String currentStatus;
float lastWeight = 0;

void updateStatus() {
  currentStatusJson["weight"] = (int)lastWeight;
  serializeJson(currentStatusJson, currentStatus);
}

class CommandCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
      String input = pCharacteristic->getValue();
      Serial.printf("< %s\r\n", input.c_str());
      if (input.length() > 0) {
        DeserializationError error = deserializeJson(doc, input);
        if (error) {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
          return;
        }

        String cmd = doc["cmd"];
        if(cmd.indexOf("read")>-1){
          
        }else if (cmd.indexOf("tare") > -1) {
          loadCell.tareNoDelay();
        } else if (cmd.indexOf("calib") > -1) {
          calibrate();
        }else{
          return;
        }

        updateStatus();
      }
    }
};

void setup() {
  Serial.begin(115200); delay(10);
  Serial.println();
  Serial.println("Starting...");

  loadCell.begin();
  //loadCell.setReverseOutput();
  loadCell.start(2000, true);
  if (loadCell.getTareTimeoutFlag() || loadCell.getSignalTimeoutFlag()) {
    Serial.println("timeout");
  }else {
    loadCell.setCalFactor(calibrationValue);
  }
  while (!loadCell.update());

  NimBLEDevice::init("PlantScale");
  NimBLEDevice::setMTU(214);
  NimBLEServer *pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  NimBLEService *pService = pServer->createService(SERVICE_UUID);
  notificationCharacteristic = pService->createCharacteristic(NOTIFICATION_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);
  commandCharacteristic = pService->createCharacteristic(COMMAND_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
  commandCharacteristic->setCallbacks(new CommandCallbacks());
  pService->start();

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->setScanResponse(true);
  pAdvertising->start();
  Serial.println("Waiting for a client connection to notify...");
}

uint32_t lastUpdateMs = 0;
void loop() {
  static boolean newDataReady = 0;
  const int serialPrintInterval = 0;

  if (loadCell.update()) newDataReady = true;

  if (newDataReady) {
    uint32_t t = 0;
    if (millis() > t + serialPrintInterval) {
      float i = loadCell.getData();
      Serial.printf("%fg\r\n", i);
      lastWeight = i;
      newDataReady = 0;
      t = millis();
    }
    if(millis() - lastUpdateMs > 5000){
      if(deviceConnected){
        updateStatus();
        Serial.printf("> %s\r\n", currentStatus.c_str());
        notificationCharacteristic->setValue(currentStatus);
        notificationCharacteristic->notify();
        lastUpdateMs = millis();
      }
    }
  }

  if (Serial.available() > 0) {
    char inByte = Serial.read();
    if (inByte == 't'){
      loadCell.tareNoDelay();
    } else if (inByte == 'c') calibrate();
  }

  if (loadCell.getTareStatus() == true) {
    Serial.println("Tare complete");
  }

}

void calibrate() {
  Serial.println("Start calibration:");
  Serial.println("Place the load cell an a level stable surface.");
  Serial.println("Remove any load applied to the load cell.");
  Serial.println("Send 't' from serial monitor to set the tare offset.");

  boolean _resume = false;
  while (_resume == false) {
    loadCell.update();
    if (Serial.available() > 0) {
      if (Serial.available() > 0) {
        char inByte = Serial.read();
        if (inByte == 't') loadCell.tareNoDelay();
      }
    }
    if (loadCell.getTareStatus() == true) {
      Serial.println("Tare complete");
      _resume = true;
    }
  }

  Serial.println("Now, place your known mass on the loadCell.");
  Serial.println("Then send the weight of this mass (i.e. 100.0) from serial monitor.");

  float known_mass = 0;
  _resume = false;
  while (_resume == false) {
    loadCell.update();
    if (Serial.available() > 0) {
      known_mass = Serial.parseFloat();
      if (known_mass != 0) {
        Serial.print("Known mass is: ");
        Serial.println(known_mass);
        _resume = true;
      }
    }
  }
  loadCell.refreshDataSet();
  float newCalibrationValue = loadCell.getNewCalibration(known_mass);
  Serial.print("New calibration value has been set to: ");
  Serial.print(newCalibrationValue);
  Serial.println(", use this as calibration value (calFactor) in your project sketch.");
}
