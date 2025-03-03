package testcommon

import (
	"bytes"
	"context"
	"fmt"
	"hash/crc32"
	"math/rand"
	"net/http"
	"os"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/golang/protobuf/ptypes/empty"
	"github.com/google/uuid"
	"github.com/stretchr/testify/require"
	"github.com/ydb-platform/nbs/cloud/disk_manager/api"
	operation_proto "github.com/ydb-platform/nbs/cloud/disk_manager/api/operation"
	internal_client "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/client"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/clients/nbs"
	nbs_config "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/clients/nbs/config"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/headers"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/logging"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/monitoring/metrics"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks/errors"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/types"
	sdk_client "github.com/ydb-platform/nbs/cloud/disk_manager/pkg/client"
	client_config "github.com/ydb-platform/nbs/cloud/disk_manager/pkg/client/config"
	"golang.org/x/sync/errgroup"
	"google.golang.org/grpc"
	grpc_codes "google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	grpc_status "google.golang.org/grpc/status"
)

////////////////////////////////////////////////////////////////////////////////

var (
	DefaultKeyHash = []byte{
		0x6c, 0x4d, 0x74, 0x64, 0x34, 0x38, 0x50, 0x35,
		0x6a, 0x45, 0x39, 0x32, 0x6f, 0x4d, 0x5a, 0x2f,
		0x4c, 0x67, 0x62, 0x57, 0x4d, 0x30, 0x30, 0x46,
		0x6e, 0x64, 0x33, 0x63, 0x4f, 0x49, 0x42, 0x32,
		0x54, 0x37, 0x69, 0x33, 0x42, 0x6f, 0x7a, 0x6d,
		0x76, 0x6f, 0x64, 0x39, 0x42, 0x44, 0x6c, 0x4b,
		0x4c, 0x62, 0x30, 0x5a, 0x4a, 0x59, 0x4d, 0x44,
		0x57, 0x6a, 0x35, 0x53, 0x43, 0x69, 0x2f, 0x51,
	}
)

////////////////////////////////////////////////////////////////////////////////

func newDefaultClientConfig() *client_config.Config {
	endpoint := fmt.Sprintf(
		"localhost:%v",
		os.Getenv("DISK_MANAGER_RECIPE_DISK_MANAGER_PORT"),
	)
	maxRetryAttempts := uint32(1000)
	timeout := "1s"

	return &client_config.Config{
		Endpoint:            &endpoint,
		MaxRetryAttempts:    &maxRetryAttempts,
		PerRetryTimeout:     &timeout,
		BackoffTimeout:      &timeout,
		OperationPollPeriod: &timeout,
	}
}

////////////////////////////////////////////////////////////////////////////////

func GetRawImageFileURL() string {
	port := os.Getenv("DISK_MANAGER_RECIPE_RAW_IMAGE_FILE_SERVER_PORT")
	return fmt.Sprintf("http://localhost:%v", port)
}

func GetRawImageSize(t *testing.T) uint64 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_RAW_IMAGE_SIZE"), 10, 64)
	require.NoError(t, err)
	return uint64(value)
}

func GetRawImageCrc32(t *testing.T) uint32 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_RAW_IMAGE_CRC32"), 10, 32)
	require.NoError(t, err)
	return uint32(value)
}

func GetNonExistentImageFileURL() string {
	port := os.Getenv("DISK_MANAGER_RECIPE_NON_EXISTENT_IMAGE_FILE_SERVER_PORT")
	return fmt.Sprintf("http://localhost:%v", port)
}

func GetQCOW2ImageFileURL() string {
	port := os.Getenv("DISK_MANAGER_RECIPE_QCOW2_IMAGE_FILE_SERVER_PORT")
	return fmt.Sprintf("http://localhost:%v", port)
}

func GetQCOW2ImageSize(t *testing.T) uint64 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_QCOW2_IMAGE_SIZE"), 10, 64)
	require.NoError(t, err)
	return uint64(value)
}

func GetQCOW2ImageCrc32(t *testing.T) uint32 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_QCOW2_IMAGE_CRC32"), 10, 32)
	require.NoError(t, err)
	return uint32(value)
}

func UseOtherQCOW2ImageFile(t *testing.T) {
	_, err := http.Get(GetQCOW2ImageFileURL() + "/use_other_image")
	require.NoError(t, err)
}

func GetOtherQCOW2ImageSize(t *testing.T) uint64 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_OTHER_QCOW2_IMAGE_SIZE"), 10, 64)
	require.NoError(t, err)
	return uint64(value)
}

func GetOtherQCOW2ImageCrc32(t *testing.T) uint32 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_OTHER_QCOW2_IMAGE_CRC32"), 10, 32)
	require.NoError(t, err)
	return uint32(value)
}

func GetInvalidQCOW2ImageFileURL() string {
	port := os.Getenv("DISK_MANAGER_RECIPE_INVALID_QCOW2_IMAGE_FILE_SERVER_PORT")
	return fmt.Sprintf("http://localhost:%v", port)
}

func GetQCOW2FuzzingImageFileURL() string {
	port := os.Getenv("DISK_MANAGER_RECIPE_QCOW2_FUZZING_IMAGE_FILE_SERVER_PORT")
	return fmt.Sprintf("http://localhost:%v", port)
}

func GetGeneratedVMDKImageFileURL() string {
	port := os.Getenv("DISK_MANAGER_RECIPE_GENERATED_VMDK_IMAGE_FILE_SERVER_PORT")
	return fmt.Sprintf("http://localhost:%v", port)
}

func GetGeneratedVMDKImageSize(t *testing.T) uint64 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_GENERATED_VMDK_IMAGE_SIZE"), 10, 64)
	require.NoError(t, err)
	return uint64(value)
}

func GetGeneratedVMDKImageCrc32(t *testing.T) uint32 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_GENERATED_VMDK_IMAGE_CRC32"), 10, 32)
	require.NoError(t, err)
	return uint32(value)
}

func GetQCOW2PanicImageFileURL() string {
	port := os.Getenv("DISK_MANAGER_RECIPE_QCOW2_PANIC_IMAGE_FILE_SERVER_PORT")
	return fmt.Sprintf("http://localhost:%v", port)
}

func GetQCOW2PanicImageSize(t *testing.T) uint64 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_QCOW2_PANIC_IMAGE_SIZE"), 10, 64)
	require.NoError(t, err)
	return uint64(value)
}

func GetQCOW2PanicImageCrc32(t *testing.T) uint32 {
	value, err := strconv.ParseUint(os.Getenv("DISK_MANAGER_RECIPE_QCOW2_PANIC_IMAGE_CRC32"), 10, 32)
	require.NoError(t, err)
	return uint32(value)
}

////////////////////////////////////////////////////////////////////////////////

func CancelOperation(
	t *testing.T,
	ctx context.Context,
	client sdk_client.Client,
	operationID string,
) {

	operation, err := client.CancelOperation(ctx, &disk_manager.CancelOperationRequest{
		OperationId: operationID,
	})
	require.NoError(t, err)
	require.Equal(t, operationID, operation.Id)
	require.True(t, operation.Done)

	switch result := operation.Result.(type) {
	case *operation_proto.Operation_Error:
		status := grpc_status.FromProto(result.Error)
		require.Equal(t, grpc_codes.Canceled, status.Code())
	case *operation_proto.Operation_Response:
	default:
		require.True(t, false)
	}
}

func NewClient(ctx context.Context) (sdk_client.Client, error) {
	creds, err := credentials.NewClientTLSFromFile(
		os.Getenv("DISK_MANAGER_RECIPE_ROOT_CERTS_FILE"),
		"",
	)
	if err != nil {
		return nil, err
	}

	return sdk_client.NewClient(
		ctx,
		newDefaultClientConfig(),
		grpc.WithTransportCredentials(creds),
	)
}

func CreatePrivateClient(ctx context.Context) (internal_client.PrivateClient, error) {
	creds, err := credentials.NewClientTLSFromFile(
		os.Getenv("DISK_MANAGER_RECIPE_ROOT_CERTS_FILE"),
		"",
	)
	if err != nil {
		return nil, err
	}

	return internal_client.NewPrivateClient(
		ctx,
		newDefaultClientConfig(),
		grpc.WithTransportCredentials(creds),
	)
}

func NewNbsClient(
	t *testing.T,
	ctx context.Context,
	zoneID string,
) nbs.Client {
	rootCertsFile := os.Getenv("DISK_MANAGER_RECIPE_ROOT_CERTS_FILE")

	durableClientTimeout := "5m"
	discoveryClientHardTimeout := "8m"
	discoveryClientSoftTimeout := "15s"

	factory, err := nbs.NewFactory(
		ctx,
		&nbs_config.ClientConfig{
			Zones: map[string]*nbs_config.Zone{
				"zone": {
					Endpoints: []string{
						fmt.Sprintf(
							"localhost:%v",
							os.Getenv("DISK_MANAGER_RECIPE_NBS_PORT"),
						),
					},
				},
				"other": {
					Endpoints: []string{
						fmt.Sprintf(
							"localhost:%v",
							os.Getenv("DISK_MANAGER_RECIPE_NBS2_PORT"),
						),
					},
				},
			},
			RootCertsFile:              &rootCertsFile,
			DurableClientTimeout:       &durableClientTimeout,
			DiscoveryClientHardTimeout: &discoveryClientHardTimeout,
			DiscoveryClientSoftTimeout: &discoveryClientSoftTimeout,
		},
		metrics.NewEmptyRegistry(),
		metrics.NewEmptyRegistry(),
	)
	require.NoError(t, err)

	client, err := factory.GetClient(ctx, zoneID)
	require.NoError(t, err)

	return client
}

////////////////////////////////////////////////////////////////////////////////

func NewContextWithToken(token string) context.Context {
	ctx := headers.SetOutgoingAccessToken(context.Background(), token)
	return logging.SetLogger(
		ctx,
		logging.NewStderrLogger(logging.DebugLevel),
	)
}

func NewContext() context.Context {
	return NewContextWithToken("TestToken")
}

////////////////////////////////////////////////////////////////////////////////

var lastReqNumber int

func GetRequestContext(t *testing.T, ctx context.Context) context.Context {
	lastReqNumber++

	cookie := fmt.Sprintf("%v_%v", t.Name(), lastReqNumber)
	ctx = headers.SetOutgoingIdempotencyKey(ctx, cookie)
	ctx = headers.SetOutgoingRequestID(ctx, cookie)
	return ctx
}

////////////////////////////////////////////////////////////////////////////////

func WaitForRandomDuration(min time.Duration, max time.Duration) {
	var duration time.Duration

	rand.Seed(time.Now().UnixNano())
	x := min.Microseconds()
	y := max.Microseconds()

	if y <= x {
		duration = min
	} else {
		duration = time.Duration(x+rand.Int63n(y-x)) * time.Microsecond
	}

	<-time.After(duration)
}

func RequireCheckpointsAreEmpty(
	t *testing.T,
	ctx context.Context,
	diskID string,
) {

	nbsClient := NewNbsClient(t, ctx, "zone")
	checkpoints, err := nbsClient.GetCheckpoints(ctx, diskID)
	require.NoError(t, err)
	require.Empty(t, checkpoints)
}

func WaitForCheckpointsAreEmpty(
	t *testing.T,
	ctx context.Context,
	diskID string,
) {

	nbsClient := NewNbsClient(t, ctx, "zone")

	for {
		checkpoints, err := nbsClient.GetCheckpoints(ctx, diskID)
		require.NoError(t, err)

		if len(checkpoints) == 0 {
			return
		}

		logging.Warn(
			ctx,
			"waitForCheckpointsAreEmpty proceeding to next iteration",
		)

		<-time.After(100 * time.Millisecond)
	}
}

////////////////////////////////////////////////////////////////////////////////

func FillDisk(
	nbsClient nbs.Client,
	diskID string,
	diskSize uint64,
) (uint32, uint64, error) {

	return FillEncryptedDisk(nbsClient, diskID, diskSize, nil)
}

func FillEncryptedDisk(
	nbsClient nbs.Client,
	diskID string,
	diskSize uint64,
	encryption *types.EncryptionDesc,
) (uint32, uint64, error) {

	ctx := NewContext()

	session, err := nbsClient.MountRW(
		ctx,
		diskID,
		0, // fillGeneration
		0, // fillSeqNumber
		encryption,
	)
	if err != nil {
		return 0, 0, err
	}
	defer session.Close(ctx)

	chunkSize := uint64(1024 * 4096) // 4 MiB
	blockSize := uint64(session.BlockSize())
	blocksInChunk := uint32(chunkSize / blockSize)
	acc := crc32.NewIEEE()
	storageSize := uint64(0)
	zeroes := make([]byte, chunkSize)

	rand.Seed(time.Now().UnixNano())

	for offset := uint64(0); offset < diskSize; offset += chunkSize {
		blockIndex := offset / blockSize
		data := make([]byte, chunkSize)
		dice := rand.Intn(3)

		var err error
		switch dice {
		case 0:
			rand.Read(data)
			if bytes.Equal(data, zeroes) {
				logging.Debug(ctx, "rand generated all zeroes")
			}

			err = session.Write(ctx, blockIndex, data)
			storageSize += chunkSize
		case 1:
			err = session.Zero(ctx, blockIndex, blocksInChunk)
		}
		if err != nil {
			return 0, 0, err
		}

		_, err = acc.Write(data)
		if err != nil {
			return 0, 0, err
		}
	}

	return acc.Sum32(), storageSize, nil
}

func GoWriteRandomBlocksToNbsDisk(
	ctx context.Context,
	nbsClient nbs.Client,
	diskID string,
) (func() error, error) {

	sessionCtx := NewContext()
	session, err := nbsClient.MountRW(
		sessionCtx,
		diskID,
		0,   // fillGeneration
		0,   // fillSeqNumber
		nil, // encryption
	)
	if err != nil {
		return nil, err
	}

	errGroup := new(errgroup.Group)

	errGroup.Go(func() error {
		defer session.Close(sessionCtx)

		writeCount := uint32(1000)

		blockSize := session.BlockSize()
		blocksCount := session.BlockCount()
		zeroes := make([]byte, blockSize)

		rand.Seed(time.Now().UnixNano())

		for i := uint32(0); i < writeCount; i++ {
			blockIndex := uint64(rand.Int63n(int64(blocksCount)))
			dice := rand.Intn(2)

			var err error

			switch dice {
			case 0:
				data := make([]byte, blockSize)
				rand.Read(data)
				if bytes.Equal(data, zeroes) {
					logging.Debug(ctx, "rand generated all zeroes")
				}

				err = session.Write(ctx, blockIndex, data)
			case 1:
				err = session.Zero(ctx, blockIndex, 1)
			}

			if err != nil {
				return err
			}
		}

		return nil
	})

	return errGroup.Wait, nil
}

////////////////////////////////////////////////////////////////////////////////

func CreateImage(
	t *testing.T,
	ctx context.Context,
	imageID string,
	imageSize uint64,
	folderID string,
	pooled bool,
) (crc32 uint32, storageSize uint64) {

	client, err := NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	diskID := "temporary_disk_for_image_" + imageID

	reqCtx := GetRequestContext(t, ctx)
	operation, err := client.CreateDisk(reqCtx, &disk_manager.CreateDiskRequest{
		Src: &disk_manager.CreateDiskRequest_SrcEmpty{
			SrcEmpty: &empty.Empty{},
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

	nbsClient := NewNbsClient(t, ctx, "zone")
	crc32, storageSize, err = FillDisk(nbsClient, diskID, imageSize)
	require.NoError(t, err)

	reqCtx = GetRequestContext(t, ctx)
	operation, err = client.CreateImage(reqCtx, &disk_manager.CreateImageRequest{
		Src: &disk_manager.CreateImageRequest_SrcDiskId{
			SrcDiskId: &disk_manager.DiskId{
				ZoneId: "zone",
				DiskId: diskID,
			},
		},
		DstImageId: imageID,
		FolderId:   folderID,
		Pooled:     pooled,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)

	response := disk_manager.CreateImageResponse{}
	err = internal_client.WaitResponse(ctx, client, operation.Id, &response)
	require.NoError(t, err)
	require.Equal(t, int64(imageSize), response.Size)
	require.Equal(t, int64(storageSize), response.StorageSize)

	return crc32, storageSize
}

////////////////////////////////////////////////////////////////////////////////

func isValidUUID(s string) bool {
	_, err := uuid.Parse(s)
	return err == nil
}

func CheckConsistency(t *testing.T, ctx context.Context) {
	nbsClient := NewNbsClient(t, ctx, "zone")

	for {
		ok := true

		diskIDs, err := nbsClient.List(ctx)
		require.NoError(t, err)

		for _, diskID := range diskIDs {
			// TODO: should remove dependency on disk id here.
			if strings.Contains(diskID, "proxy_") {
				ok = false

				logging.Info(
					ctx,
					"waiting for proxy overlay disk %v deletion",
					diskID,
				)
				continue
			}

			if isValidUUID(diskID) {
				// UUID v4 ids are used for base disks.
				// TODO: should use prefix 'base' for base disk ids (NBS-3860).
				continue
			}

			require.True(
				t,
				strings.Contains(diskID, "Test"),
				"disk with unexpected id is found: %v",
				diskID,
			)
		}

		if ok {
			break
		}

		time.Sleep(time.Second)
	}

	// TODO: validate internal YDB tables (tasks, pools etc.) consistency.
}

////////////////////////////////////////////////////////////////////////////////

func GetEncryptionKeyHash(encryptionDesc *types.EncryptionDesc) ([]byte, error) {
	switch key := encryptionDesc.Key.(type) {
	case *types.EncryptionDesc_KeyHash:
		return key.KeyHash, nil
	case nil:
		return nil, nil
	default:
		return nil, errors.NewNonRetriableErrorf("unknown key %s", key)
	}
}
