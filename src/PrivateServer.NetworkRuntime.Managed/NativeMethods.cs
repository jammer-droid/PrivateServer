using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PrivateServer.NetworkRuntime.Managed;

internal static class NativeMethods
{
    private const string CAbiLibrary = "PrivateServer.NetworkRuntime.CAbi";

#pragma warning disable CA2255 // Interop resolver must be registered before the first P/Invoke.
    [ModuleInitializer]
    internal static void InitializeNativeLibraryResolver()
    {
        // Managed assembly의 P/Invoke 수행 시 native DLL을 어디서 찾을지 결정하는 callback 등록
        NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, ResolveNativeLibrary);
    }
#pragma warning restore CA2255

    // native DLL 경로 탐색 후 Handle 반환
    private static IntPtr ResolveNativeLibrary(
        string libraryName,
        Assembly assembly,
        DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, CAbiLibrary, StringComparison.Ordinal))
        {
            return IntPtr.Zero; // 0을 반환하면 .NET 의 기본 DLL 탐색 진행
        }

        // Godot에서 Managed 어셈블리를 파일 경로 기반이 아니라 메모리로 로드해서 사용함
        //      - 메모리 로드: Assembly.Load(bytes) 형식으로 읽음
        //      - 파일 로드: Assembly.LoadFrom("AssemblyPath") 형식으로 읽음
        // 그래서 해당 어셈블리에 연결된 물리 파일 경로가 "" 으로 나옴(.NET 명세)
        // resolver에서 정확한 Native DLL 경로로 Load하여 사용
        // Native DLL이 의존하는 다른 DLL은 Windows Loader에서 import table을 확인하고 같이 Load

        // Godot 는 실행 중에 .NET Runtime 의 BaseDirectory 를 빌드 경로와 동일하게 설정함
        //  -> 해당 경로에 있는 Native DLL을 찾아 사용
        string libraryPath = Path.Combine(AppContext.BaseDirectory, $"{CAbiLibrary}.dll");
        return NativeLibrary.Load(libraryPath); // Windows DLL 로드 후 handle 반환
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct NativeStatus
    {
        internal readonly uint ErrorCode;
        internal readonly uint NativeErrorCode;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeClientConfig
    {
        internal uint EventQueueCapacity;
        internal uint PayloadQueueCapacity;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeIpv4Endpoint
    {
        internal byte Address0;
        internal byte Address1;
        internal byte Address2;
        internal byte Address3;
        internal ushort Port;
        internal ushort Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeByteView
    {
        internal IntPtr Data;
        internal uint Size;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeClientSnapshot
    {
        internal uint LifecycleState;
        internal uint Reserved;
        internal ulong PendingConnectIoCount;
        internal ulong PendingRecvIoCount;
        internal ulong PendingSendIoCount;
        internal ulong PendingIoCount;
        internal ulong EventQueueDepth;
        internal ulong EventQueueHighWatermark;
        internal ulong PendingSendQueueDepth;
        internal ulong PendingSendQueueHighWatermark;
    }

    // 함수 구현부는 DLL 에 존재. DLL 함수 이름과 동일한 이름 사용
    // CAbi DLL은 MSVC 기본 호출 규약 __cdecl 사용(/Gd)


    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeClientConfig psnr_client_config_default();

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_create(
        in NativeClientConfig config,
        out IntPtr outClient);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void psnr_client_destroy(IntPtr client);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_connect_ipv4(
        SafeClientHandle client,
        in NativeIpv4Endpoint endpoint);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_disconnect(SafeClientHandle client);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_shutdown(SafeClientHandle client);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_send(
        SafeClientHandle client,
        uint packetType,
        IntPtr payload,
        uint payloadSize);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_try_pop_event(
        SafeClientHandle client,
        out IntPtr outEvent);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_capture_snapshot(
        SafeClientHandle client,
        out NativeClientSnapshot outSnapshot);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void psnr_client_event_destroy(IntPtr clientEvent);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_event_get_kind(
        SafeClientEventHandle clientEvent,
        out uint outKind);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_event_get_packet_type(
        SafeClientEventHandle clientEvent,
        out uint outPacketType);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_event_get_payload(
        SafeClientEventHandle clientEvent,
        out NativeByteView outPayload);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_event_get_transport_status(
        SafeClientEventHandle clientEvent,
        out NativeStatus outStatus);

    [DllImport(CAbiLibrary, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeStatus psnr_client_event_get_disconnect_reason(
        SafeClientEventHandle clientEvent,
        out uint outReason);
}
