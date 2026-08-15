using System;
using System.Numerics;

namespace PrivateServer.GameClient.Gameplay.Remote;

internal sealed class MovementSendScheduler
{
    private bool started;
    private double intervalSeconds;
    private double nextDeadlineSeconds;

    internal void Start(double nowSeconds, uint tickRateHz)
    {
        if (!double.IsFinite(nowSeconds) || nowSeconds < 0.0)
        {
            throw new ArgumentOutOfRangeException(nameof(nowSeconds));
        }
        if (tickRateHz == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(tickRateHz));
        }

        intervalSeconds = 1.0 / tickRateHz;
        nextDeadlineSeconds = nowSeconds;
        started = true;
    }

    internal bool TryConsumeDeadline(double nowSeconds)
    {
        if (!started ||
            !double.IsFinite(nowSeconds) ||
            nowSeconds < nextDeadlineSeconds)
        {
            return false;
        }

        nextDeadlineSeconds = nowSeconds + intervalSeconds;
        return true;
    }

    internal void Reset()
    {
        started = false;
        intervalSeconds = 0.0;
        nextDeadlineSeconds = 0.0;
    }

    internal static bool TryNormalizeAndEncode(
        float inputX,
        float inputY,
        out Vector2 normalizedInput,
        out short encodedX,
        out short encodedY)
    {
        normalizedInput = Vector2.Zero;
        encodedX = 0;
        encodedY = 0;
        if (!float.IsFinite(inputX) || !float.IsFinite(inputY))
        {
            return false;
        }

        normalizedInput = new Vector2(inputX, inputY);
        if (normalizedInput.LengthSquared() > 1.0f)
        {
            normalizedInput = Vector2.Normalize(normalizedInput);
        }

        encodedX = EncodeAxis(normalizedInput.X);
        encodedY = EncodeAxis(normalizedInput.Y);
        return true;
    }

    private static short EncodeAxis(float axis)
    {
        float normalizedAxis = Math.Clamp(axis, -1.0f, 1.0f);
        float rounded = MathF.Round(
            normalizedAxis * short.MaxValue,
            MidpointRounding.AwayFromZero);
        return checked((short)rounded);
    }
}
