// Node01 (server) to serve from branch node and send to master
#include <RF24.h>
#include <RF24Network.h>
#include <SPI.h>
#include <HX711_ADC.h>
#include <EEPROM.h>

// Deklarasi fungsi
void ReadSendClientMultiHop(uint16_t node_client, RF24NetworkHeader headerToSend);

// Definisi RF24 dan RF24Network
RF24 radio(5, 10); // nRF24L01 (CE,CSN)
RF24Network network(radio);

// Alamat node yang dibutuhkan untuk komunikasi dalam format octal
const uint16_t this_node = 01;
const uint16_t master00 = 00;
const uint16_t node_client_03 = 03;
const uint16_t node_client_04 = 04;

// Variabel penyimpanan data
// Variabel ini dua karena master cuma terima array 2 index saja
uint16_t Data[2] = {0, 0}; // weight, address
// Variabel CopyFromClient juga 2 saja karena master hanya menerima array 2 index
uint16_t CopyFromClient[2] = {0, 0};  // weight, address
uint16_t incomingData[3] = {0, 0, 0}; // weight, address, feedback

// Index dari masing-masing array
const byte weight = 0;   // index untuk berat
const byte address = 1;  // index untuk alamat
const byte feedback = 2; // index untuk feedback

// Variabel pin load cell menggunakan pin 3 (DOUT) dan pin 4 (SCK)
const byte HX711_dout = 2;
const byte HX711_sck = 3;
HX711_ADC LoadCell(HX711_dout, HX711_sck);

// Variabel penyimpanan data kalibrasi
const int calVal_eepromAdress = 0; // alamat eeprom untuk menyimpan nilai kalibrasi
unsigned long t = 0;
float calibration_factor = 40594.66; // nilai kesalahan kalibrasi
uint16_t gram;

// Interval untuk millis()
unsigned long previous_millis;

// Variabel bantuan
uint16_t check_data_deviation = 0;    // variabel untuk menyimpan selisih data
uint16_t data_before = 0;             // variabel untuk menyimpan data load cell sebelumnya
int sit_timer = 0;                    // variabel untuk menyimpan waktu timer sit pengunjung
int stand_up_timer = 0;               // variabel untuk menyimpan waktu timer sit pengunjung
int death = 0;                        // buat lock reset berat
int failed_send_to_master_stack = 0;
int failed_send_to_branch_stack = 0;  // variabel untuk menyimpan jumlah percobaan gagal pengiriman ke master
bool stat_just_one_time_send = false; // untuk mengaktifkan/nonaktifkan pengiriman data hanya sekali

void setup()
{
    Serial.begin(115200);
    Serial.println("\n------- Load Cell Begin -------");
    LoadCell.begin();
    delay(10);
    unsigned long stabilizing_time = 2000;
    bool _tare = true;
    LoadCell.start(stabilizing_time, _tare);
    if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag())
    {
        Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
        while (1)
            ;
    }
    else
    {
        LoadCell.setCalFactor(calibration_factor);
        Serial.println("Startup is complete");
    }
    while (!LoadCell.update())
        ;
    // RADIO SETUP
    Serial.println("------- Radio & Network Begin -------");
    if (!radio.begin())
    {
        Serial.println(F("radio hardware is not responding!!"));
        while (1)
            ; // ditahan menggunakan infinite loop hingga NRF terkoneksi dengan baik
    }
    SPI.begin();
    radio.begin();
    radio.enableAckPayload();
//    radio.setDataRate(RF24_250KBPS);
    // radio.setDataRate(RF24_1MBPS);
     radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MIN);
    // radio.setPALevel(RF24_PA_MAX);
    network.begin(90, this_node); //(channel, node address)
}

void loop()
{
    // untuk memperbarui network NRF24L01, agar dapat menerima data dari node lain
    network.update();
    RF24NetworkHeader headerToSend(master00);

    // Baca apapun pesan yang baru masuk dari node client karena node server memiliki peran ganda sebagai transit node_client
    if (network.available())
    {
        Serial.println("---------READ MESSAGE FROM NODE CLIENT---------");
        // Menggunakan header untuk membaca data dari node_client, header tanpa alamat digunakan untuk membaca semua alamat header yang masuk
        RF24NetworkHeader header;
        // Membaca data dari node client dan datanya disimpan ke variabel array incomingData
        network.read(header, &incomingData, sizeof(incomingData));
        // Jangan ambil data weight == 0, merupakan noise
        if (header.from_node == node_client_03)
        {
            ReadSendClientMultiHop(node_client_03, headerToSend);
        }
    }

    // mendapatkan nilai berat dari load cell
    static bool newDataReady = 0;
    const int serialPrintInterval = 100;

    // check for new data/start next conversion:
    if (LoadCell.update())
        newDataReady = true;

    // get smoothed value from the dataset:
    if (newDataReady)
    {
        if (millis() > t + serialPrintInterval)
        {
            gram = LoadCell.getData();
            gram = abs(gram);
            newDataReady = 0;
            t = millis();
        }
    }
    if (millis() - previous_millis >= 1000) // hanya akan masuk kondisi ini jika sistem sudah berjalan selama 1 detik
    {
        // untuk menyimpan nilai millis sebelumnya, alhasil sistem selalu masuk di kelipatan 1000ms (1 detik), seperti delay buatan
        previous_millis = millis();
        if (Data[weight] >= 10) // jika berat >= 10kg
        {
            sit_timer++; // untuk menghindari bug jumlah pengunjung bisa bertambah terus menerus jika pengunjung duduk dan berdiri terus menerus. Maka lama duduk harus di timer
            stand_up_timer = 0;
        }
        else
        {
            sit_timer = 0;    // jika berat < 10kg, jalankan timer stand up
            stand_up_timer++; // masuk else, brarti pengunjung berdiri disini dapat dimanfaatkan untuk melakukan reset pengiriman berat = 0
        }
    }

    // Inisialisasi data pembacaan load cell ke variabel Data index weight
    Data[weight] = gram;

    // Jika orang yang duduk sudah lebih dari 5 detik maka akan dikirimkan data ke node server
    if (sit_timer > 5)
    {
        //  Cek data biar ngga ngirim data terus, kecuali selisihnya jauh minimal 10kg
        check_data_deviation = data_before - Data[weight];
        check_data_deviation = abs(check_data_deviation);
        data_before = Data[weight];

        //  Jika selisih sebanyak lebih dari 10kg maka proses sekali kirim di aktifkan, ini berfungsi untuk menghindari pengiriman secara terus menerus. Dengan memberikan selisih minimal 10kg disetiap pembacaaan, membuat data hanya dikirim ketika terjadi perubahan besar.
        if (check_data_deviation >= 5)
        {
            stat_just_one_time_send = true;
            death = 0;
        }
        //  Proses sekali kirim data ke master
    }
    //  Inisialisasi data before disini, supaya di cari selisihnya terelebih dahulu
    if (stand_up_timer >= 30)
    {
        stand_up_timer = 0;
        death++;
        stat_just_one_time_send = true;
    }
    if (death > 1)
    {
        stat_just_one_time_send = false;
    }
    if (stat_just_one_time_send)
    {
        // Inisialisasi data alamat this_node
        Data[address] = this_node;
        // kirim array Data ke master dan assign returnnya ke variabel boolean ok
        bool ok = network.write(headerToSend, &Data, sizeof(Data));
        // Jika data berhasil dikirim, maka
        if (!ok)
        {
            if (!radio.isAckPayloadAvailable())
            {
                Serial.print("Success ");
                failed_send_to_branch_stack = 0;
            }
            else
            {
                Serial.print("Acknowledge ");
            }
        }
        // Jika data gagal dikirim, maka
        else
        {
            Serial.print("Failed ");
        }
        //  Data langsung di stop untuk dikirim
        Serial.print("node_");
        Serial.print(Data[address]);
        Serial.println("->sink_node");
        Serial.print("Weight: ");
        Serial.println(Data[weight]);
        stat_just_one_time_send = false;
    }

    //    No response at all
    if (failed_send_to_master_stack > 10)
    {
        Serial.println("---------RESET PROGRAM---------");
        //    resetFunc();
        failed_send_to_master_stack = 0;
    }
}

// Multihop ada disini
void ReadSendClientMultiHop(uint16_t node_client, RF24NetworkHeader headerToSend)
{
    // Inisialisasi variabel ke CopyFromClient karena master hanya menerima Data dengan array index 2 saja, kalau 3 nanti error. Maka dari itu di pindahkan sementara ke CopyFromClient
    CopyFromClient[address] = incomingData[address];
    CopyFromClient[weight] = incomingData[weight];

    // Forward pesan sekaligus mengirim pesan kembali ke node client
    // Jika permintaan dari client adalah 1 maka teruskan data ke master
    if (incomingData[feedback] == 1)
    {
        // Jika data berhasil dikirim, maka
        bool ok = network.write(headerToSend, &CopyFromClient, sizeof(CopyFromClient));
        if (!ok)
        {
            if (!radio.isAckPayloadAvailable())
            {
                Serial.print("Success ");
            }
            else
            {
                Serial.print("Acknowledge ");
            }
        }
        // Jika data gagal dikirim, maka
        else
        {
            Serial.print("Failed ");
        }
        Serial.print("node_");
        Serial.print(CopyFromClient[address]);
        Serial.print("->node_");
        Serial.print(this_node);
        Serial.print("Weight: ");
        Serial.println(CopyFromClient[weight]);
    }
}
