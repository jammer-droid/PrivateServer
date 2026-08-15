using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace PrivateServer.GameClient.Gameplay.Flow;

internal sealed class GameplayChannelDirectory
{
    private const string ExpectedSchema = "psnr.game_client.channels";
    private const int ExpectedVersion = 1;

    private GameplayChannelDirectory(IReadOnlyList<GameplayChannelOption> channels)
    {
        Channels = channels;
    }

    internal IReadOnlyList<GameplayChannelOption> Channels { get; }

    internal static bool TryParse(string json, out GameplayChannelDirectory? directory)
    {
        directory = null;
        if (string.IsNullOrWhiteSpace(json))
        {
            return false;
        }

        try
        {
            ManifestDocument? document = JsonSerializer.Deserialize<ManifestDocument>(
                json);
            if (document is null ||
                document.Schema != ExpectedSchema ||
                document.Version != ExpectedVersion ||
                document.Channels is null ||
                document.Channels.Length == 0)
            {
                return false;
            }

            List<GameplayChannelOption> channels = new List<GameplayChannelOption>(document.Channels.Length);
            HashSet<uint> ids = new HashSet<uint>();
            HashSet<string> endpoints = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            for (int index = 0; index < document.Channels.Length; ++index)
            {
                ManifestChannel entry = document.Channels[index];
                GameplayChannelOption option = new GameplayChannelOption(
                    entry.Id,
                    entry.Name ?? string.Empty,
                    entry.Address ?? string.Empty,
                    entry.Port);
                if (!option.TryCreateEndpoint(out _) ||
                    !ids.Add(option.Id) ||
                    !endpoints.Add($"{option.Address}:{option.Port}"))
                {
                    return false;
                }
                channels.Add(option);
            }

            directory = new GameplayChannelDirectory(channels.AsReadOnly());
            return true;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    private sealed class ManifestDocument
    {
        [JsonPropertyName("schema")]
        public string? Schema { get; set; }

        [JsonPropertyName("version")]
        public int Version { get; set; }

        [JsonPropertyName("channels")]
        public ManifestChannel[]? Channels { get; set; }
    }

    private sealed class ManifestChannel
    {
        [JsonPropertyName("id")]
        public uint Id { get; set; }

        [JsonPropertyName("name")]
        public string? Name { get; set; }

        [JsonPropertyName("address")]
        public string? Address { get; set; }

        [JsonPropertyName("port")]
        public int Port { get; set; }
    }
}
