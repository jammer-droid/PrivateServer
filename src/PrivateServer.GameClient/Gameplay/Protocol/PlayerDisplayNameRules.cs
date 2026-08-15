using System;
using System.Text;

namespace PrivateServer.GameClient.Gameplay.Protocol;

internal static class PlayerDisplayNameRules
{
    internal const int MaximumByteCount = 48;

    internal static bool IsValid(string? value)
    {
        if (value is null || value.Length > MaximumByteCount)
        {
            return false;
        }

        foreach (char character in value)
        {
            if (!IsAsciiLetterOrDigit(character))
            {
                return false;
            }
        }
        return true;
    }

    internal static bool IsValid(ReadOnlySpan<byte> value)
    {
        if (value.Length > MaximumByteCount)
        {
            return false;
        }

        foreach (byte character in value)
        {
            if (!IsAsciiLetterOrDigit(character))
            {
                return false;
            }
        }
        return true;
    }

    internal static void Write(string value, Span<byte> output)
    {
        for (int index = 0; index < value.Length; ++index)
        {
            output[index] = (byte)value[index];
        }
    }

    internal static string Decode(ReadOnlySpan<byte> value)
    {
        return Encoding.ASCII.GetString(value);
    }

    private static bool IsAsciiLetterOrDigit(int character)
    {
        return (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9');
    }
}
