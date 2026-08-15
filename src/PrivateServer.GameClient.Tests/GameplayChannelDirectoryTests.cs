using Microsoft.VisualStudio.TestTools.UnitTesting;
using PrivateServer.GameClient.Gameplay.Flow;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GameplayChannelDirectoryTests
{
    [TestMethod]
    public void ParsesOrderedUniqueClientChannelOptions()
    {
        const string Json = """
            {
              "schema": "psnr.game_client.channels",
              "version": 1,
              "channels": [
                { "id": 1, "name": "Channel 1", "address": "127.0.0.1", "port": 27015 },
                { "id": 2, "name": "Channel 2", "address": "127.0.0.1", "port": 27016 }
              ]
            }
            """;

        Assert.IsTrue(GameplayChannelDirectory.TryParse(Json, out GameplayChannelDirectory? directory));
        Assert.IsNotNull(directory);
        Assert.AreEqual(2, directory.Channels.Count);
        Assert.AreEqual(1u, directory.Channels[0].Id);
        Assert.AreEqual((ushort)27016, checked((ushort)directory.Channels[1].Port));
    }

    [TestMethod]
    [DataRow("{\"schema\":\"wrong\",\"version\":1,\"channels\":[]}")]
    [DataRow("{\"schema\":\"psnr.game_client.channels\",\"version\":1,\"channels\":[]}")]
    [DataRow("{\"schema\":\"psnr.game_client.channels\",\"version\":1,\"channels\":[{\"id\":1,\"name\":\"One\",\"address\":\"127.0.0.1\",\"port\":27015},{\"id\":1,\"name\":\"Two\",\"address\":\"127.0.0.1\",\"port\":27016}]}")]
    [DataRow("{\"schema\":\"psnr.game_client.channels\",\"version\":1,\"channels\":[{\"id\":1,\"name\":\"One\",\"address\":\"127.0.0.1\",\"port\":27015},{\"id\":2,\"name\":\"Two\",\"address\":\"127.0.0.1\",\"port\":27015}]}")]
    public void RejectsInvalidOrDuplicateManifest(string json)
    {
        Assert.IsFalse(GameplayChannelDirectory.TryParse(json, out GameplayChannelDirectory? _));
    }
}
