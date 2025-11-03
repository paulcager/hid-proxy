/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "tusb.h"
#include "hid_proxy.h"
#include "logging.h"
#include "macros.h" // For serialize_macros and parse_macros

// A static RAM buffer to hold the content of our virtual disk (macros.txt)
// 23KB buffer to handle text expansion (text is ~3.8x larger than binary format)
#define MSC_DISK_BUFFER_SIZE (23 * 1024)
static char msc_disk_buffer[MSC_DISK_BUFFER_SIZE];

// Invoked when received SCSI Inquiry command
// Application fill vendor id, product id, revision and Correspondent capacity
// for the inquiry command
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
  (void) lun;

  const char vid[] = "TinyUSB";
  const char pid[] = "Mass Storage";
  const char rev[] = "1.0";

  memcpy(vendor_id  , vid, strlen(vid));
  memcpy(product_id , pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command. Return true if the unit is ready.
// If not ready, SCSI driver will sense the Media not present error.
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  (void) lun;

  // We will always be ready, as our disk is internal flash.
  return true;
}

// Invoked when received SCSI Read Capacity command
// Application fill length of device (lba) and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
  (void) lun;

  *block_count = MSC_DISK_BUFFER_SIZE / 512; // Our virtual disk size in 512-byte blocks
  *block_size = 512; // Standard block size
}

// Invoked when received Start Stop Unit command. Return true to accept the command.
// Application can use this to save/restore state.
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t command, bool start, bool load_eject)
{
  (void) lun;
  (void) command;
  (void) load_eject;

  if (start)
  {
    // start command: populate the buffer with current macros from flash
    memset(msc_disk_buffer, 0, sizeof(msc_disk_buffer));
    if (!serialize_macros((const store_t*)FLASH_STORE_ADDRESS, msc_disk_buffer, sizeof(msc_disk_buffer))) {
        LOG_ERROR("MSC: Failed to serialize macros - buffer too small!\n");
        // Fill buffer with error message
        snprintf(msc_disk_buffer, sizeof(msc_disk_buffer),
                 "# ERROR: Too many macros to display!\n"
                 "# Please reduce the number of macros in HID mode first.\n");
    }
  }
  else
  {
    // stop command (eject): parse the buffer and write to flash
    LOG_INFO("MSC: Disk Ejected! Parsing and writing to flash.\n");
    // Parse directly into kb.local_store (which is already allocated to FLASH_STORE_SIZE)
    if (parse_macros(msc_disk_buffer, kb.local_store)) {
        save_state(&kb);
        LOG_INFO("MSC: Macros parsed and written to flash successfully.\n");
    } else {
        LOG_ERROR("MSC: Failed to parse macros from disk buffer. Flash not updated.\n");
    }
  }

  return true;
}

// Invoked when received SCSI Read10 command
// Application fill buffer with data from disk via Block Address (LBA) and length
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
  (void) lun;

  uint32_t const disk_addr = lba * 512 + offset;

  // Ensure we don't read beyond our virtual disk size
  if (disk_addr + bufsize > MSC_DISK_BUFFER_SIZE) {
      bufsize = MSC_DISK_BUFFER_SIZE - disk_addr;
  }

  memcpy(buffer, msc_disk_buffer + disk_addr, bufsize);

  return bufsize;
}

// Invoked when received SCSI Write10 command
// Application write data from buffer to disk via Block Address (LBA) and length
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  (void) lun;

  uint32_t const disk_addr = lba * 512 + offset;

  // Ensure we don't write beyond our virtual disk size
  if (disk_addr + bufsize > MSC_DISK_BUFFER_SIZE) {
      bufsize = MSC_DISK_BUFFER_SIZE - disk_addr;
  }

  memcpy(msc_disk_buffer + disk_addr, buffer, bufsize);

  return bufsize;
}

// Invoked when received SCSI Write Protect command. Return true if the disk is write protected.
bool tud_msc_is_writable_cb(uint8_t lun)
{
  (void) lun;

  // For now, allow writing. We might make it read-only in some states.
  return true;
}

// Invoked when received SCSI Flush command. Return true to accept the command.
bool tud_msc_flush_cb(uint8_t lun)
{
  (void) lun;

  // Nothing to do for now, as we're not caching writes.
  return true;
}

// Invoked when received SCSI Get Max LUN command
// Application return number of LUNs (e.g. 1 for only one flash drive)
uint8_t tud_msc_get_max_lun_cb(void)
{
  return 1; // single LUN
}

// Invoked when received SCSI Sense command. Return false for no error, true for error.
// To act as a removable disk, we need to report 'Media Not Present' when not mounted.
// For now, we'll always report no error.
bool tud_msc_sense_cb(uint8_t lun, uint8_t scsi_sense_key, uint8_t scsi_asc, uint8_t scsi_ascq)
{
  (void) lun;
  (void) scsi_sense_key;
  (void) scsi_asc;
  (void) scsi_ascq;

  return false; // no error
}

int32_t tud_msc_scsi_cb (uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
  // read10 & write10 has their own callback and MUST not be handled here

  int32_t resplen = 0;

  switch (scsi_cmd[0])
  {
    case SCSI_CMD_TEST_UNIT_READY:
      // Command that host uses to check if device is ready
      // This mostly happens when device is plugged in
      resplen = 0;
      break;

    default:
      // Set Sense Response
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

      // unsupported command
      resplen = -1;
      break;
  }

  return resplen;
}
