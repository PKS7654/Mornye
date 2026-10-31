#pragma once

#include <stdint.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>

/**
 * Linux ALSA USB-Audio Kernel (sound/usb/) Reference Definitions
 * Reference: linux/sound/usb/format.h, linux/sound/usb/card.h, linux/sound/usb/uac2.h
 */

// USB Device Classes and Subclasses
#define USB_CLASS_AUDIO                 0x01
#define USB_SUBCLASS_UNDEFINED          0x00
#define USB_SUBCLASS_AUDIOCONTROL       0x01
#define USB_SUBCLASS_AUDIOSTREAMING     0x02
#define USB_SUBCLASS_MIDISTREAMING      0x03

// Audio Class-Specific Descriptor Types
#define CS_UNDEFINED                    0x20
#define CS_DEVICE                       0x21
#define CS_CONFIGURATION                0x22
#define CS_STRING                       0x23
#define CS_INTERFACE                    0x24
#define CS_ENDPOINT                     0x25

// AudioControl Interface Descriptor Subtypes (UAC 1.0 & UAC 2.0)
#define UAC_HEADER                      0x01
#define UAC_INPUT_TERMINAL              0x02
#define UAC_OUTPUT_TERMINAL             0x03
#define UAC_MIXER_UNIT                  0x04
#define UAC_SELECTOR_UNIT               0x05
#define UAC_FEATURE_UNIT                0x06
#define UAC1_PROCESSING_UNIT            0x07
#define UAC1_EXTENSION_UNIT             0x08

// UAC 2.0 Specific AudioControl Entity Types
#define UAC2_CLOCK_SOURCE               0x0a
#define UAC2_CLOCK_SELECTOR             0x0b
#define UAC2_CLOCK_MULTIPLIER           0x0c
#define UAC2_SAMPLE_RATE_CONVERTER      0x0d

// AudioStreaming Interface Descriptor Subtypes
#define UAC_AS_GENERAL                  0x01
#define UAC_FORMAT_TYPE                 0x02
#define UAC_FORMAT_SPECIFIC             0x03

// Format Types
#define UAC_FORMAT_TYPE_I               0x01
#define UAC_FORMAT_TYPE_II              0x02
#define UAC_FORMAT_TYPE_III             0x03

// Format Type I Audio Data Format Codes
#define UAC_FORMAT_TYPE_I_UNDEFINED     0x0000
#define UAC_FORMAT_TYPE_I_PCM           0x0001
#define UAC_FORMAT_TYPE_I_PCM8          0x0002
#define UAC_FORMAT_TYPE_I_IEEE_FLOAT    0x0003
#define UAC_FORMAT_TYPE_I_ALAW          0x0004
#define UAC_FORMAT_TYPE_I_MULAW         0x0005

// Terminal Types (Audio Device Terminal Types)
#define UAC_TERMINAL_UNDEFINED          0x0100
#define UAC_TERMINAL_STREAMING          0x0101
#define UAC_OUTPUT_TERMINAL_SPEAKER     0x0301
#define UAC_OUTPUT_TERMINAL_HEADPHONES  0x0302

// Class-Specific Request Codes
#define UAC_SET_CUR                     0x01
#define UAC_GET_CUR                     0x81
#define UAC_SET_MIN                     0x02
#define UAC_GET_MIN                     0x82
#define UAC_SET_MAX                     0x03
#define UAC_GET_MAX                     0x83
#define UAC_SET_RES                     0x04
#define UAC_GET_RES                     0x84

// UAC 2.0 Request Codes
#define UAC2_CS_CUR                     0x01
#define UAC2_CS_RANGE                   0x02

// Feature Unit Control Selectors
#define UAC_FU_MUTE                     0x01
#define UAC_FU_VOLUME                   0x02
#define UAC_FU_BASS                     0x03
#define UAC_FU_MID                      0x04
#define UAC_FU_TREBLE                   0x05

// Endpoint Control Selectors (UAC 1.0)
#define UAC_EP_CS_SAMPLING_FREQ_CONTROL 0x01
#define UAC_EP_CS_PITCH_CONTROL         0x02

// Clock Source Control Selectors (UAC 2.0)
#define UAC2_CS_CONTROL_SAM_FREQ        0x01
#define UAC2_CS_CONTROL_CLOCK_VALID     0x02

// USB Request Types (bmRequestType)
#define USB_DIR_OUT                     0x00
#define USB_DIR_IN                      0x80
#define USB_TYPE_STANDARD               (0x00 << 5)
#define USB_TYPE_CLASS                  (0x01 << 5)
#define USB_TYPE_VENDOR                 (0x02 << 5)
#define USB_RECIP_DEVICE                0x00
#define USB_RECIP_INTERFACE             0x01
#define USB_RECIP_ENDPOINT              0x02
#define USB_RECIP_OTHER                 0x03

#pragma pack(push, 1)

// Standard USB Descriptor Header
struct usb_descriptor_header {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
};

// Standard USB Interface Descriptor
struct usb_interface_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
};

// Standard USB Endpoint Descriptor
struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
};

// UAC 1.0 AudioControl Header Descriptor
struct uac1_ac_header_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint16_t bcdADC;
    uint16_t wTotalLength;
    uint8_t  bInCollection;
    uint8_t  baInterfaceNr[1];
};

// UAC 2.0 AudioControl Header Descriptor
struct uac2_ac_header_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint16_t bcdADC;
    uint8_t  bCategory;
    uint16_t wTotalLength;
    uint8_t  bmControls;
};

// UAC 2.0 Clock Source Descriptor
struct uac2_clock_source_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bClockId;
    uint8_t  bmAttributes;
    uint8_t  bmControls;
    uint8_t  bAssocTerminal;
    uint8_t  iClockSource;
};

// UAC 1.0 AS Format Type I Descriptor (Continuous or Discrete)
struct uac1_format_type_i_discrete {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bFormatType;
    uint8_t  bNrChannels;
    uint8_t  bSubFrameSize;
    uint8_t  bBitResolution;
    uint8_t  bSamFreqType;     // 0 = continuous, >0 = discrete count
    uint8_t  tSamFreq[3][1];   // 3 bytes per sample rate
};

// UAC 2.0 AS Format Type I Descriptor
struct uac2_format_type_i_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bFormatType;
    uint8_t  bSubSlotSize;     // bytes per slot (2, 3, or 4)
    uint8_t  bBitResolution;   // bits (16, 24, 32)
};

#pragma pack(pop)
