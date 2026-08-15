using PrivateServer.NetworkRuntime.Managed;
using System;
using System.Net;
using System.Net.Sockets;

namespace PrivateServer.GameClient.Gameplay.Flow;

internal readonly record struct GameplayChannelOption(
    uint Id,
    string Name,
    string Address,
    int Port)
{
    internal bool TryCreateEndpoint(out NetworkRuntimeIpv4Endpoint endpoint)
    {
        endpoint = default;
        if (Id == 0 ||
            string.IsNullOrWhiteSpace(Name) ||
            string.IsNullOrWhiteSpace(Address) ||
            Port is <= 0 or > ushort.MaxValue ||
            !IPAddress.TryParse(Address, out IPAddress? parsedAddress) ||
            parsedAddress.AddressFamily != AddressFamily.InterNetwork)
        {
            return false;
        }

        byte[] bytes = parsedAddress.GetAddressBytes();
        endpoint = new NetworkRuntimeIpv4Endpoint(
            bytes[0],
            bytes[1],
            bytes[2],
            bytes[3],
            checked((ushort)Port));
        return true;
    }
}
