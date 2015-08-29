/*

  Copyright (c) 2014 Guillaume Duc <guillaume@guiduc.org>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#include "ch.h"
#include "hal.h"

#include "usb_main.h"
#include "usb_keyboard.h"
#ifdef MOUSE_ENABLE
  #include "usb_mouse.h"
#endif
#ifdef CONSOLE_ENABLE
  #include "usb_console.h"
#endif
#ifdef EXTRAKEY_ENABLE
  #include "usb_extra.h"
#endif

// Mac OS-X and Linux automatically load the correct drivers.  On
// Windows, even though the driver is supplied by Microsoft, an
// INF file is needed to load the driver.  These numbers need to
// match the INF file.
#ifndef VENDOR_ID
#   define VENDOR_ID    0xFEED
#endif

#ifndef PRODUCT_ID
#   define PRODUCT_ID   0xBABE
#endif

#ifndef DEVICE_VER
#   define DEVICE_VER   0x0100
#endif

// HID specific constants
#define USB_DESCRIPTOR_HID 0x21
#define USB_DESCRIPTOR_HID_REPORT 0x22
#define HID_GET_REPORT 0x01
#define HID_GET_IDLE 0x02
#define HID_GET_PROTOCOL 0x03
#define HID_SET_REPORT 0x09
#define HID_SET_IDLE 0x0A
#define HID_SET_PROTOCOL 0x0B

// USB Device Descriptor
static const uint8_t usb_device_descriptor_data[] = {
  USB_DESC_DEVICE(0x0200,      // bcdUSB (1.1)
                  0,           // bDeviceClass (defined in later in interface)
                  0,           // bDeviceSubClass
                  0,           // bDeviceProtocol
                  64,          // bMaxPacketSize (64 bytes) (the driver didn't work with 32)
                  VENDOR_ID,   // idVendor
                  PRODUCT_ID,  // idProduct
                  DEVICE_VER,      // bcdDevice
                  1,           // iManufacturer
                  2,           // iProduct
                  3,           // iSerialNumber
                  1)           // bNumConfigurations
};

// Device Descriptor wrapper
static const USBDescriptor usb_device_descriptor = {
  sizeof usb_device_descriptor_data,
  usb_device_descriptor_data
};

/*
 * HID Report Descriptor
 *
 * This is the description of the format and the content of the
 * different IN or/and OUT reports that your application can
 * receive/send
 *
 * See "Device Class Definition for Human Interface Devices (HID)"
 * (http://www.usb.org/developers/hidpage/HID1_11.pdf) for the
 * detailed descrition of all the fields
 */

// Keyboard Protocol 1, HID 1.11 spec, Appendix B, page 59-60
static const uint8_t keyboard_hid_report_desc_data[] = {
        0x05, 0x01,          // Usage Page (Generic Desktop),
        0x09, 0x06,          // Usage (Keyboard),
        0xA1, 0x01,          // Collection (Application),
        0x75, 0x01,          //   Report Size (1),
        0x95, 0x08,          //   Report Count (8),
        0x05, 0x07,          //   Usage Page (Key Codes),
        0x19, 0xE0,          //   Usage Minimum (224),
        0x29, 0xE7,          //   Usage Maximum (231),
        0x15, 0x00,          //   Logical Minimum (0),
        0x25, 0x01,          //   Logical Maximum (1),
        0x81, 0x02,          //   Input (Data, Variable, Absolute), ;Modifier byte
        0x95, 0x01,          //   Report Count (1),
        0x75, 0x08,          //   Report Size (8),
        0x81, 0x03,          //   Input (Constant),                 ;Reserved byte
        0x95, 0x05,          //   Report Count (5),
        0x75, 0x01,          //   Report Size (1),
        0x05, 0x08,          //   Usage Page (LEDs),
        0x19, 0x01,          //   Usage Minimum (1),
        0x29, 0x05,          //   Usage Maximum (5),
        0x91, 0x02,          //   Output (Data, Variable, Absolute), ;LED report
        0x95, 0x01,          //   Report Count (1),
        0x75, 0x03,          //   Report Size (3),
        0x91, 0x03,          //   Output (Constant),                 ;LED report padding
        0x95, KBD_REPORT_KEYS,    //   Report Count (),
        0x75, 0x08,          //   Report Size (8),
        0x15, 0x00,          //   Logical Minimum (0),
        0x25, 0xFF,          //   Logical Maximum(255),
        0x05, 0x07,          //   Usage Page (Key Codes),
        0x19, 0x00,          //   Usage Minimum (0),
        0x29, 0xFF,          //   Usage Maximum (255),
        0x81, 0x00,          //   Input (Data, Array),
        0xc0                 // End Collection
};
// wrapper
static const USBDescriptor keyboard_hid_report_descriptor = {
  sizeof keyboard_hid_report_desc_data,
  keyboard_hid_report_desc_data
};

#ifdef NKRO_ENABLE
static const uint8_t nkro_hid_report_desc_data[] = {
        0x05, 0x01,                     // Usage Page (Generic Desktop),
        0x09, 0x06,                     // Usage (Keyboard),
        0xA1, 0x01,                     // Collection (Application),
        // bitmap of modifiers
        0x75, 0x01,                     //   Report Size (1),
        0x95, 0x08,                     //   Report Count (8),
        0x05, 0x07,                     //   Usage Page (Key Codes),
        0x19, 0xE0,                     //   Usage Minimum (224),
        0x29, 0xE7,                     //   Usage Maximum (231),
        0x15, 0x00,                     //   Logical Minimum (0),
        0x25, 0x01,                     //   Logical Maximum (1),
        0x81, 0x02,                     //   Input (Data, Variable, Absolute), ;Modifier byte
        // LED output report
        0x95, 0x05,                     //   Report Count (5),
        0x75, 0x01,                     //   Report Size (1),
        0x05, 0x08,                     //   Usage Page (LEDs),
        0x19, 0x01,                     //   Usage Minimum (1),
        0x29, 0x05,                     //   Usage Maximum (5),
        0x91, 0x02,                     //   Output (Data, Variable, Absolute),
        0x95, 0x01,                     //   Report Count (1),
        0x75, 0x03,                     //   Report Size (3),
        0x91, 0x03,                     //   Output (Constant),
        // bitmap of keys
        0x95, NKRO_REPORT_KEYS*8,       //   Report Count (),
        0x75, 0x01,                     //   Report Size (1),
        0x15, 0x00,                     //   Logical Minimum (0),
        0x25, 0x01,                     //   Logical Maximum(1),
        0x05, 0x07,                     //   Usage Page (Key Codes),
        0x19, 0x00,                     //   Usage Minimum (0),
        0x29, NKRO_REPORT_KEYS*8-1,     //   Usage Maximum (),
        0x81, 0x02,                     //   Input (Data, Variable, Absolute),
        0xc0                            // End Collection
};
// wrapper
static const USBDescriptor nkro_hid_report_descriptor = {
  sizeof nkro_hid_report_desc_data,
  nkro_hid_report_desc_data
};
#endif

#ifdef MOUSE_ENABLE
// Mouse Protocol 1, HID 1.11 spec, Appendix B, page 59-60, with wheel extension
// http://www.microchip.com/forums/tm.aspx?high=&m=391435&mpage=1#391521
// http://www.keil.com/forum/15671/
// http://www.microsoft.com/whdc/device/input/wheel.mspx
static const uint8_t mouse_hid_report_desc_data[] = {
    /* mouse */
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,                    // USAGE (Mouse)
    0xa1, 0x01,                    // COLLECTION (Application)
    //0x85, REPORT_ID_MOUSE,         //   REPORT_ID (1)
    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)
                                   // ----------------------------  Buttons
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x05,                    //     USAGE_MAXIMUM (Button 5)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x95, 0x05,                    //     REPORT_COUNT (5)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x03,                    //     REPORT_SIZE (3)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x81, 0x03,                    //     INPUT (Cnst,Var,Abs)
                                   // ----------------------------  X,Y position
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)
                                   // ----------------------------  Vertical wheel
    0x09, 0x38,                    //     USAGE (Wheel)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)        - reset physical
    0x45, 0x00,                    //     PHYSICAL_MAXIMUM (0)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)
                                   // ----------------------------  Horizontal wheel
    0x05, 0x0c,                    //     USAGE_PAGE (Consumer Devices)
    0x0a, 0x38, 0x02,              //     USAGE (AC Pan)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)
    0xc0,                          //   END_COLLECTION
    0xc0,                          // END_COLLECTION
};
// wrapper
static const USBDescriptor mouse_hid_report_descriptor = {
  sizeof mouse_hid_report_desc_data,
  mouse_hid_report_desc_data
};
#endif

#ifdef CONSOLE_ENABLE
static const uint8_t console_hid_report_desc_data[] = {
  0x06, 0x31, 0xFF, // Usage Page 0xFF31 (vendor defined)
  0x09, 0x74,       // Usage 0x74
  0xA1, 0x53,       // Collection 0x53
  0x75, 0x08,       // report size = 8 bits
  0x15, 0x00,       // logical minimum = 0
  0x26, 0xFF, 0x00, // logical maximum = 255
  0x95, CONSOLE_SIZE, // report count
  0x09, 0x75,       // usage
  0x81, 0x02,       // Input (array)
  0xC0              // end collection
};
// wrapper
static const USBDescriptor console_hid_report_descriptor = {
  sizeof console_hid_report_desc_data,
  console_hid_report_desc_data
};
#endif

#ifdef EXTRAKEY_ENABLE
// audio controls & system controls
// http://www.microsoft.com/whdc/archive/w2kbd.mspx
static const uint8_t extra_hid_report_desc_data[] = {
    /* system control */
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x80,                    // USAGE (System Control)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x85, REPORT_ID_SYSTEM,        //   REPORT_ID (2)
    0x15, 0x01,                    //   LOGICAL_MINIMUM (0x1)
    0x25, 0xb7,                    //   LOGICAL_MAXIMUM (0xb7)
    0x19, 0x01,                    //   USAGE_MINIMUM (0x1)
    0x29, 0xb7,                    //   USAGE_MAXIMUM (0xb7)
    0x75, 0x10,                    //   REPORT_SIZE (16)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x81, 0x00,                    //   INPUT (Data,Array,Abs)
    0xc0,                          // END_COLLECTION
    /* consumer */
    0x05, 0x0c,                    // USAGE_PAGE (Consumer Devices)
    0x09, 0x01,                    // USAGE (Consumer Control)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x85, REPORT_ID_CONSUMER,      //   REPORT_ID (3)
    0x15, 0x01,                    //   LOGICAL_MINIMUM (0x1)
    0x26, 0x9c, 0x02,              //   LOGICAL_MAXIMUM (0x29c)
    0x19, 0x01,                    //   USAGE_MINIMUM (0x1)
    0x2a, 0x9c, 0x02,              //   USAGE_MAXIMUM (0x29c)
    0x75, 0x10,                    //   REPORT_SIZE (16)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x81, 0x00,                    //   INPUT (Data,Array,Abs)
    0xc0,                          // END_COLLECTION
};
// wrapper
static const USBDescriptor extra_hid_report_descriptor = {
  sizeof extra_hid_report_desc_data,
  extra_hid_report_desc_data
};
#endif


/*
 * Configuration Descriptor tree for a HID device
 *
 * The HID Specifications version 1.11 require the following order:
 * - Configuration Descriptor
 * - Interface Descriptor
 * - HID Descriptor
 * - Endpoints Descriptors
 */
#define KBD_HID_DESC_NUM                0
#define KBD_HID_DESC_OFFSET             (9+(9+9+7)*KBD_HID_DESC_NUM+9)

#ifdef MOUSE_ENABLE
#   define MOUSE_HID_DESC_NUM           (KBD_HID_DESC_NUM + 1)
#   define MOUSE_HID_DESC_OFFSET        (9+(9+9+7)*MOUSE_HID_DESC_NUM+9)
#else
#   define MOUSE_HID_DESC_NUM           (KBD_HID_DESC_NUM + 0)
#endif

#ifdef CONSOLE_ENABLE
#define CONSOLE_HID_DESC_NUM              (MOUSE_HID_DESC_NUM + 1)
#define CONSOLE_HID_DESC_OFFSET           (9+(9+9+7)*CONSOLE_HID_DESC_NUM+9)
#else
#   define CONSOLE_HID_DESC_NUM           (MOUSE_HID_DESC_NUM + 0)
#endif

#ifdef EXTRAKEY_ENABLE
#   define EXTRA_HID_DESC_NUM           (CONSOLE_HID_DESC_NUM + 1)
#   define EXTRA_HID_DESC_OFFSET        (9+(9+9+7)*EXTRA_HID_DESC_NUM+9)
#else
#   define EXTRA_HID_DESC_NUM           (CONSOLE_HID_DESC_NUM + 0)
#endif

#ifdef NKRO_ENABLE
#   define NKRO_HID_DESC_NUM            (EXTRA_HID_DESC_NUM + 1)
#   define NKRO_HID_DESC_OFFSET         (9+(9+9+7)*EXTRA_HID_DESC_NUM+9)
#else
#   define NKRO_HID_DESC_NUM            (EXTRA_HID_DESC_NUM + 0)
#endif

#define NUM_INTERFACES                  (NKRO_HID_DESC_NUM + 1)
#define CONFIG1_DESC_SIZE               (9+(9+9+7)*NUM_INTERFACES)

static const uint8_t hid_configuration_descriptor_data[] = {
  // Configuration Descriptor (9 bytes) USB spec 9.6.3, page 264-266, Table 9-10
  USB_DESC_CONFIGURATION(CONFIG1_DESC_SIZE, // wTotalLength
                         NUM_INTERFACES,    // bNumInterfaces
                         1,    // bConfigurationValue
                         0,    // iConfiguration
                         0xA0, // bmAttributes
                         50),  // bMaxPower (100mA)

  // Interface Descriptor (9 bytes) USB spec 9.6.5, page 267-269, Table 9-12
  USB_DESC_INTERFACE(KBD_INTERFACE,        // bInterfaceNumber
                     0,        // bAlternateSetting
                     1,        // bNumEndpoints
                     0x03,     // bInterfaceClass: HID
                     0x01,     // bInterfaceSubClass: Boot
                     0x01,     // bInterfaceProtocol: Keyboard
                     0),       // iInterface

  // HID descriptor (9 bytes) HID 1.11 spec, section 6.2.1
  USB_DESC_BYTE(9),            // bLength
  USB_DESC_BYTE(0x21),         // bDescriptorType (HID class)
  USB_DESC_BCD(0x0111),        // bcdHID: HID version 1.11
  USB_DESC_BYTE(0),            // bCountryCode
  USB_DESC_BYTE(1),            // bNumDescriptors
  USB_DESC_BYTE(0x22),         // bDescriptorType (report desc)
  USB_DESC_WORD(sizeof(keyboard_hid_report_desc_data)), // wDescriptorLength

  // Endpoint Descriptor (7 bytes) USB spec 9.6.6, page 269-271, Table 9-13
  USB_DESC_ENDPOINT(KBD_ENDPOINT | 0x80,  // bEndpointAddress
                    0x03,      // bmAttributes (Interrupt)
                    KBD_SIZE,  // wMaxPacketSize
                    10),       // bInterval

  #ifdef MOUSE_ENABLE
  // Interface Descriptor (9 bytes) USB spec 9.6.5, page 267-269, Table 9-12
  USB_DESC_INTERFACE(MOUSE_INTERFACE,   // bInterfaceNumber
                    0,         // bAlternateSetting
                    1,         // bNumEndpoints
                    0x03,      // bInterfaceClass (0x03 = HID)
                    // ThinkPad T23 BIOS doesn't work with boot mouse.
                    0x00,      // bInterfaceSubClass (0x01 = Boot)
                    0x00,      // bInterfaceProtocol (0x02 = Mouse)
                    /*                  
                    0x01,      // bInterfaceSubClass (0x01 = Boot)
                    0x02,      // bInterfaceProtocol (0x02 = Mouse)
                    */                  
                    0),         // iInterface

  // HID descriptor (9 bytes) HID 1.11 spec, section 6.2.1
  USB_DESC_BYTE(9),            // bLength
  USB_DESC_BYTE(0x21),         // bDescriptorType (HID class)
  USB_DESC_BCD(0x0111),        // bcdHID: HID version 1.11
  USB_DESC_BYTE(0),            // bCountryCode
  USB_DESC_BYTE(1),            // bNumDescriptors
  USB_DESC_BYTE(0x22),         // bDescriptorType (report desc)
  USB_DESC_WORD(sizeof(mouse_hid_report_desc_data)), // wDescriptorLength    

  // Endpoint Descriptor (7 bytes) USB spec 9.6.6, page 269-271, Table 9-13
  USB_DESC_ENDPOINT(MOUSE_ENDPOINT | 0x80,  // bEndpointAddress
                    0x03,      // bmAttributes (Interrupt)
                    MOUSE_SIZE,  // wMaxPacketSize
                    1),        // bInterval
  #endif

  #ifdef CONSOLE_ENABLE
  // Interface Descriptor (9 bytes) USB spec 9.6.5, page 267-269, Table 9-12
  USB_DESC_INTERFACE(CONSOLE_INTERFACE, // bInterfaceNumber
                     0,        // bAlternateSetting
                     1,        // bNumEndpoints
                     0x03,     // bInterfaceClass: HID
                     0x00,     // bInterfaceSubClass: None
                     0x00,     // bInterfaceProtocol: None
                     0),       // iInterface

  // HID descriptor (9 bytes) HID 1.11 spec, section 6.2.1
  USB_DESC_BYTE(9),            // bLength
  USB_DESC_BYTE(0x21),         // bDescriptorType (HID class)
  USB_DESC_BCD(0x0111),        // bcdHID: HID version 1.11
  USB_DESC_BYTE(0),            // bCountryCode
  USB_DESC_BYTE(1),            // bNumDescriptors
  USB_DESC_BYTE(0x22),         // bDescriptorType (report desc)
  USB_DESC_WORD(sizeof(console_report_desc_data)), // wDescriptorLength

  // Endpoint Descriptor (7 bytes) USB spec 9.6.6, page 269-271, Table 9-13
  USB_DESC_ENDPOINT(CONSOLE_ENDPOINT | 0x80,  // bEndpointAddress
                    0x03,      // bmAttributes (Interrupt)
                    CONSOLE_SIZE, // wMaxPacketSize
                    1),        // bInterval
  #endif

  #ifdef EXTRAKEY_ENABLE
  // Interface Descriptor (9 bytes) USB spec 9.6.5, page 267-269, Table 9-12
  USB_DESC_INTERFACE(EXTRA_INTERFACE, // bInterfaceNumber
                     0,        // bAlternateSetting
                     1,        // bNumEndpoints
                     0x03,     // bInterfaceClass: HID
                     0x00,     // bInterfaceSubClass: None
                     0x00,     // bInterfaceProtocol: None
                     0),       // iInterface

  // HID descriptor (9 bytes) HID 1.11 spec, section 6.2.1
  USB_DESC_BYTE(9),            // bLength
  USB_DESC_BYTE(0x21),         // bDescriptorType (HID class)
  USB_DESC_BCD(0x0111),        // bcdHID: HID version 1.11
  USB_DESC_BYTE(0),            // bCountryCode
  USB_DESC_BYTE(1),            // bNumDescriptors
  USB_DESC_BYTE(0x22),         // bDescriptorType (report desc)
  USB_DESC_WORD(sizeof(extra_report_desc_data)), // wDescriptorLength

  // Endpoint Descriptor (7 bytes) USB spec 9.6.6, page 269-271, Table 9-13
  USB_DESC_ENDPOINT(EXTRA_ENDPOINT | 0x80,  // bEndpointAddress
                    0x03,      // bmAttributes (Interrupt)
                    EXTRA_SIZE, // wMaxPacketSize
                    10),       // bInterval
  #endif

  #ifdef NKRO_ENABLE
  // Interface Descriptor (9 bytes) USB spec 9.6.5, page 267-269, Table 9-12
  USB_DESC_INTERFACE(NKRO_INTERFACE, // bInterfaceNumber
                     0,        // bAlternateSetting
                     1,        // bNumEndpoints
                     0x03,     // bInterfaceClass: HID
                     0x00,     // bInterfaceSubClass: None
                     0x00,     // bInterfaceProtocol: None
                     0),       // iInterface

  // HID descriptor (9 bytes) HID 1.11 spec, section 6.2.1
  USB_DESC_BYTE(9),            // bLength
  USB_DESC_BYTE(0x21),         // bDescriptorType (HID class)
  USB_DESC_BCD(0x0111),        // bcdHID: HID version 1.11
  USB_DESC_BYTE(0),            // bCountryCode
  USB_DESC_BYTE(1),            // bNumDescriptors
  USB_DESC_BYTE(0x22),         // bDescriptorType (report desc)
  USB_DESC_WORD(sizeof(nkro_hid_report_desc_data)), // wDescriptorLength

  // Endpoint Descriptor (7 bytes) USB spec 9.6.6, page 269-271, Table 9-13
  USB_DESC_ENDPOINT(NKRO_ENDPOINT | 0x80,  // bEndpointAddress
                    0x03,      // bmAttributes (Interrupt)
                    NKRO_SIZE, // wMaxPacketSize
                    1),       // bInterval
  #endif
};

// Configuration Descriptor wrapper
static const USBDescriptor hid_configuration_descriptor = {
  sizeof hid_configuration_descriptor_data,
  hid_configuration_descriptor_data
};

// wrappers
#define HID_DESCRIPTOR_SIZE 9
static const USBDescriptor keyboard_hid_descriptor = {
  HID_DESCRIPTOR_SIZE,
  &hid_configuration_descriptor_data[KBD_HID_DESC_OFFSET]
};
#ifdef MOUSE_ENABLE
static const USBDescriptor mouse_hid_descriptor = {
  HID_DESCRIPTOR_SIZE,
  &hid_configuration_descriptor_data[MOUSE_HID_DESC_OFFSET]
};
#endif
#ifdef CONSOLE_ENABLE
static const USBDescriptor console_hid_descriptor = {
  HID_DESCRIPTOR_SIZE,
  &hid_configuration_descriptor_data[CONSOLE_HID_DESC_OFFSET]
};
#endif
#ifdef EXTRAKEY_ENABLE
static const USBDescriptor extra_hid_descriptor = {
  HID_DESCRIPTOR_SIZE,
  &hid_configuration_descriptor_data[EXTRA_HID_DESC_OFFSET]
};
#endif
#ifdef NKRO_ENABLE
static const USBDescriptor nkro_hid_descriptor = {
  HID_DESCRIPTOR_SIZE,
  &hid_configuration_descriptor_data[NKRO_HID_DESC_OFFSET]
};
#endif


// U.S. English language identifier
static const uint8_t usb_string_langid[] = {
  USB_DESC_BYTE(4),                        // bLength
  USB_DESC_BYTE(USB_DESCRIPTOR_STRING),    // bDescriptorType
  USB_DESC_WORD(0x0409)                    // wLANGID (U.S. English)
};

// Vendor string = manufacturer
static const uint8_t usb_string_vendor[] = {
  USB_DESC_BYTE(38),                       // bLength
  USB_DESC_BYTE(USB_DESCRIPTOR_STRING),    // bDescriptorType
  'S', 0, 'T', 0, 'M', 0, 'i', 0, 'c', 0, 'r', 0, 'o', 0, 'e', 0,
  'l', 0, 'e', 0, 'c', 0, 't', 0, 'r', 0, 'o', 0, 'n', 0, 'i', 0,
  'c', 0, 's', 0
};

// Device Description string = product
static const uint8_t usb_string_description[] = {
  USB_DESC_BYTE(50),           // bLength
  USB_DESC_BYTE(USB_DESCRIPTOR_STRING),    // bDescriptorType
  'C', 0, 'h', 0, 'i', 0, 'b', 0, 'i', 0, 'O', 0, 'S', 0, '/', 0,
  'R', 0, 'T', 0, ' ', 0, 'L', 0, 'o', 0, 't', 0, 's', 0, 'a', 0,
  ' ', 0, 'H', 0, 'I', 0, 'D', 0, ' ', 0, 'U', 0, 'S', 0, 'B', 0
};

// Serial Number string (will be filled by the function init_usb_serial_string)
static uint8_t usb_string_serial[] = {
  USB_DESC_BYTE(22),                       // bLength
  USB_DESC_BYTE(USB_DESCRIPTOR_STRING),    // bDescriptorType
  '0', 0, 'x', 0, 'D', 0, 'E', 0, 'A', 0, 'D', 0, 'B', 0, 'E', 0, 'E', 0, 'F', 0
};

// Strings wrappers array
static const USBDescriptor usb_strings[] = {
  {sizeof usb_string_langid, usb_string_langid}
  ,
  {sizeof usb_string_vendor, usb_string_vendor}
  ,
  {sizeof usb_string_description, usb_string_description}
  ,
  {sizeof usb_string_serial, usb_string_serial}
};

/*
 * Handles the GET_DESCRIPTOR callback
 *
 * Returns the proper descriptor
 */
static const USBDescriptor* usb_get_descriptor_cb(USBDriver* usbp, uint8_t dtype, uint8_t dindex, uint16_t lang) {
  (void)usbp;
  (void)lang;
  switch(dtype) {
    // Generic descriptors
    case USB_DESCRIPTOR_DEVICE: // Device Descriptor
      return &usb_device_descriptor;
    case USB_DESCRIPTOR_CONFIGURATION:  // Configuration Descriptor
      return &hid_configuration_descriptor;
    case USB_DESCRIPTOR_STRING: // Strings
      if (dindex < 4)
        return &usb_strings[dindex];
      break;

      // HID specific descriptors
    case USB_DESCRIPTOR_HID:    // HID Descriptors
      switch(lang) {  // yea, poor label, it's actually wIndex from the setup packet
        case KBD_INTERFACE:
          return &keyboard_hid_descriptor;
#ifdef MOUSE_ENABLE
        case MOUSE_INTERFACE:
          return &mouse_hid_descriptor;
#endif
#ifdef CONSOLE_ENABLE
        case CONSOLE_INTERFACE:
          return &console_hid_descriptor;
#endif
#ifdef EXTRAKEY_ENABLE
        case EXTRA_INTERFACE:
          return &extra_hid_descriptor;
#endif
#ifdef NKRO_ENABLE
        case NKRO_INTERFACE:
          return &nkro_hid_descriptor;
#endif
      }
    case USB_DESCRIPTOR_HID_REPORT:     // HID Report Descriptor
      switch(lang) {
        case KBD_INTERFACE:
          return &keyboard_hid_report_descriptor;
#ifdef MOUSE_ENABLE
        case MOUSE_INTERFACE:
          return &mouse_hid_report_descriptor;
#endif
#ifdef CONSOLE_ENABLE
        case CONSOLE_INTERFACE:
          return &console_hid_report_descriptor;
#endif
#ifdef EXTRAKEY_ENABLE
        case EXTRA_INTERFACE:
          return &extra_hid_report_descriptor;
#endif
#ifdef NKRO_ENABLE
        case NKRO_INTERFACE:
          return &nkro_hid_report_descriptor;
#endif
      }      
  }
  return NULL;
}

// keyboard endpoint state structure
static USBInEndpointState kbd_ep_state;
// keyboard endpoint initialization structure (IN)
static const USBEndpointConfig kbd_ep_config = {
  USB_EP_MODE_TYPE_INTR,        // Interrupt EP
  NULL,                         // SETUP packet notification callback
  kbd_in_cb,                    // IN notification callback
  NULL,                         // OUT notification callback
  KBD_SIZE,                     // IN maximum packet size
  0,                            // OUT maximum packet size
  &kbd_ep_state,                // IN Endpoint state
  NULL,                         // OUT endpoint state
  2,                            // IN multiplier
  NULL                          // SETUP buffer (not a SETUP endpoint)
};

#ifdef MOUSE_ENABLE
// mouse endpoint state structure
static USBInEndpointState mouse_ep_state;

// mouse endpoint initialization structure (IN)
static const USBEndpointConfig mouse_ep_config = {
  USB_EP_MODE_TYPE_INTR,        // Interrupt EP
  NULL,                         // SETUP packet notification callback
  mouse_in_cb,                  // IN notification callback
  NULL,                         // OUT notification callback
  MOUSE_SIZE,                   // IN maximum packet size
  0,                            // OUT maximum packet size
  &mouse_ep_state,              // IN Endpoint state
  NULL,                         // OUT endpoint state
  2,                            // IN multiplier
  NULL                          // SETUP buffer (not a SETUP endpoint)
};
#endif

#ifdef CONSOLE_ENABLE
// console endpoint state structure
static USBInEndpointState console_ep_state;

// console endpoint initialization structure (IN)
static const USBEndpointConfig console_ep_config = {
  USB_EP_MODE_TYPE_INTR,        // Interrupt EP
  NULL,                         // SETUP packet notification callback
  console_in_cb,                // IN notification callback
  NULL,                         // OUT notification callback
  CONSOLE_SIZE,                 // IN maximum packet size
  0,                            // OUT maximum packet size
  &console_ep_state,            // IN Endpoint state
  NULL,                         // OUT endpoint state
  2,                            // IN multiplier
  NULL                          // SETUP buffer (not a SETUP endpoint)
};
#endif

#ifdef EXTRAKEY_ENABLE
// extrakey endpoint state structure
static USBInEndpointState extra_ep_state;

// extrakey endpoint initialization structure (IN)
static const USBEndpointConfig extra_ep_config = {
  USB_EP_MODE_TYPE_INTR,        // Interrupt EP
  NULL,                         // SETUP packet notification callback
  extra_in_cb,                  // IN notification callback
  NULL,                         // OUT notification callback
  EXTRA_SIZE,                   // IN maximum packet size
  0,                            // OUT maximum packet size
  &extra_ep_state,              // IN Endpoint state
  NULL,                         // OUT endpoint state
  2,                            // IN multiplier
  NULL                          // SETUP buffer (not a SETUP endpoint)
};
#endif

#ifdef NKRO_ENABLE
// nkro endpoint state structure
static USBInEndpointState nkro_ep_state;

// nkro endpoint initialization structure (IN)
static const USBEndpointConfig nkro_ep_config = {
  USB_EP_MODE_TYPE_INTR,        // Interrupt EP
  NULL,                         // SETUP packet notification callback
  nkro_in_cb,                   // IN notification callback
  NULL,                         // OUT notification callback
  NKRO_SIZE,                    // IN maximum packet size
  0,                            // OUT maximum packet size
  &nkro_ep_state,               // IN Endpoint state
  NULL,                         // OUT endpoint state
  2,                            // IN multiplier
  NULL                          // SETUP buffer (not a SETUP endpoint)
};
#endif


// Handles the USB driver global events
static void usb_event_cb(USBDriver * usbp, usbevent_t event) {
  switch(event) {
    case USB_EVENT_RESET:
      return;
    case USB_EVENT_ADDRESS:
      return;
    case USB_EVENT_CONFIGURED:
      osalSysLockFromISR();
      // Enable the endpoints specified into the configuration.
      usbInitEndpointI(usbp, KBD_ENDPOINT, &kbd_ep_config);
#ifdef MOUSE_ENABLE
      usbInitEndpointI(usbp, MOUSE_ENDPOINT, &mouse_ep_config);
#endif
#ifdef CONSOLE_ENABLE
      usbInitEndpointI(usbp, CONSOLE_ENDPOINT, &console_ep_config);
#endif
#ifdef EXTRAKEY_ENABLE
      usbInitEndpointI(usbp, EXTRA_ENDPOINT, &extra_ep_config);
#endif
#ifdef NKRO_ENABLE
      usbInitEndpointI(usbp, NKRO_ENDPOINT, &nkro_ep_config);
#endif
      osalSysUnlockFromISR();
      return;
    case USB_EVENT_SUSPEND:
      return;
    case USB_EVENT_WAKEUP:
      return;
    case USB_EVENT_STALLED:
      return;
  }
}

// Function used locally in os/hal/src/usb.c for getting descriptors
// need it here for HID descriptor
static uint16_t get_hword(uint8_t *p) {
  uint16_t hw;

  hw  = (uint16_t)*p++;
  hw |= (uint16_t)*p << 8U;
  return hw;
}

/*
Appendix G: HID Request Support Requirements

The following table enumerates the requests that need to be supported by various types of HID class devices.
Device type     GetReport   SetReport   GetIdle     SetIdle     GetProtocol SetProtocol
------------------------------------------------------------------------------------------
Boot Mouse      Required    Optional    Optional    Optional    Required    Required
Non-Boot Mouse  Required    Optional    Optional    Optional    Optional    Optional
Boot Keyboard   Required    Optional    Required    Required    Required    Required
Non-Boot Keybrd Required    Optional    Required    Required    Optional    Optional
Other Device    Required    Optional    Optional    Optional    Optional    Optional
*/

// Callback for SETUP request on the endpoint 0 (control)
static bool usb_request_hook_cb(USBDriver * usbp) {
  const USBDescriptor *dp;

  // usbp->setup fields:
  //  0:   bmRequestType (bitmask)
  //  1:   bRequest
  //  2,3: (LSB,MSB) wValue
  //  4,5: (LSB,MSB) wIndex
  //  6,7: (LSB,MSB) wLength (number of bytes to transfer if there is a data phase)

  // Handle HID class specific requests
  if(((usbp->setup[0] & USB_RTYPE_TYPE_MASK) == USB_RTYPE_TYPE_CLASS) &&
     ((usbp->setup[0] & USB_RTYPE_RECIPIENT_MASK ) == USB_RTYPE_RECIPIENT_INTERFACE)) {
    switch(usbp->setup[0] & USB_RTYPE_DIR_MASK) {
      case USB_RTYPE_DIR_DEV2HOST:
        switch(usbp->setup[1]) { // bRequest
          case HID_GET_REPORT:
            switch(usbp->setup[4]) { // LSB(wIndex) (check MSB==0?)
              case KBD_INTERFACE:
                usbSetupTransfer(usbp, (uint8_t *)&keyboard_report_sent, sizeof(keyboard_report_sent), NULL);
                break;
              // TODO: also got GET_REPORT for NKRO on linux
              default:
                usbSetupTransfer(usbp, NULL, 0, NULL);
                break;  
            }
            break;
          case HID_GET_PROTOCOL:
            if((usbp->setup[4] == KBD_INTERFACE) && (usbp->setup[5]==0)) { // wIndex
              usbSetupTransfer(usbp, &keyboard_protocol, 1, NULL);
            }
            break;
          case HID_GET_IDLE:
            usbSetupTransfer(usbp, &keyboard_idle, 1, NULL);
            break;
        }
        break;
      case USB_RTYPE_DIR_HOST2DEV:
        switch(usbp->setup[1]) { // bRequest
          case HID_SET_REPORT:
              switch(usbp->setup[4]) { // LSB(wIndex) (check MSB==0 and wLength==1?)
                case KBD_INTERFACE:
#ifdef NKRO_ENABLE
                case NKRO_INTERFACE:
#endif
                  // keyboard_led_stats = <read byte from next OUT report>
                  // the ep0out_cb is hardcoded into the USB driver
                  // but maybe this will work (without being notified of transfer)
                  // usbPrepareReceive(usbp, 0, &keyboard_led_stats, 1);
                  // osalSysLockFromISR();
                  // (void) usbStartReceiveI(usbp, 0);
                  // osalSysUnlockFromISR();
                break;
              }
            break;
          case HID_SET_PROTOCOL:
            if((usbp->setup[4] == KBD_INTERFACE) && (usbp->setup[5]==0)) { // wIndex
              keyboard_protocol = ((usbp->setup[2])!=0x00); // LSB(wValue)
#ifdef NKRO_ENABLE
              keyboard_nkro = !!keyboard_protocol;
#endif
            }
            break;
          case HID_SET_IDLE:
            keyboard_idle = usbp->setup[3]; // MSB(wValue)
            break;
        }
        break;
    }
  }

  // Handle the Get_Descriptor Request for HID class (not handled by the default hook)
  if((usbp->setup[0] == 0x81) && (usbp->setup[1] == USB_REQ_GET_DESCRIPTOR)) {
    dp = usbp->config->get_descriptor_cb(usbp, usbp->setup[3], usbp->setup[2], get_hword(&usbp->setup[4]));
    if(dp == NULL)
      return FALSE;
    usbSetupTransfer(usbp, (uint8_t *)dp->ud_string, dp->ud_size, NULL);
    return TRUE;
  }

  return FALSE;
}

// Start-of-frame callback
static void usb_sof_cb(USBDriver *usbp) {
  kbd_sof_cb(usbp);
}


// USB driver configuration
static const USBConfig usbcfg = {
  usb_event_cb,                 // USB events callback
  usb_get_descriptor_cb,        // Device GET_DESCRIPTOR request callback
  usb_request_hook_cb,          // Requests hook callback
  usb_sof_cb                    // Start Of Frame callback
};

/*
 * Initialize the USB driver
 */
void init_usb_driver(void) {
  /*
   * Activates the USB driver and then the USB bus pull-up on D+.
   * Note, a delay is inserted in order to not have to disconnect the cable
   * after a reset.
   */
  usbDisconnectBus(&USB_DRIVER);
  chThdSleepMilliseconds(1500);
  usbStart(&USB_DRIVER, &usbcfg);
  usbConnectBus(&USB_DRIVER);
}
