using System;
using System.Runtime.InteropServices;

namespace PrivateServer.NetworkRuntime.Managed;

internal sealed class SafeClientHandle : SafeHandle
{
    internal SafeClientHandle(IntPtr client)
        : base(IntPtr.Zero, true) // invalidHandle, ownsHandle = true
    {
        SetHandle(client); // Handle 설정
    }

    public override bool IsInvalid => handle == IntPtr.Zero;

    protected override bool ReleaseHandle() // SafeHandle이 정리될 때 호출됨
    {
        NativeMethods.psnr_client_destroy(handle);
        return true;
    }
}

internal sealed class SafeClientEventHandle : SafeHandle
{
    internal SafeClientEventHandle(IntPtr clientEvent)
        : base(IntPtr.Zero, true) // invalidHandle, ownsHandle = true
    {
        SetHandle(clientEvent);
    }

    public override bool IsInvalid => handle == IntPtr.Zero;

    protected override bool ReleaseHandle()
    {
        NativeMethods.psnr_client_event_destroy(handle);
        return true;
    }
}
