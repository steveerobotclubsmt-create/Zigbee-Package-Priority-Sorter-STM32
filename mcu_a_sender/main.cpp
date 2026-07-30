#include "mbed.h"
#include <cstdio>
#include <cstring>

#define BUFFSIZE 20

BufferedSerial pc(USBTX, USBRX, 9600);
BufferedSerial router(PA_9, PA_10, 9600);
AnalogIn potPin(A0);

char msgBuff[1] = {0};
int trackingID = 0;

uint8_t fuzzify(float val) {
    if (val < 0.33f) return 0;
    else if (val < 0.66f) return 1;
    else return 2;
}

uint8_t buildPayload(uint8_t priority, uint8_t counter) {
    return (priority << 6) | (counter & 0x3F);
}

bool verify_secure_join() {
    ThisThread::sleep_for(1100ms);
    router.write("+++", 3);
    ThisThread::sleep_for(1100ms);

    char response[16] = {0};
    router.write("ATEE\r", 5);
    ThisThread::sleep_for(100ms);

    int bytes_read = 0;
    while (router.read(&response[bytes_read], 1) > 0 && bytes_read < 15) {
        bytes_read++;
    }

    router.write("ATCN\r", 5);
    ThisThread::sleep_for(100ms);

    return (strstr(response, "1") != NULL);
}

void CreateMessage(char *msg, int len) {
    char startByte[]    = {0x7E};
    char msgLen[]       = {0x00, 0x10};
    char type[]         = {0x10};
    char frameID[]      = {0x00};
    char destAdd1[]     = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    char destAdd2[]     = {0x00, 0x00};
    char broadcastRad[] = {0x00};
    char options[]      = {0x00};
    char dataMarker[]   = {0xDA};

    int finallen = 20;
    char _xbeeMsg2[20] = {0};
    int temp = 0;

    _xbeeMsg2[0] = startByte[0];
    _xbeeMsg2[1] = msgLen[0];
    _xbeeMsg2[2] = msgLen[1];
    _xbeeMsg2[3] = type[0];
    _xbeeMsg2[4] = frameID[0];

    for(int i=0; i<8; i++) _xbeeMsg2[5+i] = destAdd1[i];

    _xbeeMsg2[13] = destAdd2[0];
    _xbeeMsg2[14] = destAdd2[1];
    _xbeeMsg2[15] = broadcastRad[0];
    _xbeeMsg2[16] = options[0];
    _xbeeMsg2[17] = dataMarker[0];

    for(int i=0; i<len; i++) {
        _xbeeMsg2[18+i] = msg[i];
    }

    for(int i=3; i<=18; i++) {
        temp += _xbeeMsg2[i];
    }
    _xbeeMsg2[19] = 0xFF - (temp & 0xFF);

    router.write(_xbeeMsg2, finallen);

    char log[80];
    int log_len = snprintf(log, sizeof(log),
                            "[MCU A TX] priority=%d id=%d payload=0x%02X checksum=0x%02X\r\n",
                            (msg[0] >> 6) & 0x03, msg[0] & 0x3F,
                            msg[0], _xbeeMsg2[19]);
    pc.write(log, log_len);
}

uint16_t readPotAveraged() {
    uint32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += (uint16_t)(potPin.read() * 4095);
        wait_us(500);
    }
    return sum / 10;
}

int main() {
    router.set_blocking(false);

    char init[] = "\r\n--- MCU A Ready ---\r\n";
    pc.write(init, sizeof(init) - 1);

    verify_secure_join();

    while (true) {
        uint16_t rawAvg = readPotAveraged();
        uint8_t priority = fuzzify(rawAvg / 4095.0f);

        msgBuff[0] = buildPayload(priority, trackingID);
        CreateMessage(msgBuff, 1);

        trackingID = (trackingID + 1) & 0x3F;
        ThisThread::sleep_for(1000ms);
    }
}
