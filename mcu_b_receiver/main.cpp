#include "mbed.h"
#include <cstdio>
#include <cstring>

#define BUFFERSIZE 64

BufferedSerial pc(USBTX, USBRX, 9600);
BufferedSerial receiver(PA_9, PA_10, 9600);
PwmOut     servo(D6);
DigitalOut led(LED1);

char buffer[BUFFERSIZE] = {0};
int payload = 0;

enum ParseState {
    SEARCH_START,
    READ_LEN_MSB,
    READ_LEN_LSB,
    READ_FRAME_DATA,
    READ_CHECKSUM
};

void setServo360(int priority) {
    switch (priority) {
        case 0: servo.pulsewidth_us(1000); break;    // Standard
        case 1: servo.pulsewidth_us(1500); break;    // Express
        case 2: servo.pulsewidth_us(2000); break;    // Urgent
        default: servo.pulsewidth_us(1500); break;
    }
}

bool verify_secure_join() {
    ThisThread::sleep_for(1100ms);
    receiver.write("+++", 3);
    ThisThread::sleep_for(1100ms);

    char response[16] = {0};
    receiver.write("ATEE\r", 5);
    ThisThread::sleep_for(100ms);

    int bytes_read = 0;
    while (receiver.read(&response[bytes_read], 1) > 0 && bytes_read < 15) {
        bytes_read++;
    }

    receiver.write("ATCN\r", 5);
    ThisThread::sleep_for(100ms);

    return (strstr(response, "1") != NULL);
}

int main() {
    receiver.set_blocking(false);

    servo.period_ms(20);
    servo.pulsewidth_us(1500);

    char init[] = "\r\n--- MCU B Ready ---\r\n";
    pc.write(init, sizeof(init) - 1);

    verify_secure_join();

    ParseState state = SEARCH_START;
    uint16_t frame_len = 0;
    uint16_t data_index = 0;
    uint32_t checksum_sum = 0;
    uint8_t rx_byte = 0;

    while (true) {
        while (receiver.read(&rx_byte, 1) > 0) {
            switch (state) {
                case SEARCH_START:
                    if (rx_byte == 0x7E) state = READ_LEN_MSB;
                    break;

                case READ_LEN_MSB:
                    frame_len = ((uint16_t)rx_byte) << 8;
                    state = READ_LEN_LSB;
                    break;

                case READ_LEN_LSB:
                    frame_len |= rx_byte;
                    data_index = 0;
                    checksum_sum = 0;
                    if (frame_len >= 13 && frame_len < BUFFERSIZE)
                       state = READ_FRAME_DATA;
                   else
                       state = SEARCH_START;
                   break;

                case READ_FRAME_DATA:
                    buffer[data_index++] = rx_byte;
                    checksum_sum += rx_byte;
                    if (data_index >= frame_len) state = READ_CHECKSUM;
                    break;

                case READ_CHECKSUM:
                    if ((uint8_t)(0xFF - (checksum_sum & 0xFF)) == rx_byte) {
                        if (buffer[0] == 0x90) {

                           uint8_t marker = buffer[frame_len - 2];
                           if (marker == 0xDA) {

                               payload = buffer[frame_len - 1];

                                int priority = (payload >> 6) & 0x03;
                                int pkgId    = payload & 0x3F;

                               setServo360(priority);
                               led = !led;

                               const char* label[] = {"Standard", "Express", "Urgent"};
                               char log[90];
                               int len = snprintf(log, sizeof(log),
                                   "[MCU B RX] priority=%d (%s) pkgId=%d payload=0x%02X\r\n",
                                   priority,
                                   priority <= 2 ? label[priority] : "?",
                                   pkgId, payload);
                               pc.write(log, len);
                           }
                       }
                   } else {

                       pc.write("Checksum FAIL\r\n", 15);
                   }
                   state = SEARCH_START;
                   break;
            }
        }

        ThisThread::sleep_for(1ms);
    }
}
