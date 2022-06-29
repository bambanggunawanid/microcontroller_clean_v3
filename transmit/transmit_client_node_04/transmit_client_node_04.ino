  // Node04 (client) need transmit to server for send data to master
#include <RF24.h>
#include <RF24Network.h>
#include <SPI.h>
#include <HX711_ADC.h>
#include <EEPROM.h>

// Definisi RF24 dan RF24Network
RF24 radio(5, 10); // nRF24L01 (CE,CSN)
RF24Network network(radio);

// Alamat node yang dibutuhkan untuk komunikasi dalam format octal
const uint16_t this_node = 04;
const uint16_t node_server_01 = 01;
const uint16_t node_server_02 = 02;
const uint16_t master00 = 00;

// Variabel penyimpanan data
// variabel ini 3 karena butuh 1 index lagi yaitu feedback untuk komunikasi antar node saja. Akan tetapi untuk kirim ke master cukup 2 index saja
uint16_t Data[3] = {0, 0, 0}; // weight, address, feedback

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
float calibration_factor = 63572.00; // nilai kesalahan kalibrasi
unsigned long t = 0;
uint16_t gram;                       // nilai gram untuk menyimpan nilai berat

// Interval untuk millis()
unsigned long previous_millis;

// Variabel bantuan
uint16_t check_data_deviation = 0;    // variabel untuk menyimpan selisih data
uint16_t data_before = 0;             // variabel untuk menyimpan data load cell sebelumnya
int sit_timer = 0;                    // variabel untuk menyimpan waktu timer sit pengunjung
int stand_up_timer = 0;               // variabel untuk menyimpan waktu timer sit pengunjung
int interval = 0;
int death = 0;                        // buat lock reset berat
int failed_send_to_branch_stack = 0;  // variabel untuk menyimpan jumlah percobaan gagal pengiriman ke node_server
bool stat_send_data = false;          // untuk mengaktifkan/nonaktifkan pengiriman data
bool stat_just_one_time_send = false; // untuk mengaktifkan/nonaktifkan pengiriman data hanya sekali

void setup()
{
    Serial.begin(115200);
    Serial.println("------- Load Cell Begin -------");
    LoadCell.begin();
    delay(10);
    unsigned long stabilizing_time = 5000;
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
    RF24NetworkHeader headerToServer_2(node_server_02);

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
        interval++;
        if (Data[weight] >= 10) // jika berat >= 10kg
        {
            // untuk menghindari bug jumlah pengunjung bisa bertambah terus menerus jika pengunjung duduk dan berdiri terus menerus. Maka lama duduk harus di timer
            sit_timer++;
            stand_up_timer = 0;
        }
        else
        {
            sit_timer = 0;
            stand_up_timer++; // jika berat < 10kg, jalankan timer stand up
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
            // Kondisi pengiriman sekali kirim diaktifkan
            stat_just_one_time_send = true;
            death = 0;
        }
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
    if(interval <= 5){
        stat_just_one_time_send = true;
    }
    //  Proses sekali kirim data
    if (stat_just_one_time_send)
    {
        // Inisialisasi data alamat this_node
        Data[address] = this_node; // inisialisasi alamatnya dengan node sendiri
        Data[feedback] = 1;        // 1 artinya tolong kirimin ke server
        // kirim array Data ke node_server_01 dan assign returnnya ke variabel boolean ok
        bool ok = network.write(headerToServer_2, &Data, sizeof(Data));
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
//        Serial.print("Status\t: ");
//        Serial.println(ok);
        Serial.print("node_");
        Serial.print(Data[address]);
        Serial.print("->node_");
        Serial.print(node_server_02);
        Serial.println("->sink_node");
        Serial.print("Weight: ");
        Serial.println(Data[weight]);
//        Serial.print("Destination\t: ");
//        Serial.println(master00);
        stat_just_one_time_send = false;
    }

    // No response at all
    if (failed_send_to_branch_stack > 10)
    {
        Serial.println("---------RESET PROGRAM---------");
        failed_send_to_branch_stack = 0;
    }
}
