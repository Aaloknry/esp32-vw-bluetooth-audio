#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"
#include "chime_128k_data.h"

extern "C" {
#include "lib/layer3.h"
}

#define AUDIO_RX_PIN 18
#define AUDIO_TX_PIN 17

USBMSC MSC;

// FAT16 Geometry (2.0 GB Volume - Exactly 65,500 Clusters)
static const uint32_t SECTOR_SIZE = 512;
static const uint16_t SECTORS_PER_CLUSTER = 64;        // 32 KB per cluster
static const uint16_t RESERVED_SECTORS = 1;            // Sector 0
static const uint16_t NUM_FATS = 2;                    // FAT1 and FAT2
static const uint16_t SECTORS_PER_FAT = 256;           // 256 sectors per FAT (65,536 entries)
static const uint16_t ROOT_ENTRIES = 512;              // 512 entries
static const uint16_t ROOT_DIR_SECTORS = 32;           // 32 sectors (512 * 32 / 512)
static const uint32_t ROOT_DIR_START = RESERVED_SECTORS + (NUM_FATS * SECTORS_PER_FAT); // 513
static const uint32_t DATA_START_SECTOR = ROOT_DIR_START + ROOT_DIR_SECTORS;            // 545

// Cluster count: 65,500 (< 65,525 guarantees standard FAT16 identification across all OS/radios)
static const uint32_t DATA_CLUSTERS = 65500;
static const uint32_t TOTAL_SECTORS = DATA_START_SECTOR + (DATA_CLUSTERS * SECTORS_PER_CLUSTER); // 4,192,545 sectors
static const uint32_t LAST_CLUSTER = 2 + DATA_CLUSTERS - 1;                                     // 65501
static const uint32_t FILE_SIZE = DATA_CLUSTERS * (SECTORS_PER_CLUSTER * SECTOR_SIZE);          // 2,146,304,000 bytes (37.26 hours)

// 1 MB MP3 Ring Buffer in PSRAM (~65 seconds of 128 kbps audio)
static const uint32_t MP3_RING_SIZE = 1024 * 1024;
static uint8_t *mp3_ring = nullptr;
static uint32_t actual_rb_size = MP3_RING_SIZE;
static volatile uint32_t rb_head = 0;
static volatile uint32_t rb_tail = 0;
static volatile bool live_streaming = false;
static uint32_t last_packet_time = 0;
static uint32_t last_host_read_time = 0;
static uint32_t chime_pos = 0;

// Standard 128 kbps 44.1 kHz CBR silence frame (417 bytes)
static const uint8_t SILENCE_FRAME[417] = {
    0xFF, 0xFB, 0x90, 0x64, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00,
    0x0D, 0x20, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0xA4, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x34
};

// Shine MP3 Encoder
static shine_t shine = nullptr;
static int16_t pcm_interleaved[1152 * 2];
static uint32_t pcm_sample_count = 0;

static inline uint32_t rb_available() {
    uint32_t h = rb_head;
    uint32_t t = rb_tail;
    if (h >= t) return h - t;
    return actual_rb_size - (t - h);
}

static inline void rb_write(uint8_t byte) {
    if (!mp3_ring) return;
    uint32_t next = (rb_head + 1) % actual_rb_size;
    if (next != rb_tail) {
        mp3_ring[rb_head] = byte;
        rb_head = next;
    }
}

static inline uint8_t rb_read() {
    if (!mp3_ring || rb_head == rb_tail) return 0;
    uint8_t val = mp3_ring[rb_tail];
    rb_tail = (rb_tail + 1) % actual_rb_size;
    return val;
}

// Generate Volume Boot Record (Sector 0)
static void get_boot_sector(uint8_t *buf) {
    memset(buf, 0, SECTOR_SIZE);
    buf[0] = 0xEB; buf[1] = 0x3C; buf[2] = 0x90;
    memcpy(&buf[3], "MSDOS5.0", 8);
    // Bytes per sector (512)
    buf[11] = 0x00; buf[12] = 0x02;
    // Sectors per cluster (64 = 32KB)
    buf[13] = (uint8_t)SECTORS_PER_CLUSTER;
    // Reserved sectors (1)
    buf[14] = (uint8_t)(RESERVED_SECTORS & 0xFF);
    buf[15] = (uint8_t)(RESERVED_SECTORS >> 8);
    // Number of FATs (2)
    buf[16] = (uint8_t)NUM_FATS;
    // Root entries (512)
    buf[17] = 0x00; buf[18] = 0x02;
    // Total sectors 16-bit (0 -> use 32-bit field)
    buf[19] = 0x00; buf[20] = 0x00;
    // Media descriptor (Fixed Disk)
    buf[21] = 0xF8;
    // Sectors per FAT (256)
    buf[22] = (uint8_t)(SECTORS_PER_FAT & 0xFF);
    buf[23] = (uint8_t)(SECTORS_PER_FAT >> 8);
    // Sectors per track (32)
    buf[24] = 0x20; buf[25] = 0x00;
    // Heads (64)
    buf[26] = 0x40; buf[27] = 0x00;
    // Hidden sectors (0)
    buf[28] = 0x00; buf[29] = 0x00; buf[30] = 0x00; buf[31] = 0x00;
    // Total sectors 32-bit (4,192,545)
    buf[32] = (uint8_t)(TOTAL_SECTORS & 0xFF);
    buf[33] = (uint8_t)((TOTAL_SECTORS >> 8) & 0xFF);
    buf[34] = (uint8_t)((TOTAL_SECTORS >> 16) & 0xFF);
    buf[35] = (uint8_t)((TOTAL_SECTORS >> 24) & 0xFF);
    // Drive number
    buf[36] = 0x80;
    // Extended boot signature
    buf[38] = 0x29;
    // Volume serial number
    buf[39] = 0x20; buf[40] = 0x26; buf[41] = 0x11; buf[42] = 0x17;
    // Volume label
    memcpy(&buf[43], "VW_AUDIO   ", 11);
    // File system type
    memcpy(&buf[54], "FAT16   ", 8);
    // Boot signature
    buf[510] = 0x55; buf[511] = 0xAA;
}

// Generate FAT Sector with standard End-Of-Chain marker
static void get_fat_sector(uint32_t fat_sec_idx, uint8_t *buf) {
    uint16_t *entries = (uint16_t*)buf;
    uint32_t start_cluster = fat_sec_idx * 256;
    for (int i = 0; i < 256; i++) {
        uint32_t cluster = start_cluster + i;
        if (cluster == 0) {
            entries[i] = 0xFFF8; // Media descriptor
        } else if (cluster == 1) {
            entries[i] = 0xFFFF; // Clean shutdown marker
        } else if (cluster >= 2 && cluster < LAST_CLUSTER) {
            entries[i] = (uint16_t)(cluster + 1); // Continuous file chain
        } else if (cluster == LAST_CLUSTER) {
            entries[i] = 0xFFFF; // Standard End-Of-Chain (EOC) marker
        } else {
            entries[i] = 0x0000; // Unused
        }
    }
}

// Generate Root Directory Sector
static void get_root_dir_sector(uint32_t dir_sec_idx, uint8_t *buf) {
    memset(buf, 0, SECTOR_SIZE);
    if (dir_sec_idx == 0) {
        // Entry 0: Volume Label
        memcpy(&buf[0], "VW_AUDIO   ", 11);
        buf[11] = 0x08; // FILE_ATTR_VOLUME_LABEL

        // Entry 1: VW_MUSIC.MP3
        uint8_t *e = &buf[32];
        memcpy(&e[0], "VW_MUSIC", 8);
        memcpy(&e[8], "MP3", 3);
        e[11] = 0x20; // FILE_ATTR_ARCHIVE
        // Creation date/time (2026-11-16 08:30)
        e[22] = 0x00; e[23] = 0x44;
        e[24] = 0x21; e[25] = 0x5D;
        // Starting cluster (Cluster 2)
        e[26] = 0x02; e[27] = 0x00;
        // File size (2,146,304,000 bytes = 37.26 hours)
        e[28] = (uint8_t)(FILE_SIZE & 0xFF);
        e[29] = (uint8_t)((FILE_SIZE >> 8) & 0xFF);
        e[30] = (uint8_t)((FILE_SIZE >> 16) & 0xFF);
        e[31] = (uint8_t)((FILE_SIZE >> 24) & 0xFF);
    }
}

// Build standard 128-byte ID3v1 Tag for the end of the file
static void get_id3v1_tag(uint8_t *tag) {
    memset(tag, 0, 128);
    memcpy(&tag[0], "TAG", 3);
    memcpy(&tag[3], "VW Bluetooth Live             ", 30);
    memcpy(&tag[33], "Live Audio Stream             ", 30);
    memcpy(&tag[63], "VW Vento RCD 310              ", 30);
    memcpy(&tag[93], "2026", 4);
    memcpy(&tag[97], "Antigravity Realtime Stream   ", 28);
    tag[126] = 1;
    tag[127] = 12;
}

// Dynamic Live Sector Reader with Resync-On-Resume
static void read_one_sector(uint32_t lba, uint8_t *sec_buf) {
    uint32_t now = millis();

    // Check if the car stereo just resumed reading after a break / pause (> 1500 ms)
    if (last_host_read_time > 0 && (now - last_host_read_time > 1500)) {
        // RESYNC TO THE PRESENT:
        // Flush stale audio backlog down to 32 KB (~2.0 seconds) so the host's initial burst read has plenty of audio!
        if (live_streaming && rb_available() > 32768) {
            while (rb_available() > 32768) {
                rb_read();
            }
        }
    }
    last_host_read_time = now;

    if (lba == 0) {
        get_boot_sector(sec_buf);
    } else if (lba >= 1 && lba <= SECTORS_PER_FAT) {
        get_fat_sector(lba - 1, sec_buf);
    } else if (lba > SECTORS_PER_FAT && lba <= (NUM_FATS * SECTORS_PER_FAT)) {
        get_fat_sector(lba - 1 - SECTORS_PER_FAT, sec_buf);
    } else if (lba >= ROOT_DIR_START && lba < DATA_START_SECTOR) {
        get_root_dir_sector(lba - ROOT_DIR_START, sec_buf);
    } else if (lba >= DATA_START_SECTOR && lba < TOTAL_SECTORS) {
        // Last sector of the virtual file: include standard ID3v1 metadata in the last 128 bytes
        if (lba == TOTAL_SECTORS - 1) {
            for (uint32_t i = 0; i < 384; i++) {
                sec_buf[i] = CHIME_128K[chime_pos++];
                if (chime_pos >= CHIME_128K_LEN) chime_pos = 0;
            }
            get_id3v1_tag(&sec_buf[384]);
            return;
        }

        // Active Bluetooth streaming: serve real-time MP3 frames
        if (live_streaming) {
            if (rb_available() >= SECTOR_SIZE) {
                for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
                    sec_buf[i] = rb_read();
                }
            } else {
                // If the host briefly reads faster than real-time, send standard silence frame padding
                // NEVER inject chime into the middle of live music!
                memset(sec_buf, 0, SECTOR_SIZE);
                memcpy(sec_buf, SILENCE_FRAME, sizeof(SILENCE_FRAME));
            }
        } else {
            // Standby comfort chime (pure 128 kbps 44.1 kHz stereo frames, clean loop)
            for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
                sec_buf[i] = CHIME_128K[chime_pos++];
                if (chime_pos >= CHIME_128K_LEN) chime_pos = 0;
            }
        }
    } else {
        memset(sec_buf, 0, SECTOR_SIZE);
    }
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    uint8_t *dest = (uint8_t*)buffer;
    uint32_t bytes_remaining = bufsize;
    uint32_t cur_lba = lba;
    uint32_t cur_offset = offset;
    static uint8_t sec_temp[SECTOR_SIZE];

    while (bytes_remaining > 0) {
        uint32_t take = SECTOR_SIZE - cur_offset;
        if (take > bytes_remaining) take = bytes_remaining;

        if (cur_offset == 0 && take == SECTOR_SIZE) {
            read_one_sector(cur_lba, dest);
        } else {
            read_one_sector(cur_lba, sec_temp);
            memcpy(dest, sec_temp + cur_offset, take);
        }

        dest += take;
        bytes_remaining -= take;
        cur_lba++;
        cur_offset = 0;
    }

    return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    return bufsize;
}

void setup() {
    Serial.begin(115200);

    // Set status LED: Blue = Standby Chime
    rgbLedWrite(RGB_BUILTIN, 0, 0, 30);

    // 2,000,000 baud (2 Mbps) UART link to WROOM with 128 KB buffer
    Serial1.setRxBufferSize(131072);
    Serial1.begin(2000000, SERIAL_8N1, AUDIO_RX_PIN, AUDIO_TX_PIN);

    // Initialize Shine MP3 Encoder on ESP32-S3 (128 kbps, 44.1 kHz, Joint Stereo)
    shine_config_t config;
    config.wave.channels = PCM_STEREO;
    config.wave.samplerate = 44100;
    config.mpeg.mode = JOINT_STEREO;
    config.mpeg.bitr = 128;
    config.mpeg.emph = NONE;
    config.mpeg.copyright = 0;
    config.mpeg.original = 1;

    shine = shine_initialise(&config);

    // Allocate 1 MB MP3 ring buffer in 8MB Octal PSRAM
    if (psramInit()) {
        mp3_ring = (uint8_t*)ps_malloc(MP3_RING_SIZE);
        if (mp3_ring) {
            actual_rb_size = MP3_RING_SIZE;
            Serial.println("Allocated 1MB MP3 buffer in PSRAM!");
        }
    }
    if (!mp3_ring) {
        actual_rb_size = 128 * 1024;
        mp3_ring = (uint8_t*)malloc(actual_rb_size);
        Serial.println("Allocated 128KB MP3 buffer in Internal RAM!");
    }

    // Initialize TinyUSB MSC
    MSC.vendorID("VOLKSWAG");
    MSC.productID("VIRTUAL_STREAM");
    MSC.productRevision("1.0");
    MSC.mediaPresent(true);
    MSC.isWritable(false);
    MSC.onRead(onRead);
    MSC.onWrite(onWrite);
    MSC.begin(TOTAL_SECTORS, SECTOR_SIZE);

    USB.begin();
    Serial.println("ESP32-S3 Dynamic Live Stream Engine Initialized (2.0GB FAT16 Infinite Loop)");
}

void loop() {
    // Read incoming 16-bit 44.1 kHz stereo PCM from ESP32-WROOM over 2 Mbps UART
    int avail = Serial1.available();
    if (avail >= 4) {
        uint8_t pcm_chunk[2048];
        int to_read = (avail > (int)sizeof(pcm_chunk)) ? (int)sizeof(pcm_chunk) : (avail & ~3);
        int n = Serial1.read(pcm_chunk, to_read);
        if (n > 0) {
            const int16_t *pcm16 = (const int16_t*)pcm_chunk;
            uint32_t num_samples = n / 4;

            for (uint32_t i = 0; i < num_samples; i++) {
                pcm_interleaved[pcm_sample_count * 2] = pcm16[i * 2];
                pcm_interleaved[pcm_sample_count * 2 + 1] = pcm16[i * 2 + 1];
                pcm_sample_count++;

                // When a full frame (1152 samples) is ready, encode to MP3
                if (pcm_sample_count == 1152) {
                    if (shine) {
                        int written = 0;
                        unsigned char *mp3 = shine_encode_buffer_interleaved(shine, pcm_interleaved, &written);
                        if (written > 0 && mp3 != nullptr) {
                            for (int b = 0; b < written; b++) {
                                rb_write(mp3[b]);
                            }
                            last_packet_time = millis();

                            // Pre-buffering: wait for 16 KB (~1 sec) before declaring live streaming active
                            if (!live_streaming && rb_available() >= 16384) {
                                live_streaming = true;
                                rgbLedWrite(RGB_BUILTIN, 0, 40, 0); // Turn LED Green when live audio is streaming!
                            }
                        }
                    }
                    pcm_sample_count = 0;
                }
            }
        }
    }

    // If no audio received for > 2.5 seconds and buffer is drained, transition back to standby chime
    if (live_streaming && (millis() - last_packet_time > 2500) && (rb_available() < SECTOR_SIZE)) {
        live_streaming = false;
        rgbLedWrite(RGB_BUILTIN, 0, 0, 30); // Turn LED Blue for standby chime
    }
}
