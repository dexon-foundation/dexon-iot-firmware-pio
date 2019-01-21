#include <WiFi.h>
#include <Web3.h>
#include <Util.h>
#include <Contract.h>

const char *ssid = "COBINHOOD_Guest";
const char *password = "COB0921592018";
#define MY_ADDRESS "0xBF8C48A620bacc46907f9B89732D25E47A2D7Cf7"
#define RPC_HOST "api-testnet.dexscan.app"
#define RPC_PATH "/v1/network/rpc"
#define CONTRACT_ADDRESS "0x109dc2e0964e114f03e9ce3348912b3e925b42f2"
#define DEXSCAN_TX "https://testnet.dexscan.app/transaction/"
#define LED_PIN 5

// Copy/paste the private key from MetaMask in here
const char *PRIVATE_KEY = "FA30B47A7A3D5AB6935D873FFAEB8CA5B9782D102C4094BE6DA6B7F2FC04B5BD"; //32 Byte Private key 

int wificounter = 0;
Web3 web3(RPC_HOST, RPC_PATH);

void setup_wifi();
void PushERC20Transaction();
void sendEthToAddress(double eth, const char *destination); 
void queryERC875Balance(const char *userAddress);
double queryAccountBalance(const char *address);

void setup() {
    Serial.begin(115200);

    setup_wifi();
}


void loop() {
    Contract contract(&web3, CONTRACT_ADDRESS);

    string param = contract.SetupContractData("powered()");
    string result = contract.ViewCall(&param);
    int status = web3.getInt(&result);
    Serial.printf("Status: %d\n", status);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, status);
}


/* This routine is specifically geared for ESP32 perculiarities */
/* You may need to change the code as required */
/* It should work on 8266 as well */
void setup_wifi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return;
    }

    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.persistent(false);
        WiFi.mode(WIFI_OFF);
        WiFi.mode(WIFI_STA);

        WiFi.begin(ssid, password);
    }

    wificounter = 0;
    while (WiFi.status() != WL_CONNECTED && wificounter < 10)
    {
        for (int i = 0; i < 500; i++)
        {
            delay(1);
        }
        Serial.print(".");
        wificounter++;
    }

    if (wificounter >= 10)
    {
        Serial.println("Restarting ...");
        ESP.restart(); //targetting 8266 & Esp32 - you may need to replace this
    }

    delay(10);

    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}