using System.Reflection;

using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class ProjectWiringTests
{
    [TestMethod]
    public void GameClientProjectReferenceLoadsExpectedAssembly()
    {
        Assembly gameClientAssembly = typeof(global::Pri57NetworkRuntimeFixture).Assembly;

        Assert.AreEqual("PrivateServer.GameClient", gameClientAssembly.GetName().Name);
    }
}
