using Microsoft.VisualStudio.TestTools.UnitTesting;
using PrivateServer.GameClient.Gameplay.Flow;
using PrivateServer.NetworkRuntime.Managed;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GameplayChannelOptionTests
{
    [TestMethod]
    public void CreatesIpv4EndpointFromConfiguredLocalChannel()
    {
        GameplayChannelOption option = new GameplayChannelOption(
            1,
            "Local Channel",
            "127.0.0.1",
            27015);

        Assert.IsTrue(option.TryCreateEndpoint(out NetworkRuntimeIpv4Endpoint endpoint));
        Assert.AreEqual((byte)127, endpoint.Address0);
        Assert.AreEqual((byte)0, endpoint.Address1);
        Assert.AreEqual((byte)0, endpoint.Address2);
        Assert.AreEqual((byte)1, endpoint.Address3);
        Assert.AreEqual((ushort)27015, endpoint.Port);
    }

    [TestMethod]
    [DataRow(0u, "Local", "127.0.0.1", 27015)]
    [DataRow(1u, "", "127.0.0.1", 27015)]
    [DataRow(1u, "Local", "localhost", 27015)]
    [DataRow(1u, "Local", "::1", 27015)]
    [DataRow(1u, "Local", "127.0.0.1", 0)]
    [DataRow(1u, "Local", "127.0.0.1", 65536)]
    public void RejectsInvalidChannelConfiguration(
        uint id,
        string name,
        string address,
        int port)
    {
        GameplayChannelOption option = new GameplayChannelOption(id, name, address, port);

        Assert.IsFalse(option.TryCreateEndpoint(out NetworkRuntimeIpv4Endpoint _));
    }
}
