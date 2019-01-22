#include <WiFi.h>
#include <Web3.h>
#include <Util.h>
#include <Contract.h>
#include <WebSocketsClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "cJSON/cJSON.h"

#define MY_ADDRESS "0xBF8C48A620bacc46907f9B89732D25E47A2D7Cf7"
#define RPC_HOST "api-testnet.dexscan.app"
#define RPC_PATH "/v1/network/rpc"
#define DEXSCAN_TX "https://testnet.dexscan.app/transaction/"
#define CONTRACT_BYTECODE "608060405234801561001057600080fd5b506040516020806102c18339810180604052602081101561003057600080fd5b8101908080519060200190929190505050806000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050610230806100916000396000f3fe608060405260043610610051576000357c01000000000000000000000000000000000000000000000000000000009004806329c15b71146100565780638da5cb5b14610093578063b70c5e75146100ea575b600080fd5b34801561006257600080fd5b506100916004803603602081101561007957600080fd5b81019080803515159060200190929190505050610119565b005b34801561009f57600080fd5b506100a86101cc565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b3480156100f657600080fd5b506100ff6101f1565b604051808215151515815260200191505060405180910390f35b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff1614151561017457600080fd5b80600060146101000a81548160ff0219169083151502179055507fb87f936dd029fdc50266e531c100fb6f7e2b1f385e524eb8a736753539fc25e881604051808215151515815260200191505060405180910390a150565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600060149054906101000a900460ff168156fea165627a7a72305820dbabb9ae82b801b172e87e23029ddd8e141dae86ef80989dc133d77da6bb987c0029"

// BLE
#define SERVICE_UUID                              "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define OWNER_CHARACTERISTIC_UUID                 "f925981a-7be4-45af-9b61-23979364a0e2"
#define CONTRACT_ADDRESSS_CHARACTERISTIC_UUID     "25f656a8-89db-4d94-a5d3-1da16c10fa3e"

#define PRIVATE_KEY "FA30B47A7A3D5AB6935D873FFAEB8CA5B9782D102C4094BE6DA6B7F2FC04B5BD" //32 Byte Private key

#define LED_PIN 5

int wificounter = 0;

WebSocketsClient webSocket;
Web3 web3(RPC_HOST, RPC_PATH);

enum State {
  STATE_INIT = 0,
  STATE_WIFI_CONNECTED,
  STATE_CONTRACT_DEPLOYED,
  STATE_WS_CONNECTED,
  STATE_LOGS_SUBSCRIBED,
};

State state;

std::string contractAddress;
std::string ownerAddress;

BLECharacteristic *pContractAddressCharacteristic;

void on_websocket_event(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      {
        Serial.println("Websocket connected.");

        string req = std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_subscribe\",\"params\":[\"logs\",{\"address\":\"") +
          contractAddress +
          std::string("\",\"topics\":[\"0xb87f936dd029fdc50266e531c100fb6f7e2b1f385e524eb8a736753539fc25e8\"]}]}");

        webSocket.sendTXT(req.c_str());
        break;
      }
    case WStype_DISCONNECTED:
      Serial.println("Websocket disconnected.");
      break;
    case WStype_TEXT:
      {
        int status;
        if (state < STATE_LOGS_SUBSCRIBED) {
          Serial.println((const char*) payload);
          state = STATE_LOGS_SUBSCRIBED;
          break;
        }
        Serial.println((const char*)payload);
        cJSON *root = NULL, *params = NULL, *result = NULL, *data = NULL;
        root = cJSON_Parse((const char*)payload);
        if (root == NULL) {
          goto cleanup;
        }
        params = cJSON_GetObjectItem(root, "params");
        if (params == NULL) {
          Serial.println("get params error");
          goto cleanup;
        }
        result = cJSON_GetObjectItem(params, "result");
        if (result == NULL) {
          Serial.println("get data error");
          goto cleanup;
        }
        data = cJSON_GetObjectItem(result, "data");
        if (data == NULL) {
          Serial.println("get data error");
          goto cleanup;
        }
        status = strtol(data->valuestring, nullptr, 16);
        Serial.printf("Status: %d\n", status);
        digitalWrite(LED_PIN, status);
cleanup:
        if (root != NULL)
          cJSON_free(root);
        if (params != NULL)
          cJSON_free(params);
        if (result != NULL)
          cJSON_free(result);
        if (data != NULL)
          cJSON_free(data);
        break;
      }
    default:
      Serial.printf("unhandled event: %d\n", type);
  }
}

std::string deploy_contract(std::string owner) {
  Contract contract(&web3, "");
  contract.SetPrivateKey(PRIVATE_KEY);

  std::string address = MY_ADDRESS;
  uint32_t nonceVal = (uint32_t)web3.EthGetTransactionCount(&address);
  unsigned long long gasPriceVal = 20000000000ULL;
  uint32_t  gasLimitVal = 5000000;
  std::string value;
  std::string emptyString;

  value = Util::ConvertEthToWei(1);
  std::string result = contract.SendTransaction(nonceVal, gasPriceVal, gasLimitVal, &ownerAddress, &value, &emptyString);
  Serial.println(result.c_str());

  std::string data = string(CONTRACT_BYTECODE) + "000000000000000000000000" + owner.substr(2);
  value = "0";
  result = contract.SendTransaction(nonceVal + 1, gasPriceVal, gasLimitVal, &emptyString, &value, &data);
  Serial.println(result.c_str());

  string transactionHash = web3.getString(&result);
  Serial.printf("TxHash: %s\n", transactionHash.c_str());

  for (int i = 0; i < 7; i++) {
    string contractAddress = web3.EthGetDeployedContractAddress(&transactionHash);
    if (contractAddress.length() != 0)
      return contractAddress;
    delay(1000);
  }
  return "";
}

void post_wifi_connect() {
  if (ownerAddress.length() == 0) {
    return;
  }
  Serial.printf("Deploying contract, setting owner to %s\n", ownerAddress.c_str());
  contractAddress = deploy_contract(ownerAddress);
  if (contractAddress.length() == 0) {
    Serial.println("Contract deployment failed.");
    return;
  }
  Serial.printf("Contract deployed: %s\n", contractAddress.c_str());

  pContractAddressCharacteristic->setValue(contractAddress.c_str());
  state = STATE_CONTRACT_DEPLOYED;
}

void post_contract_deployment() {
  Contract contract(&web3, contractAddress.c_str());
  string param = contract.SetupContractData("powered()");
  string result = contract.ViewCall(&param);
  digitalWrite(LED_PIN, web3.getInt(&result));

  webSocket.begin("testnet.dexon.org", 8546);
  webSocket.onEvent(on_websocket_event);
  state = STATE_WS_CONNECTED;
}

void setup_wifi(const char* ssid, const char* password) {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  if (WiFi.status() != WL_CONNECTED)  {
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);

    WiFi.begin(ssid, password);
  }

  wificounter = 0;
  while (WiFi.status() != WL_CONNECTED && wificounter < 10)  {
    delay(500);
    Serial.print(".");
    wificounter++;
  }

  if (wificounter >= 10) {
    Serial.println("Can not connect");
    return;
  }

  delay(10);

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  state = STATE_WIFI_CONNECTED;
}


class WiFiCharacteristicCallback : public BLECharacteristicCallbacks {
  virtual void onWrite(BLECharacteristic* pCharacteristic) {
    std::string wifiSettings = pCharacteristic->getValue();
    int pos = wifiSettings.find_first_of(" ");
    if (pos == std::string::npos) {
      return;
    }
    std::string ssid = wifiSettings.substr(0, pos);
    std::string password = wifiSettings.substr(pos + 1);
    Serial.println(ssid.c_str());
    Serial.println(password.c_str());
    setup_wifi(ssid.c_str(), password.c_str());
  }
};

class OwnerCharacteristicCallback : public BLECharacteristicCallbacks {
  virtual void onWrite(BLECharacteristic* pCharacteristic) {
    ownerAddress = pCharacteristic->getValue();
  }
};

void setup_bluetooth() {
  BLEDevice::init("DEXON IoT Core");

  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic* c = pService->createCharacteristic(
      OWNER_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE);
  c->setCallbacks(new OwnerCharacteristicCallback());

  pContractAddressCharacteristic = pService->createCharacteristic(
      CONTRACT_ADDRESSS_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ);

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertisement started.");
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);

  setup_wifi("COBINHOOD_Guest", "COB0921592018");
  setup_bluetooth();
  //ownerAddress = "0x32528352352B73fAE48AbB05945EA457797D8bDC";
}

void loop() {
  switch (state) {
    case STATE_INIT:
      return;
    case STATE_WIFI_CONNECTED:
      post_wifi_connect();
      return;
    case STATE_CONTRACT_DEPLOYED:
      post_contract_deployment();
      return;
    case STATE_WS_CONNECTED:
    case STATE_LOGS_SUBSCRIBED:
    default:
      webSocket.loop();
  }
}
