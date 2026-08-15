using System;
using System.Collections.Generic;

namespace PrivateServer.GameClient.Gameplay.Replication;

internal enum ChunkGroupAcceptResult
{
    Buffered,
    Committed,
    Duplicate,
    Stale,
    InvalidGroup,
}

internal abstract class ChunkGroupAssembler<TChunk, TCommitted>
    where TChunk : class
    where TCommitted : class
{
    private PendingGroup? pending;
    private uint lastCommittedGroupId;

    internal ChunkGroupAcceptResult Accept(TChunk chunk, out TCommitted? committed)
    {
        ArgumentNullException.ThrowIfNull(chunk);
        committed = null;
        uint groupId = GroupId(chunk);
        if (groupId <= lastCommittedGroupId)
        {
            return ChunkGroupAcceptResult.Stale;
        }

        if (pending is null || groupId > pending.GroupId)
        {
            pending = new PendingGroup(groupId, ChunkCount(chunk), chunk);
        }
        else if (groupId < pending.GroupId)
        {
            return ChunkGroupAcceptResult.Stale;
        }
        else if (!MetadataEqual(pending.ReferenceChunk, chunk))
        {
            pending = null;
            return ChunkGroupAcceptResult.InvalidGroup;
        }

        int chunkIndex = ChunkIndex(chunk);
        TChunk? existing = pending.Chunks[chunkIndex];
        if (existing is not null)
        {
            if (ChunksEqual(existing, chunk))
            {
                return ChunkGroupAcceptResult.Duplicate;
            }
            pending = null;
            return ChunkGroupAcceptResult.InvalidGroup;
        }

        pending.Chunks[chunkIndex] = chunk;
        ++pending.ReceivedCount;
        if (pending.ReceivedCount != pending.Chunks.Length)
        {
            return ChunkGroupAcceptResult.Buffered;
        }

        List<TChunk> orderedChunks = new List<TChunk>(pending.Chunks.Length);
        for (int index = 0; index < pending.Chunks.Length; ++index)
        {
            orderedChunks.Add(pending.Chunks[index]!);
        }
        if (!TryCommit(orderedChunks, out committed) || committed is null)
        {
            pending = null;
            return ChunkGroupAcceptResult.InvalidGroup;
        }

        lastCommittedGroupId = pending.GroupId;
        pending = null;
        return ChunkGroupAcceptResult.Committed;
    }

    internal void Clear()
    {
        pending = null;
        lastCommittedGroupId = 0;
    }

    protected abstract uint GroupId(TChunk chunk);
    protected abstract ushort ChunkIndex(TChunk chunk);
    protected abstract ushort ChunkCount(TChunk chunk);
    protected abstract bool MetadataEqual(TChunk left, TChunk right);
    protected abstract bool ChunksEqual(TChunk left, TChunk right);
    protected abstract bool TryCommit(IReadOnlyList<TChunk> orderedChunks, out TCommitted? committed);

    private sealed class PendingGroup
    {
        internal PendingGroup(uint groupId, ushort chunkCount, TChunk referenceChunk)
        {
            GroupId = groupId;
            ReferenceChunk = referenceChunk;
            Chunks = new TChunk?[chunkCount];
        }

        internal uint GroupId { get; }
        internal TChunk ReferenceChunk { get; }
        internal TChunk?[] Chunks { get; }
        internal int ReceivedCount { get; set; }
    }
}
