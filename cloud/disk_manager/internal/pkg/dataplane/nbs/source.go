package nbs

import (
	"context"

	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/clients/nbs"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/common"
	dataplane_common "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/dataplane/common"
	task_errors "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks/errors"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/types"
)

////////////////////////////////////////////////////////////////////////////////

const (
	maxChangedByteCountPerIteration = uint64((1 << 20) * 4096)
)

////////////////////////////////////////////////////////////////////////////////

type diskSource struct {
	client           nbs.Client
	session          *nbs.Session
	diskID           string
	baseCheckpointID string
	checkpointID     string
	blocksInChunk    uint64
	blockCount       uint64
	chunkCount       uint32
	encryptionDesc   *types.EncryptionDesc

	useGetChangedBlocks              bool
	maxChangedBlockCountPerIteration uint64
	duplicateChunkIndices            bool

	chunkIndices           common.ChannelWithInflightQueue
	duplicatedChunkIndices common.ChannelWithCancellation

	ignoreBaseDisk bool
}

func (s *diskSource) sendChunkIndex(
	ctx context.Context,
	chunkIndex uint32,
) error {

	_, err := s.chunkIndices.Send(ctx, chunkIndex)
	if err != nil {
		return err
	}

	if !s.duplicatedChunkIndices.Empty() {
		_, err = s.duplicatedChunkIndices.Send(ctx, chunkIndex)
		return err
	}

	return nil
}

func (s *diskSource) generateChunkIndicesDefault(
	ctx context.Context,
	milestoneChunkIndex uint32,
) error {

	for chunkIndex := milestoneChunkIndex; chunkIndex < s.chunkCount; chunkIndex++ {
		err := s.sendChunkIndex(ctx, chunkIndex)
		if err != nil {
			return err
		}
	}

	return nil
}

func (s *diskSource) generateChunkIndices(
	ctx context.Context,
	milestoneChunkIndex uint32,
) error {

	totalBlockCount := s.blockCount

	blockIndex := uint64(milestoneChunkIndex) * s.blocksInChunk
	for blockIndex < totalBlockCount {
		blockCount := uint32(s.maxChangedBlockCountPerIteration)
		if uint64(blockCount) > totalBlockCount-blockIndex {
			blockCount = uint32(totalBlockCount - blockIndex)
		}

		blockMask, err := s.client.GetChangedBlocks(
			ctx,
			s.diskID,
			blockIndex,
			blockCount,
			s.baseCheckpointID,
			s.checkpointID,
			s.ignoreBaseDisk,
		)
		if err != nil {
			return err
		}

		chunkIndex := uint32(blockIndex / s.blocksInChunk)

		i := 0
		for i < len(blockMask) {
			// blocksInChunk should be multiple of 8.
			chunkEnd := i + int(s.blocksInChunk/8)

			for i < len(blockMask) && i < chunkEnd {
				if blockMask[i] != 0 {
					err := s.sendChunkIndex(ctx, chunkIndex)
					if err != nil {
						return err
					}

					i = chunkEnd
					break
				}

				i++
			}

			chunkIndex++
		}

		blockIndex += s.maxChangedBlockCountPerIteration
	}

	return nil
}

func (s *diskSource) ChunkIndices(
	ctx context.Context,
	milestone dataplane_common.Milestone,
	processedChunkIndices <-chan uint32,
	holeChunkIndices common.ChannelWithCancellation,
) (<-chan uint32, common.ChannelWithCancellation, <-chan error) {

	common.Assert(s.chunkIndices.Empty(), "should be called once")

	inflightLimit := cap(processedChunkIndices)

	s.chunkIndices = common.NewChannelWithInflightQueue(
		common.Milestone{
			Value:               milestone.ChunkIndex,
			ProcessedValueCount: milestone.TransferredChunkCount,
		},
		processedChunkIndices,
		holeChunkIndices,
		inflightLimit,
	)

	if s.duplicateChunkIndices {
		s.duplicatedChunkIndices = common.NewChannelWithCancellation(
			inflightLimit,
		)
	}

	errors := make(chan error, 1)

	go func() {
		defer close(errors)

		defer func() {
			if r := recover(); r != nil {
				errors <- task_errors.NewPanicError(r)
			}
		}()

		defer s.chunkIndices.Close()
		defer s.duplicatedChunkIndices.Close()

		var err error
		if s.useGetChangedBlocks {
			err = s.generateChunkIndices(ctx, milestone.ChunkIndex)
		} else {
			err = s.generateChunkIndicesDefault(ctx, milestone.ChunkIndex)
		}
		if err != nil {
			errors <- err
		}
	}()

	return s.chunkIndices.Channel(), s.duplicatedChunkIndices, errors
}

func (s *diskSource) Read(
	ctx context.Context,
	chunk *dataplane_common.Chunk,
) error {

	startIndex := uint64(chunk.Index) * s.blocksInChunk
	// blockCount should be multiple of blocksInChunk.

	return s.session.Read(
		ctx,
		startIndex,
		uint32(s.blocksInChunk),
		s.checkpointID,
		chunk.Data,
		&chunk.Zero,
	)
}

func (s *diskSource) Milestone() dataplane_common.Milestone {
	common.Assert(!s.chunkIndices.Empty(), "should not be empty")

	milestone := s.chunkIndices.Milestone()
	return dataplane_common.Milestone{
		ChunkIndex:            milestone.Value,
		TransferredChunkCount: milestone.ProcessedValueCount,
	}
}

func (s *diskSource) ChunkCount(ctx context.Context) (uint32, error) {
	return s.chunkCount, nil
}

func (s *diskSource) Close(ctx context.Context) {
	s.session.Close(ctx)
}

////////////////////////////////////////////////////////////////////////////////

// Data will be read from proxy disk.
func NewDiskSource(
	ctx context.Context,
	client nbs.Client,
	diskID string,
	proxyDiskID string,
	baseCheckpointID string,
	checkpointID string,
	encryption *types.EncryptionDesc,
	chunkSize uint32,
	duplicateChunkIndices bool,
	ignoreBaseDisk bool,
	useGetChangedBlocksForDiskRegistryBased bool,
) (dataplane_common.Source, error) {

	if len(proxyDiskID) == 0 {
		proxyDiskID = diskID
	}

	session, err := client.MountRO(ctx, proxyDiskID, encryption)
	if err != nil {
		return nil, err
	}

	blockSize := session.BlockSize()
	blockCount := session.BlockCount()

	err = validate(blockCount, chunkSize, blockSize)
	if err != nil {
		session.Close(ctx)
		return nil, err
	}

	blocksInChunk := uint64(chunkSize / blockSize)
	if blocksInChunk%8 != 0 {
		session.Close(ctx)

		return nil, task_errors.NewNonRetriableErrorf(
			"blocksInChunk should be multiple of 8, blocksInChunk=%v",
			blocksInChunk,
		)
	}

	maxChangedBlockCountPerIteration :=
		maxChangedByteCountPerIteration / uint64(blockSize)

	if maxChangedBlockCountPerIteration%blocksInChunk != 0 {
		session.Close(ctx)

		return nil, task_errors.NewNonRetriableErrorf(
			"maxChangedBlockCountPerIteration should be multiple of blocksInChunk, maxChangedBlockCountPerIteration=%v, blocksInChunk=%v",
			maxChangedBlockCountPerIteration,
			blocksInChunk,
		)
	}

	chunkCount := uint32(blockCount / blocksInChunk)

	// TODO: NBS-3228 make GetChangedBlocks() support for Disk Registry based disks.
	// This flag is still needed for snapshots from nrd disks because we can not use
	// GetChangedBlocks between non-light checkpoints for Disk Registry based disks.
	// But we can use GetChangedBlocks in disk relocation because we use
	// light checkpoints for relocation of Disk Registry based disks.
	useGetChangedBlocks :=
		!session.IsDiskRegistryBasedDisk() || useGetChangedBlocksForDiskRegistryBased

	return &diskSource{
		client:           client,
		session:          session,
		diskID:           diskID,
		baseCheckpointID: baseCheckpointID,
		checkpointID:     checkpointID,
		blocksInChunk:    blocksInChunk,
		blockCount:       blockCount,
		chunkCount:       chunkCount,
		encryptionDesc:   encryption,

		useGetChangedBlocks:              useGetChangedBlocks,
		maxChangedBlockCountPerIteration: maxChangedBlockCountPerIteration,
		duplicateChunkIndices:            duplicateChunkIndices,

		ignoreBaseDisk: ignoreBaseDisk,
	}, nil
}
