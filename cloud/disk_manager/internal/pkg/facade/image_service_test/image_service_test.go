package tests

import (
	"context"
	"testing"
	"time"

	"github.com/golang/protobuf/ptypes/empty"
	"github.com/stretchr/testify/require"
	"github.com/ydb-platform/nbs/cloud/disk_manager/api"
	internal_client "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/client"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/facade/testcommon"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/services/disks"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/types"
	sdk_client "github.com/ydb-platform/nbs/cloud/disk_manager/pkg/client"
	"github.com/ydb-platform/nbs/cloud/disk_manager/pkg/client/codes"
	grpc_status "google.golang.org/grpc/status"
)

////////////////////////////////////////////////////////////////////////////////

var defaultEncryptionDescWithKey = &disk_manager.EncryptionDesc{
	Mode: disk_manager.EncryptionMode_ENCRYPTION_AES_XTS,
	Key: &disk_manager.EncryptionDesc_KmsKey{
		KmsKey: &disk_manager.KmsKey{
			KekId:        "kekid",
			EncryptedDek: []byte("encrypteddek"),
			TaskId:       "taskid",
		},
	},
}

////////////////////////////////////////////////////////////////////////////////

func checkEncryptedSource(
	t *testing.T,
	client sdk_client.Client,
	ctx context.Context,
	encryptedSource *disk_manager.CreateDiskRequest,
	diskSize int64,
	crc32 uint32,
	tag string,
) {

	diskID1 := t.Name() + "-good-disk-from-encrypted-" + tag

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src:  encryptedSource.Src,
		Size: diskSize,
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID1,
		},
		EncryptionDesc: &disk_manager.EncryptionDesc{
			Mode: disk_manager.EncryptionMode_ENCRYPTION_AES_XTS,
		},
	})

	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	nbsClient := testcommon.NewNbsClient(t, ctx, "zone")
	diskParams, err := nbsClient.Describe(ctx, diskID1)
	require.NoError(t, err)

	require.Equal(t, types.EncryptionMode_ENCRYPTION_AES_XTS, diskParams.EncryptionDesc.Mode)
	diskKeyHash, err := testcommon.GetEncryptionKeyHash(diskParams.EncryptionDesc)
	require.NoError(t, err)
	require.Equal(t, testcommon.DefaultKeyHash, diskKeyHash)

	encryption, err := disks.PrepareEncryptionDesc(defaultEncryptionDescWithKey)
	require.NoError(t, err)

	err = nbsClient.ValidateCrc32WithEncryption(diskID1, uint64(diskSize), encryption, crc32)
	require.NoError(t, err)

	diskID2 := t.Name() + "-bad-disk-from-encrypted-" + tag

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src:  encryptedSource.Src,
		Size: int64(diskSize),
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID2,
		},
	})

	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.Error(t, err)
	require.ErrorContains(t, err, "encryption mode should be the same")
}

func checkUnencryptedImage(
	t *testing.T,
	client sdk_client.Client,
	ctx context.Context,
	imageID string,
	diskSize int64,
	crc32 uint32,
) {

	diskID1 := t.Name() + "-good-encrypted-disk-from-image"

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcImageId{
			SrcImageId: imageID,
		},
		Size: diskSize,
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID1,
		},
		EncryptionDesc: defaultEncryptionDescWithKey,
	})

	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	nbsClient := testcommon.NewNbsClient(t, ctx, "zone")
	diskParams, err := nbsClient.Describe(ctx, diskID1)
	require.NoError(t, err)

	require.Equal(t, types.EncryptionMode_ENCRYPTION_AES_XTS, diskParams.EncryptionDesc.Mode)
	diskKeyHash, err := testcommon.GetEncryptionKeyHash(diskParams.EncryptionDesc)
	require.NoError(t, err)
	require.Equal(t, testcommon.DefaultKeyHash, diskKeyHash)

	encryption, err := disks.PrepareEncryptionDesc(defaultEncryptionDescWithKey)
	require.NoError(t, err)

	err = nbsClient.ValidateCrc32WithEncryption(diskID1, uint64(diskSize), encryption, crc32)
	require.NoError(t, err)

	diskID2 := t.Name() + "-bad-enrypted-disk-from-image"

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcImageId{
			SrcImageId: imageID,
		},
		Size: int64(diskSize),
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID2,
		},
		EncryptionDesc: &disk_manager.EncryptionDesc{
			Mode: disk_manager.EncryptionMode_ENCRYPTION_AES_XTS,
		},
	})

	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.Error(t, err)
	require.ErrorContains(t, err, "KeyPath should contain path to encryption key")
}

////////////////////////////////////////////////////////////////////////////////

func TestImageServiceCreateImageFromImage(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	imageID1 := t.Name() + "_image1"
	imageSize := uint64(40 * 1024 * 1024)

	imageCrc32, _ := testcommon.CreateImage(
		t,
		ctx,
		imageID1,
		imageSize,
		"folder",
		false, // pooled
	)

	imageID2 := t.Name() + "_image2"

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcImageId{
			SrcImageId: imageID1,
		},
		DstImageId: imageID2,
		FolderId:   "folder",
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)

	response := disk_manager.CreateImageResponse{}
	err = internal_client.WaitResponse(ctx, client, operation.Id, &response)
	require.NoError(t, err)
	require.Equal(t, int64(imageSize), response.Size)

	meta := disk_manager.CreateImageMetadata{}
	err = internal_client.GetOperationMetadata(ctx, client, operation.Id, &meta)
	require.NoError(t, err)
	require.Equal(t, float64(1), meta.Progress)

	diskID2 := t.Name() + "_disk2"
	diskSize := imageSize

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcImageId{
			SrcImageId: imageID2,
		},
		Size: int64(diskSize),
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID2,
		},
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	nbsClient := testcommon.NewNbsClient(t, ctx, "zone")

	err = nbsClient.ValidateCrc32(diskID2, imageSize, imageCrc32)
	require.NoError(t, err)

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation1, err := client.DeleteImage(reqCtx, &disk_manager.DeleteImageRequest{
		ImageId: imageID1,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation1)

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation2, err := client.DeleteImage(reqCtx, &disk_manager.DeleteImageRequest{
		ImageId: imageID2,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation2)

	err = internal_client.WaitOperation(ctx, client, operation1.Id)
	require.NoError(t, err)
	err = internal_client.WaitOperation(ctx, client, operation2.Id)
	require.NoError(t, err)

	testcommon.CheckConsistency(t, ctx)
}

func TestImageServiceCreateImageFromSnapshot(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	diskSize := uint64(10 * 1024 * 4096)
	diskID := t.Name() + "_disk"

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcEmpty{
			SrcEmpty: &empty.Empty{},
		},
		Size: int64(diskSize),
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID,
		},
		EncryptionDesc: &disk_manager.EncryptionDesc{
			Mode: disk_manager.EncryptionMode_ENCRYPTION_AES_XTS,
		},
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	nbsClient := testcommon.NewNbsClient(t, ctx, "zone")
	diskParams, err := nbsClient.Describe(ctx, diskID)
	require.NoError(t, err)

	require.Equal(t, types.EncryptionMode_ENCRYPTION_AES_XTS, diskParams.EncryptionDesc.Mode)
	diskKeyHash, err := testcommon.GetEncryptionKeyHash(diskParams.EncryptionDesc)
	require.NoError(t, err)
	require.Equal(t, []byte(nil), diskKeyHash)

	encryption, err := disks.PrepareEncryptionDesc(defaultEncryptionDescWithKey)
	require.NoError(t, err)

	crc32, _, err := testcommon.FillEncryptedDisk(nbsClient, diskID, diskSize, encryption)
	require.NoError(t, err)

	diskParams, err = nbsClient.Describe(ctx, diskID)
	require.NoError(t, err)

	require.Equal(t, types.EncryptionMode_ENCRYPTION_AES_XTS, diskParams.EncryptionDesc.Mode)
	diskKeyHash, err = testcommon.GetEncryptionKeyHash(diskParams.EncryptionDesc)
	require.NoError(t, err)
	require.Equal(t, testcommon.DefaultKeyHash, diskKeyHash)

	snapshotID := t.Name() + "_snapshot"

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateSnapshot(reqCtx, &disk_manager.CreateSnapshotRequest{
		Src: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID,
		},
		SnapshotId: snapshotID,
		FolderId:   "folder",
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)

	response := disk_manager.CreateSnapshotResponse{}
	err = internal_client.WaitResponse(ctx, client, operation.Id, &response)
	require.NoError(t, err)
	require.Equal(t, int64(diskSize), response.Size)

	snapshotSrc := &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcSnapshotId{
			SrcSnapshotId: snapshotID,
		},
	}
	checkEncryptedSource(t, client, ctx, snapshotSrc, int64(diskSize), crc32, "snapshot")

	imageID := t.Name() + "_image"

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcSnapshotId{
			SrcSnapshotId: snapshotID,
		},
		DstImageId: imageID,
		FolderId:   "folder",
		Pooled:     true,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)

	imageResponse := disk_manager.CreateImageResponse{}
	err = internal_client.WaitResponse(ctx, client, operation.Id, &imageResponse)
	require.NoError(t, err)
	require.Equal(t, int64(diskSize), imageResponse.Size)

	meta := disk_manager.CreateImageMetadata{}
	err = internal_client.GetOperationMetadata(ctx, client, operation.Id, &meta)
	require.NoError(t, err)
	require.Equal(t, float64(1), meta.Progress)

	imageSrc := &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcImageId{
			SrcImageId: imageID,
		},
	}
	checkEncryptedSource(t, client, ctx, imageSrc, int64(diskSize), crc32, "image")

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation1, err := client.DeleteImage(reqCtx, &disk_manager.DeleteImageRequest{
		ImageId: imageID,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation1)

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation2, err := client.DeleteSnapshot(reqCtx, &disk_manager.DeleteSnapshotRequest{
		SnapshotId: snapshotID,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation2)

	err = internal_client.WaitOperation(ctx, client, operation1.Id)
	require.NoError(t, err)
	err = internal_client.WaitOperation(ctx, client, operation2.Id)
	require.NoError(t, err)

	testcommon.CheckConsistency(t, ctx)
}

////////////////////////////////////////////////////////////////////////////////

func testCreateImageFromURL(
	t *testing.T,
	url string,
	imageSize uint64,
	diskCRC32 uint32,
) {

	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	imageID := t.Name()

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcUrl{
			SrcUrl: &disk_manager.ImageUrl{
				Url: url,
			},
		},
		DstImageId: imageID,
		FolderId:   "folder",
		Pooled:     true,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)

	imageResponse := disk_manager.CreateImageResponse{}
	err = internal_client.WaitResponse(ctx, client, operation.Id, &imageResponse)
	require.NoError(t, err)
	require.Equal(t, int64(imageSize), imageResponse.Size)

	meta := disk_manager.CreateImageMetadata{}
	err = internal_client.GetOperationMetadata(ctx, client, operation.Id, &meta)
	require.NoError(t, err)
	require.Equal(t, float64(1), meta.Progress)

	diskID := t.Name()
	diskSize := int64(imageSize)

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcImageId{
			SrcImageId: imageID,
		},
		Size: diskSize,
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID,
		},
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	nbsClient := testcommon.NewNbsClient(t, ctx, "zone")
	err = nbsClient.ValidateCrc32(
		diskID,
		imageSize,
		diskCRC32,
	)
	require.NoError(t, err)

	testcommon.CheckConsistency(t, ctx)
}

func testCreateRawImageFromURL(t *testing.T) {
	testCreateImageFromURL(
		t,
		testcommon.GetRawImageFileURL(),
		testcommon.GetRawImageSize(t),
		testcommon.GetRawImageCrc32(t),
	)
}

func testCreateQCOW2ImageFromURL(t *testing.T) {
	testCreateImageFromURL(
		t,
		testcommon.GetQCOW2ImageFileURL(),
		testcommon.GetQCOW2ImageSize(t),
		testcommon.GetQCOW2ImageCrc32(t),
	)
}

////////////////////////////////////////////////////////////////////////////////

func TestImageServiceCreateGeneratedVMDKImageFromURL(t *testing.T) {
	testCreateImageFromURL(
		t,
		testcommon.GetGeneratedVMDKImageFileURL(),
		testcommon.GetGeneratedVMDKImageSize(t),
		testcommon.GetGeneratedVMDKImageCrc32(t),
	)
}

////////////////////////////////////////////////////////////////////////////////

// Reproduces panic issue (NBS-4635).
func TestImageServiceCreateQCOW2PanicImageFromURL(t *testing.T) {
	testCreateImageFromURL(
		t,
		testcommon.GetQCOW2PanicImageFileURL(),
		testcommon.GetQCOW2PanicImageSize(t),
		testcommon.GetQCOW2PanicImageCrc32(t),
	)
}

////////////////////////////////////////////////////////////////////////////////

func TestImageServiceCancelCreateImageFromURL(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	imageID := t.Name()

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcUrl{
			SrcUrl: &disk_manager.ImageUrl{
				Url: testcommon.GetRawImageFileURL(),
			},
		},
		DstImageId: imageID,
		FolderId:   "folder",
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	testcommon.CancelOperation(t, ctx, client, operation.Id)

	testcommon.CheckConsistency(t, ctx)
}

////////////////////////////////////////////////////////////////////////////////

func TestImageServiceCreateRawImageFromURL(t *testing.T) {
	testCreateRawImageFromURL(t)
}

func TestImageServiceCreateQCOW2ImageFromURL(t *testing.T) {
	testCreateQCOW2ImageFromURL(t)
}

func TestImageServiceCreateQCOW2ImageFromURLWhichIsOverwrittenInProcess(
	t *testing.T,
) {

	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	imageID := t.Name()

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcUrl{
			SrcUrl: &disk_manager.ImageUrl{
				Url: testcommon.GetQCOW2ImageFileURL(),
			},
		},
		DstImageId: imageID,
		FolderId:   "folder",
		Pooled:     true,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)

	// Need to add some variance for better testing.
	testcommon.WaitForRandomDuration(1000*time.Millisecond, 2*time.Second)
	// Overwrites image URL contents.
	testcommon.UseOtherQCOW2ImageFile(t)

	imageResponse := disk_manager.CreateImageResponse{}
	err = internal_client.WaitResponse(ctx, client, operation.Id, &imageResponse)
	if err != nil {
		// TODO: remove this branch after NBS-4002.
		require.Contains(t, err.Error(), "wrong ETag")
		return
	}

	imageSize := testcommon.GetOtherQCOW2ImageSize(t)
	imageCrc32 := testcommon.GetOtherQCOW2ImageCrc32(t)

	if int64(imageSize) != imageResponse.Size {
		// Default image file is also allowed, image could have already been
		// created before we started using 'other image'.
		imageSize = testcommon.GetQCOW2ImageSize(t)
		imageCrc32 = testcommon.GetQCOW2ImageCrc32(t)

		require.Equal(t, int64(imageSize), imageResponse.Size)
	}

	diskID := t.Name()

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcImageId{
			SrcImageId: imageID,
		},
		Size: int64(imageSize),
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID,
		},
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	nbsClient := testcommon.NewNbsClient(t, ctx, "zone")

	err = nbsClient.ValidateCrc32(diskID, imageSize, imageCrc32)
	require.NoError(t, err)

	testcommon.CheckConsistency(t, ctx)
}

////////////////////////////////////////////////////////////////////////////////

func testShouldFailCreateImageFromImageFileURL(
	t *testing.T,
	imageFileURL string,
	errorDetailsMessage string,
) {

	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	imageID := t.Name()

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcUrl{
			SrcUrl: &disk_manager.ImageUrl{
				Url: imageFileURL,
			},
		},
		DstImageId: imageID,
		FolderId:   "folder",
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.Error(t, err)

	status, ok := grpc_status.FromError(err)
	require.True(t, ok)

	statusDetails := status.Details()
	require.Equal(t, 1, len(statusDetails))

	errorDetails, ok := statusDetails[0].(*disk_manager.ErrorDetails)
	require.True(t, ok)

	require.Equal(t, int64(codes.BadSource), errorDetails.Code)
	require.False(t, errorDetails.Internal)
	require.Equal(t, errorDetailsMessage, errorDetails.Message)

	testcommon.CheckConsistency(t, ctx)
}

func TestImageServiceShouldFailCreateImageFromNonExistentFile(t *testing.T) {
	testShouldFailCreateImageFromImageFileURL(
		t,
		testcommon.GetNonExistentImageFileURL(),
		"url source not found",
	)
}

func TestImageServiceShouldFailCreateQCOW2ImageFromInvalidFile(t *testing.T) {
	testShouldFailCreateImageFromImageFileURL(
		t,
		testcommon.GetInvalidQCOW2ImageFileURL(),
		"url source invalid",
	)
}

////////////////////////////////////////////////////////////////////////////////

func TestImageServiceFuzzCreateQCOW2ImageFromURL(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	imageID := t.Name()

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcUrl{
			SrcUrl: &disk_manager.ImageUrl{
				Url: testcommon.GetQCOW2FuzzingImageFileURL(),
			},
		},
		DstImageId: imageID,
		FolderId:   "folder",
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	_ = internal_client.WaitOperation(ctx, client, operation.Id)

	testcommon.CheckConsistency(t, ctx)
}

func TestImageServiceCancelCreateImageFromImage(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	imageID1 := t.Name() + "1"
	imageSize := uint64(64 * 1024 * 1024)

	_, _ = testcommon.CreateImage(
		t,
		ctx,
		imageID1,
		imageSize,
		"folder",
		false, // pooled
	)

	imageID2 := t.Name() + "2"

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcImageId{
			SrcImageId: imageID1,
		},
		DstImageId: imageID2,
		FolderId:   "folder",
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	testcommon.CancelOperation(t, ctx, client, operation.Id)

	testcommon.CheckConsistency(t, ctx)
}

func TestImageServiceCancelCreateImageFromSnapshot(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	diskID := t.Name()

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcEmpty{
			SrcEmpty: &empty.Empty{},
		},
		Size: 4194304,
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID,
		},
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	snapshotID := t.Name() + "_snapshot"

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateSnapshot(reqCtx, &disk_manager.CreateSnapshotRequest{
		Src: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID,
		},
		SnapshotId: snapshotID,
		FolderId:   "folder",
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	imageID := t.Name() + "_image"

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcSnapshotId{
			SrcSnapshotId: snapshotID,
		},
		DstImageId: imageID,
		FolderId:   "folder",
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	testcommon.CancelOperation(t, ctx, client, operation.Id)

	testcommon.CheckConsistency(t, ctx)
}

func TestImageServiceCreateImageFromDisk(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	diskID := t.Name()
	diskSize := uint64(4194304)

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcEmpty{
			SrcEmpty: &empty.Empty{},
		},
		Size: int64(diskSize),
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID,
		},
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	nbsClient := testcommon.NewNbsClient(t, ctx, "zone")
	crc32, _, err := testcommon.FillDisk(nbsClient, diskID, diskSize)
	require.NoError(t, err)

	imageID := t.Name()

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcDiskId{
			SrcDiskId: &disk_manager.DiskId{
				ZoneId: "zone",
				DiskId: diskID,
			},
		},
		DstImageId: imageID,
		FolderId:   "folder",
		Pooled:     true,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)

	response := disk_manager.CreateImageResponse{}
	err = internal_client.WaitResponse(ctx, client, operation.Id, &response)
	require.NoError(t, err)
	require.Equal(t, int64(diskSize), response.Size)

	meta := disk_manager.CreateImageMetadata{}
	err = internal_client.GetOperationMetadata(ctx, client, operation.Id, &meta)
	require.NoError(t, err)
	require.Equal(t, float64(1), meta.Progress)

	testcommon.RequireCheckpointsAreEmpty(t, ctx, diskID)

	checkUnencryptedImage(t, client, ctx, imageID, int64(diskSize), crc32)

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.DeleteImage(reqCtx, &disk_manager.DeleteImageRequest{
		ImageId: imageID,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	testcommon.CheckConsistency(t, ctx)
}

func TestImageServiceCancelCreateImageFromDisk(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	diskID := t.Name()

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcEmpty{
			SrcEmpty: &empty.Empty{},
		},
		Size: 4194304,
		Kind: disk_manager.DiskKind_DISK_KIND_SSD,
		DiskId: &disk_manager.DiskId{
			ZoneId: "zone",
			DiskId: diskID,
		},
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	imageID := t.Name()

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcDiskId{
			SrcDiskId: &disk_manager.DiskId{
				ZoneId: "zone",
				DiskId: diskID,
			},
		},
		DstImageId: imageID,
		FolderId:   "folder",
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	testcommon.CancelOperation(t, ctx, client, operation.Id)

	testcommon.CheckConsistency(t, ctx)
}

func TestImageServiceDeleteImage(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	imageID := t.Name()
	imageSize := uint64(40 * 1024 * 1024)

	_, _ = testcommon.CreateImage(
		t,
		ctx,
		imageID,
		imageSize,
		"folder",
		false, // pooled
	)

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.DeleteImage(reqCtx, &disk_manager.DeleteImageRequest{
		ImageId: imageID,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	testcommon.CheckConsistency(t, ctx)
}
