
#include <Uefi.h>

#include <Protocol/PciRootBridgeIo.h>

#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>

#define PCI_VENDOR_ID_OFFSET    0x00
#define PCI_COMMAND_OFFSET      0x04
#define PCI_CLASS_CODE_OFFSET  0x08
#define PCI_HEADER_TYPE_OFFSET 0x0E
#define PCI_BAR0_OFFSET        0x10

#define PCI_MAX_BUS       256
#define PCI_MAX_DEVICE     32
#define PCI_MAX_FUNCTION    8
#define PCI_CONFIG_DWORDS  16


STATIC
EFI_STATUS
ReadPciConfig (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *PciIo,
  IN  UINT8                           Bus,
  IN  UINT8                           Device,
  IN  UINT8                           Function,
  OUT UINT32                          *Config
  )
{
  UINT64 Address;

  Address = EFI_PCI_ADDRESS (
              Bus,
              Device,
              Function,
              0
              );

  return PciIo->Pci.Read (
                    PciIo,
                    EfiPciWidthUint32,
                    Address,
                    16,
                    Config
                    );
}


STATIC
VOID
PrintPciDevice (
  IN UINT8  Bus,
  IN UINT8  Device,
  IN UINT8  Function,
  IN UINT32 *Config
  )
{
  UINT16 VendorId;
  UINT16 DeviceId;
  UINT16 Command;
  UINT16 Status;
  UINT8  HeaderType;
  UINT8  RevisionId;
  UINT8  ProgIf;
  UINT8  SubClass;
  UINT8  BaseClass;
  UINTN  BarCount;
  UINTN  Index;

  VendorId = (UINT16)(Config[0] & 0xFFFF);
  DeviceId = (UINT16)(Config[0] >> 16);

  Command = (UINT16)(Config[1] & 0xFFFF);
  Status  = (UINT16)(Config[1] >> 16);

  RevisionId = (UINT8)(Config[2] & 0xFF);
  ProgIf     = (UINT8)((Config[2] >> 8) & 0xFF);
  SubClass   = (UINT8)((Config[2] >> 16) & 0xFF);
  BaseClass  = (UINT8)((Config[2] >> 24) & 0xFF);

  HeaderType = (UINT8)((Config[3] >> 16) & 0xFF);

  Print (
    L"\nBDF: %02x:%02x.%02x\n",
    Bus,
    Device,
    Function
    );

  Print (
    L"  VID:DID       = %04x:%04x\n",
    VendorId,
    DeviceId
    );

  Print (
    L"  Command       = 0x%04x\n",
    Command
    );

  Print (
    L"  Status        = 0x%04x\n",
    Status
    );

  Print (
    L"  Class Code    = %02x %02x %02x\n",
    BaseClass,
    SubClass,
    ProgIf
    );

  Print (
    L"  Revision ID   = 0x%02x\n",
    RevisionId
    );

  Print (
    L"  Header Type   = 0x%02x\n",
    HeaderType
    );

  // Header Type 0: PCI device; Type 1: PCI-to-PCI bridge.
  if ((HeaderType & 0x7F) == 0) {
    BarCount = 6;
  } else if ((HeaderType & 0x7F) == 1) {
    BarCount = 2;
  } else {
    BarCount = 0;
  }

  for (Index = 0; Index < BarCount; Index++) {
    Print (
      L"  BAR%u          = 0x%08x\n",
      (UINT32)Index,
      Config[(PCI_BAR0_OFFSET / sizeof (UINT32)) + Index]
      );
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            HandleCount;
  UINTN                            HandleIndex;
  UINTN                            Bus;
  UINTN                            Device;
  UINTN                            Function;
  UINT32                           Config[PCI_CONFIG_DWORDS];
  UINT32                           Function0Config[PCI_CONFIG_DWORDS];
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *PciIo;
  UINTN                            DeviceCount;

  Handles     = NULL;
  HandleCount = 0;
  DeviceCount = 0;

  Print (L"\n=== PCI Configuration Space Reader ===\n");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciRootBridgeIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );

  if (EFI_ERROR (Status)) {
    Print (
      L"LocateHandleBuffer failed: %r\n",
      Status
      );
    return Status;
  }

  Print (L"Root bridge handles: %u\n", (UINT32)HandleCount);

  for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
    Status = gBS->HandleProtocol (
                    Handles[HandleIndex],
                    &gEfiPciRootBridgeIoProtocolGuid,
                    (VOID **)&PciIo
                    );

    if (EFI_ERROR (Status)) {
      continue;
    }

    Print (L"\n--- Root Bridge %u ---\n", (UINT32)HandleIndex);

    for (Bus = 0; Bus < PCI_MAX_BUS; Bus++) {
      for (Device = 0; Device < PCI_MAX_DEVICE; Device++) {

        // Function 0 determines whether this device exists.
        Status = ReadPciConfig (
                   PciIo,
                   (UINT8)Bus,
                   (UINT8)Device,
                   0,
                   Function0Config
                   );

        if (EFI_ERROR (Status) ||
            (Function0Config[0] & 0xFFFF) == 0xFFFF) {
          continue;
        }

        for (Function = 0; Function < PCI_MAX_FUNCTION; Function++) {

          if (Function == 0) {
            CopyMem (Config, Function0Config, sizeof (Config));
          } else {
            // Scan additional functions only if function 0
            // advertises a multifunction device.
            if ((Function0Config[3] & 0x00800000) == 0) {
              break;
            }

            Status = ReadPciConfig (
                       PciIo,
                       (UINT8)Bus,
                       (UINT8)Device,
                       (UINT8)Function,
                       Config
                       );

            if (EFI_ERROR (Status) ||
                (Config[0] & 0xFFFF) == 0xFFFF) {
              continue;
            }
          }

          PrintPciDevice (
            (UINT8)Bus,
            (UINT8)Device,
            (UINT8)Function,
            Config
            );

          DeviceCount++;
        }
      }
    }
  }

  if (Handles != NULL) {
    gBS->FreePool (Handles);
  }

  Print (L"\nScan completed. Devices found: %u\n", (UINT32)DeviceCount);

  return EFI_SUCCESS;
}
