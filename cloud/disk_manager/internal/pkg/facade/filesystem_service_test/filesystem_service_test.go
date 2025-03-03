package tests

import (
	"testing"

	"github.com/stretchr/testify/require"
	"github.com/ydb-platform/nbs/cloud/disk_manager/api"
	internal_client "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/client"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/facade/testcommon"
)

////////////////////////////////////////////////////////////////////////////////

func TestFilesystemServiceCreateEmptyFilesystem(t *testing.T) {
	ctx := testcommon.NewContext()

	client, err := testcommon.NewClient(ctx)
	require.NoError(t, err)
	defer client.Close()

	filesystemID := t.Name()

	reqCtx := testcommon.GetRequestContext(t, ctx)
	operation, err := client.CreateFilesystem(reqCtx, &disk_manager.CreateFilesystemRequest{
		FilesystemId: &disk_manager.FilesystemId{
			ZoneId:       "zone",
			FilesystemId: filesystemID,
		},
		BlockSize:   4096,
		Size:        4096000,
		StorageKind: disk_manager.FilesystemStorageKind_FILESYSTEM_STORAGE_KIND_HDD,
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	reqCtx = testcommon.GetRequestContext(t, ctx)
	operation, err = client.DeleteFilesystem(reqCtx, &disk_manager.DeleteFilesystemRequest{
		FilesystemId: &disk_manager.FilesystemId{
			ZoneId:       "zone",
			FilesystemId: filesystemID,
		},
	})
	require.NoError(t, err)
	require.NotEmpty(t, operation)
	err = internal_client.WaitOperation(ctx, client, operation.Id)
	require.NoError(t, err)

	testcommon.CheckConsistency(t, ctx)
}
