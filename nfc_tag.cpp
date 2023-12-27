
#include "nfc_tag.h"
#include <Adafruit_PN532.h>
#include "pn532-lib/pn532_rp2040.h"

#define PN532_SCK  (18)
#define PN532_MOSI (19)
#define PN532_SS   (17)
#define PN532_MISO (16)

#define SPI (spi0)

void try_nfc() {
    printf("SPI: %p\n", spi0);
    //Adafruit_PN532 nfc(PN532_SS);
    Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

    uint32_t versiondata = nfc.getFirmwareVersion();
    if (! versiondata) {
        printf("Didn't find PN53x board\n");
        return;
    }

    printf("Found PN53x card: %lux\n", (unsigned long) versiondata);
}