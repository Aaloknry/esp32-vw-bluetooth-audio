#include <Arduino.h>
#include "BluetoothA2DPSink.h"

BluetoothA2DPSink a2dp_sink;

void audio_data_callback(const uint8_t *data, uint32_t length) {
    if (data && length > 0) {
        Serial.write(data, length);
    }
}

void setup() {
    // 2,000,000 baud (2 Mbps) hardware UART to ESP32-S3
    // Exact 80MHz integer divisor (80M / 40 = 2.000M, 0.00% error)
    Serial.setTxBufferSize(4096);
    Serial.begin(2000000);

    // Initialize Bluetooth A2DP Sink with maximum available DRAM
    a2dp_sink.set_stream_reader(audio_data_callback, false);
    a2dp_sink.start("VW-Vento-Audio");
}

void loop() {
    delay(100);
}
