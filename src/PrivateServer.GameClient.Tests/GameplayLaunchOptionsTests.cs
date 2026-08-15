using Microsoft.VisualStudio.TestTools.UnitTesting;
using PrivateServer.GameClient.Gameplay.Flow;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GameplayLaunchOptionsTests
{
    [TestMethod]
    public void EmptyArgumentsKeepInteractiveChannelSelection()
    {
        Assert.IsTrue(GameplayLaunchOptions.TryParse(
            System.Array.Empty<string>(),
            out GameplayLaunchOptions options));
        Assert.IsNull(options.ObserverChannelId);
    }

    [TestMethod]
    public void ObserveChannelSelectsOnePositiveChannelId()
    {
        Assert.IsTrue(GameplayLaunchOptions.TryParse(
            new[] { "--observe-channel", "2" },
            out GameplayLaunchOptions options));
        Assert.AreEqual(2u, options.ObserverChannelId);
    }

    [TestMethod]
    [DataRow("--observe-channel")]
    [DataRow("--observe-channel", "0")]
    [DataRow("--observe-channel", "invalid")]
    [DataRow("--unknown", "1")]
    [DataRow("--observe-channel", "1", "--observe-channel", "2")]
    public void InvalidArgumentsAreRejected(params string[] arguments)
    {
        Assert.IsFalse(GameplayLaunchOptions.TryParse(
            arguments,
            out GameplayLaunchOptions _));
    }
}
