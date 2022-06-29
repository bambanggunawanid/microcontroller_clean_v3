#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <string>

// Konfigurasi WiFi
const char *ssid = "sawo d3 no 26";
const char *password = "b6070wal";
// IP Address Server yang terpasang XAMPP
const char *host = "192.168.31.250";

// Konfigurasi NRF
RF24 radio(2, 4);              // nRF24L01 (CE,CSN)
RF24Network network(radio);    // Include the radio in the network
const uint16_t this_node = 00; // Address of our node in Octal format
const uint16_t node_server_01 = 01;
const uint16_t node_server_02 = 02;
const uint16_t node_client_03 = 03;
const uint16_t node_client_04 = 04;

void setup()
{
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  // Jika koneksi berhasil, maka akan muncul address di serial monitor
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // inisialisasi transceiver pada bus SPI
  if (!radio.begin())
  {
    Serial.println(F("radio hardware is not responding!!"));
    while (1)
    {
    } // ditahan menggunakan infinite loop hingga NRF terkoneksi dengan baik
  }

  // Inisialisasi NRF
  SPI.begin();
  radio.begin();
//  radio.setDataRate(RF24_250KBPS);
  radio.setDataRate(RF24_2MBPS);
  radio.enableAckPayload();
  radio.setPALevel(RF24_PA_MIN);
  network.begin(90, this_node); //(channel, node address)
  // Pesan pertama jika NRF sudah terkoneksi
  Serial.println(F("RF24 GettingStarted"));
}

uint16_t datasensor = 0;
String name_chair = "";
const unsigned long interval = 10;
unsigned long last_sent;

void loop()
{
  network.update();
  //===== Receiving =====//
  while (network.available())
  { // Is there any incoming data?
    Serial.println("-----------------START-----------------");
    RF24NetworkHeader header;
    uint16_t incomingData[2];
    network.read(header, &incomingData, sizeof(incomingData)); // Read the incoming data
    if (incomingData[1] == node_server_01)
    { // If data comes from Node 01
      name_chair = "node_01";
      datasensor = incomingData[0];
      Serial.print(name_chair);
      Serial.print(incomingData[1]);
      Serial.println("->sink_node");
      Serial.print("Data : ");
      Serial.println(incomingData[0]);
    }
    if (incomingData[1] == node_server_02)
    { // If data comes from Node 02
      name_chair = "node_02";
      datasensor = incomingData[0];
      Serial.print(name_chair);
      Serial.print(incomingData[1]);
      Serial.println("->sink_node");
      Serial.print("Data : ");
      Serial.println(incomingData[0]);
    }
    if (incomingData[1] == node_client_03)
    { // If data comes from Node 03
      name_chair = "node_03";
      datasensor = incomingData[0];
      Serial.print(name_chair);
      Serial.print(incomingData[1]);
      Serial.print("->node_03");
      Serial.println("->sink_node");
      Serial.print("Data : ");
      Serial.println(incomingData[0]);
    }
    if (incomingData[1] == node_client_04)
    { // If data comes from Node 04
      name_chair = "node_04";
      datasensor = incomingData[0];
      Serial.print(name_chair);
      Serial.print(incomingData[1]);
      Serial.print("->node_04");
      Serial.println("->sink_node");
      Serial.print("Data : ");
      Serial.println(incomingData[0]);
    }

    // Membaca Sensor -------------------------------------------------------
    Serial.print("connecting to ");
    Serial.println(host);

    // Mengirimkan ke alamat host dengan port 80 -----------------------------------
    WiFiClient client;
    const int httpPort = 80;
    if (!client.connect(host, httpPort))
    {
      Serial.println("connection failed");
      return;
    }

    // Isi Konten yang dikirim adalah alamat ip si esp -----------------------------
    String url = "/cafe_chair/update_data.php?data=";
    url += datasensor;
    url += "&name_chair=";
    url += '"';
    url += name_chair;
    url += '"';

    // QUERY 1
    Serial.print("Requesting URL: ");
    Serial.println(url);

    // Mengirimkan Request ke Server -----------------------------------------------
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (client.available() == 0)
    {
      if (millis() - timeout > 1000)
      {
        Serial.println(">>> Client Timeout !");
        client.stop();
        return;
      }
    }

    // Read all the lines of the reply from server and print them to Serial
    while (client.available())
    {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }

    Serial.println("closing connection");
    Serial.println("------------------------------");
    Serial.println();
  }
}
