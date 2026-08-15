using System;
using System.Collections.Generic;

using EntityStateBatch = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBatch;
using EntityStateBodySample = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBodySample;
using EntityStateRecord = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateRecord;

namespace PrivateServer.GameClient.Gameplay.Replication;

internal sealed record RemoteEntityStateGroup
{
    internal RemoteEntityStateGroup(
        uint serverTick,
        uint snapshotId,
        IReadOnlyList<EntityStateRecord> records)
    {
        ArgumentNullException.ThrowIfNull(records);
        EntityStateRecord[] ownedRecords = new EntityStateRecord[records.Count];
        for (int index = 0; index < records.Count; ++index)
        {
            EntityStateRecord record = records[index];
            ownedRecords[index] = new EntityStateRecord(
                record.EntityId,
                record.Generation,
                record.HeadPositionX,
                record.HeadPositionY,
                record.HeadingRadians,
                record.Diameter,
                record.GrowthPoint,
                record.BoostState,
                record.BodyTrailSamples);
        }

        ServerTick = serverTick;
        SnapshotId = snapshotId;
        Records = Array.AsReadOnly(ownedRecords);
    }

    internal uint ServerTick { get; }
    internal uint SnapshotId { get; }
    internal IReadOnlyList<EntityStateRecord> Records { get; }
}

internal sealed class RemoteEntityStateGroupAssembler : ChunkGroupAssembler<EntityStateBatch, RemoteEntityStateGroup>
{
    protected override uint GroupId(EntityStateBatch chunk) => chunk.SnapshotId;
    protected override ushort ChunkIndex(EntityStateBatch chunk) => chunk.ChunkIndex;
    protected override ushort ChunkCount(EntityStateBatch chunk) => chunk.ChunkCount;

    protected override bool TryCommit(
        IReadOnlyList<EntityStateBatch> orderedChunks,
        out RemoteEntityStateGroup? committed)
    {
        committed = null;
        List<EntityStateRecord> records = new List<EntityStateRecord>();
        HashSet<ClientWorldEntityKey> entityKeys = new HashSet<ClientWorldEntityKey>();
        for (int chunkIndex = 0; chunkIndex < orderedChunks.Count; ++chunkIndex)
        {
            EntityStateBatch chunk = orderedChunks[chunkIndex];
            for (int recordIndex = 0; recordIndex < chunk.Records.Count; ++recordIndex)
            {
                EntityStateRecord record = chunk.Records[recordIndex];
                ClientWorldEntityKey key = new ClientWorldEntityKey(record.EntityId, record.Generation);
                if (!entityKeys.Add(key))
                {
                    return false;
                }
                records.Add(record);
            }
        }

        EntityStateBatch first = orderedChunks[0];
        committed = new RemoteEntityStateGroup(first.ServerTick, first.SnapshotId, records);
        return true;
    }

    protected override bool ChunksEqual(EntityStateBatch left, EntityStateBatch right)
    {
        if (!MetadataEqual(left, right) || left.ChunkIndex != right.ChunkIndex ||
            left.Records.Count != right.Records.Count)
        {
            return false;
        }
        for (int recordIndex = 0; recordIndex < left.Records.Count; ++recordIndex)
        {
            EntityStateRecord leftRecord = left.Records[recordIndex];
            EntityStateRecord rightRecord = right.Records[recordIndex];
            if (leftRecord.EntityId != rightRecord.EntityId ||
                leftRecord.Generation != rightRecord.Generation ||
                leftRecord.HeadPositionX != rightRecord.HeadPositionX ||
                leftRecord.HeadPositionY != rightRecord.HeadPositionY ||
                leftRecord.HeadingRadians != rightRecord.HeadingRadians ||
                leftRecord.Diameter != rightRecord.Diameter ||
                leftRecord.GrowthPoint != rightRecord.GrowthPoint ||
                leftRecord.BoostState != rightRecord.BoostState ||
                leftRecord.BodyTrailSamples.Count != rightRecord.BodyTrailSamples.Count)
            {
                return false;
            }
            for (int sampleIndex = 0; sampleIndex < leftRecord.BodyTrailSamples.Count; ++sampleIndex)
            {
                EntityStateBodySample leftSample = leftRecord.BodyTrailSamples[sampleIndex];
                EntityStateBodySample rightSample = rightRecord.BodyTrailSamples[sampleIndex];
                if (leftSample != rightSample)
                {
                    return false;
                }
            }
        }
        return true;
    }

    protected override bool MetadataEqual(EntityStateBatch left, EntityStateBatch right)
    {
        return left.ServerTick == right.ServerTick &&
               left.SnapshotId == right.SnapshotId &&
               left.ChunkCount == right.ChunkCount;
    }

}
