#include <efi.h>
#include <efilib.h>
#ifndef TRACE
#define TRACE(status) //  LOG("Status: '%r', Function: '%a', File: '%a', Line: '%d'\r\n", status, __FUNCTION__, __FILE__, __LINE__)
#endif

 EFI_GUID gEfiUdp4ServiceBindingProtocolGuid = EFI_UDP4_SERVICE_BINDING_PROTOCOL;
 EFI_GUID gEfiUdp4ProtocolGuid = EFI_UDP4_PROTOCOL;

extern EFI_BOOT_SERVICES    *gBS;
extern EFI_RUNTIME_SERVICES *gRT;

 BOOLEAN gTransmitCompleteFlag = FALSE;
 BOOLEAN gReceiveCompleteFlag = FALSE;

/*
Configuration
*/
EFI_IPv4_ADDRESS gLocalAddress = {{ 10, 0, 2, 200 }};
 EFI_IPv4_ADDRESS gSubnetMask = {{ 255, 255, 255, 0 }};
 UINT16 gLocalPort = 0;

 EFI_IPv4_ADDRESS gRemoteAddress = {{ 10, 0, 2, 180 }};
 UINT16 gRemotePort = 4444;


 VOID 
EFIAPI 
TransmitEventCallback(EFI_EVENT   Event, void  *UserData)
{
    gTransmitCompleteFlag = TRUE;
}

 VOID
EFIAPI
ReceiveEventCallback(EFI_EVENT   Event, void  *UserData)
{
    gReceiveCompleteFlag = TRUE;
}

static EFI_STATUS
EFIAPI
WaitForFlag(BOOLEAN *Flag, EFI_UDP4  *Udp4Protocol, UINTN   Timeout)
{
    EFI_STATUS  Status;
    UINT8       LastSecond = 0xFF;
    UINT8       Timer = 0;
    EFI_TIME    CurrentTime;

    while (!*Flag && (Timeout == 0 || Timer < Timeout)) {
        if (Udp4Protocol) {
            uefi_call_wrapper(Udp4Protocol->Poll,1,
                Udp4Protocol);
        }

        // use gRT->GetTime to exit this loop
        Status = uefi_call_wrapper(gRT->GetTime,2,&CurrentTime, NULL);

        if (EFI_ERROR(Status)) {
            TRACE(Status);
            // Error handling
            return Status;
        }

        if (LastSecond != CurrentTime.Second) {
            LastSecond = CurrentTime.Second;
            Timer++;
        }
    }

    return *Flag ? EFI_SUCCESS : EFI_TIMEOUT;
}

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE        ImageHandle, EFI_SYSTEM_TABLE  *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    EFI_STATUS                      Status;
    
    EFI_UDP4_CONFIG_DATA            Udp4ConfigData;

    EFI_UDP4_COMPLETION_TOKEN       Udp4ReceiveCompletionToken;
    EFI_UDP4_COMPLETION_TOKEN       Udp4TansmitCompletionToken;
    EFI_UDP4_TRANSMIT_DATA          Udp4TransmitData;

    EFI_HANDLE                      Udp4ChildHandle = NULL;

    EFI_UDP4                        *Udp4Protocol = NULL;
    EFI_SERVICE_BINDING             *Udp4ServiceBindingProtocol = NULL;

    CHAR8                           TxBuffer[] = "Hello Server!";


    /*
    Step 1: Locate the corresponding Service Binding Protocol, if there is more then 1 network interface gBS->LocateHandleBuffer should be used
    */

    Status = uefi_call_wrapper(gBS->LocateProtocol,3,&gEfiUdp4ServiceBindingProtocolGuid,NULL,&Udp4ServiceBindingProtocol);

    if (EFI_ERROR(Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }

    /*
    Step 2: Create a new UDP4 instance
    */

    Status = uefi_call_wrapper(Udp4ServiceBindingProtocol->CreateChild,2,Udp4ServiceBindingProtocol,&Udp4ChildHandle);

    if (EFI_ERROR(Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }

    Status = uefi_call_wrapper(gBS->HandleProtocol,3,Udp4ChildHandle,&gEfiUdp4ProtocolGuid,&Udp4Protocol);

    if (EFI_ERROR(Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }

    /*
    Step 3: Prepare the UDP4 instance
    */

    Udp4ConfigData.AcceptBroadcast = FALSE;
    Udp4ConfigData.AcceptPromiscuous = FALSE;
    Udp4ConfigData.AcceptAnyPort = FALSE;
    Udp4ConfigData.AllowDuplicatePort = FALSE;

    Udp4ConfigData.TimeToLive = 16;
    Udp4ConfigData.TypeOfService = 0;
    Udp4ConfigData.DoNotFragment = TRUE;
    Udp4ConfigData.ReceiveTimeout = 0;
    Udp4ConfigData.TransmitTimeout = 0;

    // Change to TRUE and set the following fields to zero if DHCP is used
    Udp4ConfigData.UseDefaultAddress = FALSE;
    uefi_call_wrapper(gBS->CopyMem,3,&Udp4ConfigData.StationAddress, &gLocalAddress, sizeof(Udp4ConfigData.StationAddress));
    uefi_call_wrapper(gBS->CopyMem,3,&Udp4ConfigData.SubnetMask, &gSubnetMask, sizeof(Udp4ConfigData.SubnetMask));
    Udp4ConfigData.StationPort = gLocalPort;
    uefi_call_wrapper(gBS->CopyMem,3,&Udp4ConfigData.RemoteAddress, &gRemoteAddress, sizeof(Udp4ConfigData.RemoteAddress));
    Udp4ConfigData.RemotePort = gRemotePort;

    Status = uefi_call_wrapper(Udp4Protocol->Configure,2,Udp4Protocol,&Udp4ConfigData);

    if (EFI_ERROR(Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }

    /*
    Step 4: Send data and wait for completion
    */

    Udp4TansmitCompletionToken.Status = EFI_SUCCESS;
    Udp4TansmitCompletionToken.Event = NULL;

    Status = uefi_call_wrapper(gBS->CreateEvent,5,EVT_NOTIFY_SIGNAL,TPL_CALLBACK,TransmitEventCallback,NULL,&(Udp4TansmitCompletionToken.Event));

    if (EFI_ERROR(Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }
        
    Udp4TansmitCompletionToken.Packet.TxData = &Udp4TransmitData;

    Udp4TransmitData.UdpSessionData = NULL;
    uefi_call_wrapper(gBS->SetMem,3,&Udp4TransmitData.GatewayAddress, sizeof(Udp4TransmitData.GatewayAddress), 0x00);
    Udp4TransmitData.DataLength = sizeof(TxBuffer);
    Udp4TransmitData.FragmentCount = 1;
    Udp4TransmitData.FragmentTable[0].FragmentLength = Udp4TransmitData.DataLength;
    Udp4TransmitData.FragmentTable[0].FragmentBuffer = TxBuffer;

    gTransmitCompleteFlag = FALSE;

    

    Status = uefi_call_wrapper(Udp4Protocol->Transmit,2,Udp4Protocol,&Udp4TansmitCompletionToken);

    if (EFI_ERROR(Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }

    Status = WaitForFlag(
        &gTransmitCompleteFlag,
        Udp4Protocol,
        10);

    if (EFI_ERROR(Status)) {
        TRACE(EFI_TIMEOUT);
        // Error handling
        return EFI_TIMEOUT;
    }

    if (EFI_ERROR(Udp4TansmitCompletionToken.Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }

   
    /*
    Step 5: Receive data
    */
    
    Udp4ReceiveCompletionToken.Status = EFI_SUCCESS;
    Udp4ReceiveCompletionToken.Event = NULL;

    Status = uefi_call_wrapper(gBS->CreateEvent,5,EVT_NOTIFY_SIGNAL,TPL_CALLBACK,ReceiveEventCallback,NULL,&(Udp4ReceiveCompletionToken.Event));

    if (EFI_ERROR(Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }

    Udp4ReceiveCompletionToken.Packet.RxData = NULL;

    gReceiveCompleteFlag = FALSE;

    
    Status = uefi_call_wrapper(Udp4Protocol->Receive,2,Udp4Protocol,&Udp4ReceiveCompletionToken);

    if (EFI_ERROR(Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }

    Status = uefi_call_wrapper(WaitForFlag,3,&gReceiveCompleteFlag,Udp4Protocol,10);

    if (EFI_ERROR(Status)) {
        TRACE(EFI_TIMEOUT);
        // Error handling
        return EFI_TIMEOUT;
    }

    if (EFI_ERROR(Udp4ReceiveCompletionToken.Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }
    
    /*
    Step 6: Process received data
    
    
    if (
        Udp4ReceiveCompletionToken.Packet.RxData &&
        Udp4ReceiveCompletionToken.Packet.RxData->FragmentCount > 0 &&
        Udp4ReceiveCompletionToken.Packet.RxData->DataLength > 0) {

        LOG("Received '%a'.\r\n", 
            Udp4ReceiveCompletionToken.Packet.RxData->FragmentTable[0].FragmentBuffer);
    }
    else {
        LOG("Received an empty package.\r\n");
    }
    
    
    Step 7: Cleanup
    */

    if (
        Udp4ReceiveCompletionToken.Packet.RxData &&
        Udp4ReceiveCompletionToken.Packet.RxData->RecycleSignal) {

        Status = uefi_call_wrapper(gBS->SignalEvent,1,Udp4ReceiveCompletionToken.Packet.RxData->RecycleSignal);

        if (EFI_ERROR(Udp4ReceiveCompletionToken.Status)) {
            TRACE(Status);
            // Error handling
            return Status;
        }
    }

    Status = uefi_call_wrapper(Udp4ServiceBindingProtocol->DestroyChild,2,Udp4ServiceBindingProtocol,Udp4ChildHandle);

    if (EFI_ERROR(Udp4ReceiveCompletionToken.Status)) {
        TRACE(Status);
        // Error handling
        return Status;
    }

    return EFI_SUCCESS;
}