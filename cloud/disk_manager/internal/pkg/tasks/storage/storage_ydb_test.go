package storage

import (
	"context"
	"fmt"
	"os"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/stretchr/testify/mock"
	"github.com/stretchr/testify/require"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/logging"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/monitoring/metrics"
	metrics_mocks "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/monitoring/metrics/mocks"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/persistence"
	persistence_config "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/persistence/config"
	tasks_config "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks/config"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks/errors"
	grpc_codes "google.golang.org/grpc/codes"
)

////////////////////////////////////////////////////////////////////////////////

func newContext() context.Context {
	return logging.SetLogger(
		context.Background(),
		logging.NewStderrLogger(logging.DebugLevel),
	)
}

func newYDB(ctx context.Context) (*persistence.YDBClient, error) {
	endpoint := fmt.Sprintf(
		"localhost:%v",
		os.Getenv("DISK_MANAGER_RECIPE_KIKIMR_PORT"),
	)
	database := "/Root"
	rootPath := "disk_manager"

	return persistence.NewYDBClient(
		ctx,
		&persistence_config.PersistenceConfig{
			Endpoint: &endpoint,
			Database: &database,
			RootPath: &rootPath,
		},
		metrics.NewEmptyRegistry(),
	)
}

func newStorage(
	t *testing.T,
	ctx context.Context,
	db *persistence.YDBClient,
	config *tasks_config.TasksConfig,
	metricsRegistry metrics.Registry,
) (Storage, error) {

	folder := fmt.Sprintf("storage_ydb_test/%v", t.Name())
	config.StorageFolder = &folder

	err := CreateYDBTables(ctx, config, db, false /* dropUnusedColumns */)
	if err != nil {
		return nil, err
	}

	return NewStorage(config, metricsRegistry, db)
}

////////////////////////////////////////////////////////////////////////////////

var lastReqNumber int

func getIdempotencyKeyForTest(t *testing.T) string {
	lastReqNumber++
	return fmt.Sprintf("%v_%v", t.Name(), lastReqNumber)
}

////////////////////////////////////////////////////////////////////////////////

func TestStorageYDBCreateTask(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	taskState := TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      time.Now(),
		CreatedBy:      "some_user",
		ModifiedAt:     time.Now(),
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	}

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": taskState.TaskType},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, taskState)
	require.NoError(t, err)
	require.NotZero(t, taskID)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBCreateTaskIgnoresID(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	taskState := TaskState{
		ID:             "abc",
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      time.Now(),
		CreatedBy:      "some_user",
		ModifiedAt:     time.Now(),
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	}

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": taskState.TaskType},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, taskState)
	require.NoError(t, err)
	require.NotZero(t, taskID)
	require.NotEqual(t, taskID, taskState.ID)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBCreateTwoTasks(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	taskState := TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      time.Now(),
		CreatedBy:      "some_user",
		ModifiedAt:     time.Now(),
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	}

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": taskState.TaskType},
	).On("Add", int64(1)).Once()

	taskID1, err := storage.CreateTask(ctx, taskState)
	require.NoError(t, err)
	require.NotZero(t, taskID1)
	metricsRegistry.AssertAllExpectations(t)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": taskState.TaskType},
	).On("Add", int64(1)).Once()

	taskState.IdempotencyKey = getIdempotencyKeyForTest(t)
	taskID2, err := storage.CreateTask(ctx, taskState)
	require.NoError(t, err)
	require.NotZero(t, taskID2)
	require.NotEqual(t, taskID1, taskID2)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBCreateTwoTasksWithDifferentIdempotencyIDs(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	taskState := TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      time.Now(),
		CreatedBy:      "some_user",
		ModifiedAt:     time.Now(),
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	}

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": taskState.TaskType},
	).On("Add", int64(1)).Once()

	taskID1, err := storage.CreateTask(ctx, taskState)
	require.NoError(t, err)
	require.NotZero(t, taskID1)
	metricsRegistry.AssertAllExpectations(t)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": taskState.TaskType},
	).On("Add", int64(1)).Once()

	taskState.IdempotencyKey = getIdempotencyKeyForTest(t)
	taskID2, err := storage.CreateTask(ctx, taskState)
	require.NoError(t, err)
	require.NotZero(t, taskID2)
	metricsRegistry.AssertAllExpectations(t)

	require.NotEqual(t, taskID1, taskID2)
}

func TestStorageYDBCreateTwoTasksWithTheSameIdempotencyID(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	taskState := TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      time.Now(),
		CreatedBy:      "some_user",
		ModifiedAt:     time.Now(),
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	}

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": taskState.TaskType},
	).On("Add", int64(1)).Once()

	taskID1, err := storage.CreateTask(ctx, taskState)
	require.NoError(t, err)
	require.NotZero(t, taskID1)
	metricsRegistry.AssertAllExpectations(t)

	taskID2, err := storage.CreateTask(ctx, taskState)
	require.NoError(t, err)
	require.NotZero(t, taskID2)
	metricsRegistry.AssertAllExpectations(t)

	require.Equal(t, taskID1, taskID2)
}

func TestStorageYDBGetTask(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	createdAt := time.Now()
	modifiedAt := createdAt.Add(time.Hour)

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     modifiedAt,
		GenerationID:   42,
		Status:         TaskStatusReadyToRun,
		State:          []byte{1, 2, 3},
		Dependencies:   NewStringSet(),
		ZoneID:         "zone",
		CloudID:        "cloud",
		FolderID:       "folder",
	})
	require.NoError(t, err)

	taskState, err := storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskID, taskState.ID)
	require.EqualValues(t, "task1", taskState.TaskType)
	require.EqualValues(t, "Some task", taskState.Description)
	require.WithinDuration(t, time.Time(createdAt), time.Time(taskState.CreatedAt), time.Microsecond)
	require.EqualValues(t, "some_user", taskState.CreatedBy)
	require.WithinDuration(t, time.Time(modifiedAt), time.Time(taskState.ModifiedAt), time.Microsecond)
	require.EqualValues(t, 42, taskState.GenerationID)
	require.EqualValues(t, TaskStatusReadyToRun, taskState.Status)
	require.EqualValues(t, []byte{1, 2, 3}, taskState.State)
	require.EqualValues(t, NewStringSet(), taskState.Dependencies)
	require.WithinDuration(t, time.Time(createdAt), time.Time(taskState.ChangedStateAt), time.Microsecond)
	require.EqualValues(t, "zone", taskState.ZoneID)
	require.EqualValues(t, "cloud", taskState.CloudID)
	require.EqualValues(t, "folder", taskState.FolderID)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBGetTaskWithDependencies(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "dep_task"},
	).On("Add", int64(1)).Twice()
	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	createdAt := time.Now()
	modifiedAt := createdAt.Add(time.Hour)

	depID1, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "dep_task",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     modifiedAt,
		GenerationID:   42,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	depID2, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "dep_task",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     modifiedAt,
		GenerationID:   42,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     modifiedAt,
		GenerationID:   42,
		Status:         TaskStatusReadyToRun,
		State:          []byte{1, 2, 3},
		Dependencies:   NewStringSet(depID1, depID2),
	})
	require.NoError(t, err)

	taskState, err := storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskID, taskState.ID)
	require.EqualValues(t, "task1", taskState.TaskType)
	require.EqualValues(t, "Some task", taskState.Description)
	require.WithinDuration(t, time.Time(createdAt), time.Time(taskState.CreatedAt), time.Microsecond)
	require.EqualValues(t, "some_user", taskState.CreatedBy)
	require.WithinDuration(t, time.Time(modifiedAt), time.Time(taskState.ModifiedAt), time.Microsecond)
	require.EqualValues(t, 42, taskState.GenerationID)
	require.EqualValues(t, TaskStatusWaitingToRun, taskState.Status)
	require.EqualValues(t, []byte{1, 2, 3}, taskState.State)
	require.EqualValues(t, NewStringSet(depID1, depID2), taskState.Dependencies)
	require.WithinDuration(t, time.Time(createdAt), time.Time(taskState.ChangedStateAt), time.Microsecond)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBGetTaskMissing(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)

	taskState, err := storage.GetTask(ctx, "abc")
	require.Error(t, err, "Got %v", taskState)

	taskState, err = storage.GetTask(ctx, "")
	require.Error(t, err, "Got %v", taskState)
}

func TestStorageYDBListTasksReadyToRun(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	taskIDReadyToRun, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDReadyToRunStalling, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDFinished, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusFinished,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDReadyToCancel, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelled,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToRun, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	taskIDReadyToRunWaited, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToRun, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	expectedTaskInfos := []TaskInfo{
		TaskInfo{
			ID:           taskIDReadyToRun,
			GenerationID: generationID,
			TaskType:     "task1",
		},
		TaskInfo{
			ID:           taskIDReadyToRunStalling,
			GenerationID: generationID,
			TaskType:     "task1",
		},
		TaskInfo{
			ID:           taskIDReadyToRunWaited,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksReadyToRun(ctx, 100500, nil)
	require.NoError(t, err)
	// TODO: Maybe we need a proper order.
	require.ElementsMatch(t, expectedTaskInfos, taskInfos)
}

func TestStorageYDBListTasksReadyToCancel(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	taskIDReadyToRun, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDFinished, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusFinished,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDReadyToCancel, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDReadyToCancelStalling, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelled,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToRun, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToRun, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	taskIDReadyToCancelWaited, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	expectedTaskInfos := []TaskInfo{
		TaskInfo{
			ID:           taskIDReadyToCancel,
			GenerationID: generationID,
			TaskType:     "task1",
		},
		TaskInfo{
			ID:           taskIDReadyToCancelStalling,
			GenerationID: generationID,
			TaskType:     "task1",
		},
		TaskInfo{
			ID:           taskIDReadyToCancelWaited,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksReadyToCancel(ctx, 100500, nil)
	require.NoError(t, err)
	// TODO: Maybe we need a proper order.
	require.ElementsMatch(t, expectedTaskInfos, taskInfos)
}

func TestStorageYDBListTasksRunning(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDRunning, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDStallingWhileRunning, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusFinished,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelled,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	expectedTaskInfos := []TaskInfo{
		TaskInfo{
			ID:           taskIDRunning,
			GenerationID: generationID,
			TaskType:     "task1",
		},
		TaskInfo{
			ID:           taskIDStallingWhileRunning,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksRunning(ctx, 100500)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedTaskInfos, taskInfos)
}

func TestStorageYDBListTasksCancelling(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusFinished,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDCancelling, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDStallingWhileCancelling, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelled,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	expectedTaskInfos := []TaskInfo{
		TaskInfo{
			ID:           taskIDCancelling,
			GenerationID: generationID,
			TaskType:     "task1",
		},
		TaskInfo{
			ID:           taskIDStallingWhileCancelling,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksCancelling(ctx, 100500)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedTaskInfos, taskInfos)
}

func TestStorageYDBListFailedTasks(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(
		t,
		ctx,
		db,
		&tasks_config.TasksConfig{
			TaskStallingTimeout: &taskStallingTimeout,
		},
		metricsRegistry,
	)
	require.NoError(t, err)
	createdAt := time.Now().Add(-time.Hour)
	modifiedAt := createdAt.Add(2 * time.Hour)
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{0},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDWithSilentError, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{0},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	_, err = storage.UpdateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		ID:             taskIDWithSilentError,
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     modifiedAt,
		GenerationID:   generationID,
		Status:         TaskStatusCancelled,
		ErrorCode:      grpc_codes.InvalidArgument,
		ErrorMessage:   "invalid argument",
		ErrorSilent:    true,
		State:          []byte{1},
	})
	require.NoError(t, err)

	taskIDWithNonSilentError, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{0},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	_, err = storage.UpdateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		ID:             taskIDWithNonSilentError,
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     modifiedAt,
		GenerationID:   generationID,
		Status:         TaskStatusCancelled,
		ErrorCode:      grpc_codes.InvalidArgument,
		ErrorMessage:   "invalid argument",
		ErrorSilent:    false,
		State:          []byte{1},
	})
	require.NoError(t, err)

	taskInfos, err := storage.ListFailedTasks(
		ctx,
		modifiedAt.Add(-10*time.Minute),
	)
	require.NoError(t, err)
	require.ElementsMatch(
		t,
		[]TaskInfo{
			TaskInfo{
				ID:           taskIDWithNonSilentError,
				GenerationID: generationID + 1,
			},
		},
		taskInfos,
	)
}

func TestStorageYDBListSlowTasks(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(
		t,
		ctx,
		db,
		&tasks_config.TasksConfig{
			TaskStallingTimeout: &taskStallingTimeout,
		},
		metricsRegistry,
	)
	require.NoError(t, err)
	createdAt := time.Now().Add(-3 * time.Hour)
	var generationID uint64 = 1

	createTask := func(created time.Time, estimated, durationMinutes int) TaskInfo {
		task := TaskState{
			IdempotencyKey: getIdempotencyKeyForTest(t),
			TaskType:       "task1",
			Description:    "some task",
			CreatedAt:      created,
			CreatedBy:      "some_user",
			ModifiedAt:     created,
			GenerationID:   generationID,
			Status:         TaskStatusFinished,
			State:          []byte{0},
			Dependencies:   NewStringSet(),
			EndedAt:        created.Add(time.Duration(durationMinutes) * time.Minute),
			EstimatedTime:  created.Add(time.Duration(estimated) * time.Minute),
		}
		id, err := storage.CreateTask(ctx, task)
		generationID += 1
		require.NoError(t, err)
		return TaskInfo{
			ID:           id,
			GenerationID: task.GenerationID,
		}
	}
	createTask(createdAt, 5, 4) // Should not be presented in the result
	largeDelay := createTask(createdAt, 1, 61)
	mediumDelay := createTask(createdAt, 5, 64)
	smallDelay := createTask(createdAt, 60, 70)
	dayBefore := createTask(createdAt.Add(time.Duration(-24)*time.Hour), 5, 60)

	testCases := []struct {
		since        time.Time
		estimateMiss time.Duration
		expected     []TaskInfo
		comment      string
	}{
		{createdAt.Add(-10 * time.Minute),
			200 * time.Minute,
			[]TaskInfo{},
			"EstimateMiss is too big, no tasks expected",
		},
		{createdAt.Add(2 * time.Hour),
			10 * time.Minute,
			[]TaskInfo{},
			"No tasks expected",
		},
		{createdAt,
			10 * time.Minute,
			[]TaskInfo{largeDelay, mediumDelay, smallDelay},
			"Since limited, 3 tasks expected",
		},
		{createdAt.Add(-10 * time.Minute),
			1 * time.Hour,
			[]TaskInfo{largeDelay},
			"EstimateMiss limited, one task expected",
		},
		{createdAt.Add(-25 * time.Hour),
			10 * time.Minute,
			[]TaskInfo{largeDelay, mediumDelay, smallDelay, dayBefore},
			"All tasks fit, 4 expected",
		},
	}

	for _, tc := range testCases {
		taskInfos, err := storage.ListSlowTasks(ctx, tc.since, tc.estimateMiss)
		require.NoError(t, err)
		require.ElementsMatchf(t, tc.expected, taskInfos, tc.comment)
	}
}

func TestStorageYDBListTasksStallingWhileRunning(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	taskIDReadyToRun, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDRunStalling, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
	})
	require.NoError(t, err)

	taskIDFinished, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusFinished,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDReadyToCancel, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelled,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToRun, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	taskIDRunStallingWaited, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToRun, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	expectedTaskInfos := []TaskInfo{
		TaskInfo{
			ID:           taskIDRunStalling,
			GenerationID: generationID,
			TaskType:     "task1",
		},
		TaskInfo{
			ID:           taskIDRunStallingWaited,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksStallingWhileRunning(
		ctx,
		"current_host",
		100500,
		nil, // taskTypeWhitelist
	)
	require.NoError(t, err)
	// TODO: Maybe we need a proper order.
	require.ElementsMatch(t, expectedTaskInfos, taskInfos)
}

func TestStorageYDBListTasksStallingWhileCancelling(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	taskIDReadyToRun, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDFinished, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusFinished,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDReadyToCancel, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDCancelStalling, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelled,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToRun, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToRun, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	taskIDCancelStallingWaited, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(taskIDFinished, taskIDReadyToCancel),
	})
	require.NoError(t, err)

	expectedTaskInfos := []TaskInfo{
		TaskInfo{
			ID:           taskIDCancelStalling,
			GenerationID: generationID,
			TaskType:     "task1",
		},
		TaskInfo{
			ID:           taskIDCancelStallingWaited,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksStallingWhileCancelling(
		ctx,
		"current_host",
		100500,
		nil, // taskTypeWhitelist
	)
	require.NoError(t, err)
	// TODO: Maybe we need a proper order.
	require.ElementsMatch(t, expectedTaskInfos, taskInfos)
}

func TestStorageYDBListTasksReadyToRunWithWhitelist(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDReadyToRun2, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task2",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	expectedReadyToRunTaskInfos := []TaskInfo{
		{
			ID:           taskIDReadyToRun2,
			GenerationID: generationID,
			TaskType:     "task2",
		},
	}

	taskInfos, err := storage.ListTasksReadyToRun(
		ctx,
		100500,
		[]string{"task2"},
	)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedReadyToRunTaskInfos, taskInfos)
}

func TestStorageYDBListTasksReadyToCancelWithWhitelist(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDReadyToCancel2, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task2",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	expectedReadyToCancelTaskInfos := []TaskInfo{
		{
			ID:           taskIDReadyToCancel2,
			GenerationID: generationID,
			TaskType:     "task2",
		},
	}

	taskInfos, err := storage.ListTasksReadyToCancel(
		ctx,
		100500,
		[]string{"task2"},
	)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedReadyToCancelTaskInfos, taskInfos)
}

func TestStorageYDBListTasksStallingWhileRunningWithWhitelist(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
	})
	require.NoError(t, err)

	taskIDRunStalling2, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task2",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
	})
	require.NoError(t, err)

	expectedStallingWhileRunningTaskInfos := []TaskInfo{
		{
			ID:           taskIDRunStalling2,
			GenerationID: generationID,
			TaskType:     "task2",
		},
	}

	taskInfos, err := storage.ListTasksStallingWhileRunning(
		ctx,
		"current_host",
		100500,
		[]string{"task2"},
	)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedStallingWhileRunningTaskInfos, taskInfos)
}

func TestStorageYDBListTasksStallingWhileCancellingWithWhitelist(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
	})
	require.NoError(t, err)

	taskIDCancelStalling2, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task2",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
	})
	require.NoError(t, err)

	expectedStallingWhileCancellingTaskInfos := []TaskInfo{
		{
			ID:           taskIDCancelStalling2,
			GenerationID: generationID,
			TaskType:     "task2",
		},
	}

	taskInfos, err := storage.ListTasksStallingWhileCancelling(
		ctx,
		"current_host",
		100500,
		[]string{"task2"},
	)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedStallingWhileCancellingTaskInfos, taskInfos)
}

func TestStorageYDBListTasksReadyToRunInCertainZone(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	zoneID := "zone1"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
		ZoneIds:             []string{zoneID},
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		ZoneID:         "zone0",
	})
	require.NoError(t, err)

	taskIDReadyToRun, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		ZoneID:         "zone1",
	})
	require.NoError(t, err)

	expectedReadyToRunTaskInfos := []TaskInfo{
		{
			ID:           taskIDReadyToRun,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksReadyToRun(ctx, 100500, nil)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedReadyToRunTaskInfos, taskInfos)
}

func TestStorageYDBListTasksReadyToCancelInCertainZone(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	zoneID := "zone1"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
		ZoneIds:             []string{zoneID},
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	freshTime := createTime.Add(2 * time.Hour)
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		ZoneID:         "zone0",
	})
	require.NoError(t, err)

	taskIDReadyToCancel, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     freshTime,
		GenerationID:   generationID,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		ZoneID:         "zone1",
	})
	require.NoError(t, err)

	expectedReadyToCancelTaskInfos := []TaskInfo{
		{
			ID:           taskIDReadyToCancel,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksReadyToCancel(ctx, 100500, nil)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedReadyToCancelTaskInfos, taskInfos)
}

func TestStorageYDBListTasksStallingWhileRunningInCertainZone(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	zoneID := "zone1"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
		ZoneIds:             []string{zoneID},
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
		ZoneID:         "zone0",
	})
	require.NoError(t, err)

	taskIDRunStalling, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
		ZoneID:         "zone1",
	})
	require.NoError(t, err)

	expectedStallingWhileRunningTaskInfos := []TaskInfo{
		{
			ID:           taskIDRunStalling,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksStallingWhileRunning(
		ctx,
		"current_host",
		100500,
		nil, // taskTypeWhitelist
	)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedStallingWhileRunningTaskInfos, taskInfos)
}

func TestStorageYDBListTasksStallingWhileCancellingInCertainZone(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics.NewEmptyRegistry()

	taskStallingTimeout := "1s"
	zoneID := "zone1"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
		ZoneIds:             []string{zoneID},
	}, metricsRegistry)
	require.NoError(t, err)
	createTime := time.Now().Add(-time.Hour)
	stallTime := createTime
	var generationID uint64 = 42

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
		ZoneID:         "zone0",
	})
	require.NoError(t, err)

	taskIDCancelStalling, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createTime,
		CreatedBy:      "some_user",
		ModifiedAt:     stallTime,
		GenerationID:   generationID,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
		ZoneID:         "zone1",
	})
	require.NoError(t, err)

	expectedStallingWhileCancellingTaskInfos := []TaskInfo{
		{
			ID:           taskIDCancelStalling,
			GenerationID: generationID,
			TaskType:     "task1",
		},
	}

	taskInfos, err := storage.ListTasksStallingWhileCancelling(
		ctx,
		"current_host",
		100500,
		nil, // taskTypeWhitelist
	)
	require.NoError(t, err)
	require.ElementsMatch(t, expectedStallingWhileCancellingTaskInfos, taskInfos)
}

func TestStorageYDBLockTaskToRun(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastRunner:     "runner_42",
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Maybe().Panic("shouldn't create new task in LockTaskToRun")

	taskState, err := storage.LockTaskToRun(ctx, TaskInfo{
		ID:           taskID,
		GenerationID: 0,
	}, createdAt.Add(taskDuration), "host", "runner_43")
	require.NoError(t, err)
	require.EqualValues(t, taskState.GenerationID, 1)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBLockTaskToRunWrongGeneration(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   1,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	_, err = storage.LockTaskToRun(ctx, TaskInfo{
		ID:           taskID,
		GenerationID: 0,
	}, createdAt.Add(taskDuration), "host", "runner_42")
	require.Truef(t, errors.Is(err, errors.NewWrongGenerationError()), "Expected WrongGenerationError got %v", err)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBLockTaskToRunWrongState(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	_, err = storage.LockTaskToRun(ctx, TaskInfo{
		ID:           taskID,
		GenerationID: 0,
	}, createdAt.Add(taskDuration), "host", "runner_42")
	require.Error(t, err)
	require.Truef(t, !errors.Is(err, errors.NewWrongGenerationError()), "Unexpected WrongGenerationError")
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBLockTaskToCancel(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToCancel,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastRunner:     "runner_42",
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Maybe().Panic("shouldn't create new task in LockTaskToRun")

	taskState, err := storage.LockTaskToCancel(ctx, TaskInfo{
		ID:           taskID,
		GenerationID: 0,
	}, createdAt.Add(taskDuration), "host", "runner_43")
	require.NoError(t, err)
	require.EqualValues(t, taskState.GenerationID, 1)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBLockTaskToCancelWrongGeneration(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   1,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	_, err = storage.LockTaskToCancel(ctx, TaskInfo{
		ID:           taskID,
		GenerationID: 0,
	}, createdAt.Add(taskDuration), "host", "runner_42")
	require.Truef(t, errors.Is(err, errors.NewWrongGenerationError()), "Expected WrongGenerationError got %v", err)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBLockTaskToCancelWrongState(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	_, err = storage.LockTaskToCancel(ctx, TaskInfo{
		ID:           taskID,
		GenerationID: 0,
	}, createdAt.Add(taskDuration), "host", "runner_42")
	require.Error(t, err)
	require.Truef(t, !errors.Is(err, errors.NewWrongGenerationError()), "Unexpected WrongGenerationError")
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBMarkForCancellation(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastRunner:     "runner_42",
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	cancelling, err := storage.MarkForCancellation(ctx, taskID, createdAt.Add(taskDuration))
	require.NoError(t, err)
	require.True(t, cancelling)
	metricsRegistry.AssertAllExpectations(t)

	taskState, err := storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskState.GenerationID, 1)
	require.EqualValues(t, taskState.Status, TaskStatusReadyToCancel)
	require.EqualValues(t, taskState.ErrorCode, grpc_codes.Canceled)
	require.EqualValues(t, taskState.ErrorMessage, "Cancelled by client")
}

func TestStorageYDBMarkForCancellationAlready(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	cancelling, err := storage.MarkForCancellation(ctx, taskID, createdAt.Add(taskDuration))
	require.NoError(t, err)
	require.True(t, cancelling)

	metricsRegistry.AssertAllExpectations(t)

	taskState, err := storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskState.GenerationID, 0)
	require.Equal(t, taskState.Status, TaskStatusCancelling)
}

func TestStorageYDBMarkForCancellationFinished(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusFinished,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	cancelling, err := storage.MarkForCancellation(ctx, taskID, createdAt.Add(taskDuration))
	require.NoError(t, err)
	require.False(t, cancelling)
	metricsRegistry.AssertAllExpectations(t)

	taskState, err := storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskState.GenerationID, 0)
	require.Equal(t, taskState.Status, TaskStatusFinished)
}

func TestStorageYDBUpdateTask(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	modifiedAt := createdAt.Add(time.Minute)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Times(3)

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{0},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDDependent1, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	taskIDDependent2, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusFinished,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	_, err = storage.UpdateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		ID:             taskID,
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     modifiedAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		ErrorCode:      grpc_codes.InvalidArgument,
		ErrorMessage:   "invalid argument",
		State:          []byte{1},
		Dependencies:   NewStringSet(taskIDDependent1, taskIDDependent2),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	taskState, err := storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskState.Status, TaskStatusReadyToRun)
	require.EqualValues(t, taskState.GenerationID, 0)
	require.WithinDuration(t, time.Time(taskState.CreatedAt), time.Time(createdAt), time.Microsecond)
	require.WithinDuration(t, time.Time(taskState.ModifiedAt), time.Time(modifiedAt), time.Microsecond)
	require.EqualValues(t, taskState.ErrorCode, grpc_codes.InvalidArgument)
	require.EqualValues(t, taskState.ErrorMessage, "invalid argument")
	require.EqualValues(t, taskState.State, []byte{1})
	require.ElementsMatch(t, taskState.Dependencies.List(), []string{taskIDDependent1})
	require.WithinDuration(t, time.Time(taskState.ChangedStateAt), time.Time(createdAt), time.Microsecond)

	// Updating with the same stuff shouldn't change a thing.
	_, err = storage.UpdateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		ID:             taskID,
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     modifiedAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		ErrorCode:      grpc_codes.InvalidArgument,
		ErrorMessage:   "invalid argument",
		State:          []byte{1},
		Dependencies:   NewStringSet(taskIDDependent1, taskIDDependent2),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	taskState, err = storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskState.Status, TaskStatusReadyToRun)
	require.EqualValues(t, taskState.GenerationID, 0)
	require.WithinDuration(t, time.Time(taskState.CreatedAt), time.Time(createdAt), time.Microsecond)
	require.WithinDuration(t, time.Time(taskState.ModifiedAt), time.Time(modifiedAt), time.Microsecond)
	require.EqualValues(t, taskState.ErrorCode, grpc_codes.InvalidArgument)
	require.EqualValues(t, taskState.ErrorMessage, "invalid argument")
	require.EqualValues(t, taskState.State, []byte{1})
	require.ElementsMatch(t, taskState.Dependencies.List(), []string{taskIDDependent1})
	require.WithinDuration(t, time.Time(taskState.ChangedStateAt), time.Time(createdAt), time.Microsecond)
}

func TestStorageYDBUpdateTaskStatus(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	firstUpdatePause := time.Minute
	secondUpdatePause := time.Hour

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{0},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	_, err = storage.UpdateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		ID:             taskID,
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt.Add(firstUpdatePause),
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{1},
		Dependencies:   NewStringSet(),
		LastRunner:     "runner_42",
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	taskState, err := storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskState.GenerationID, 0)
	require.Equal(t, taskState.Status, TaskStatusReadyToRun)
	require.WithinDuration(t, time.Time(taskState.CreatedAt), time.Time(taskState.ChangedStateAt), time.Microsecond)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Maybe().Panic("shouldn't create new task in UpdateTask")

	_, err = storage.UpdateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		ID:             taskID,
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt.Add(firstUpdatePause + secondUpdatePause),
		GenerationID:   0,
		Status:         TaskStatusRunning,
		State:          []byte{2},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	taskState, err = storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskState.GenerationID, 1)
	require.Equal(t, taskState.Status, TaskStatusRunning)
	require.WithinDuration(t, time.Time(taskState.ModifiedAt), time.Time(taskState.ChangedStateAt), time.Microsecond)
}

func TestStorageYDBUpdateTaskWrongGeneration(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   2,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	_, err = storage.UpdateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		ID:             taskID,
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusRunning,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.Truef(t, errors.Is(err, errors.NewWrongGenerationError()), "Expected WrongGenerationError got %v", err)
	metricsRegistry.AssertAllExpectations(t)

	taskState, err := storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.EqualValues(t, taskState.GenerationID, 2)
	require.Equal(t, taskState.Status, TaskStatusReadyToRun)
}

func TestStorageYDBLockInParallel(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastRunner:     "runner_42",
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Maybe().Panic("shouldn't create new task in LockTaskToRun")

	var successCounter uint64
	var wg sync.WaitGroup

	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_, err := storage.LockTaskToRun(ctx, TaskInfo{
				ID:           taskID,
				GenerationID: 0,
			}, createdAt.Add(taskDuration), "host", "runner_43")
			if err == nil {
				atomic.AddUint64(&successCounter, 1)
			}
		}()
	}
	wg.Wait()
	require.EqualValues(t, successCounter, 1)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBMarkForCancellationInParallel(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastRunner:     "runner_42",
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	var successCounter uint64
	var wg sync.WaitGroup

	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_, err := storage.MarkForCancellation(ctx, taskID, createdAt.Add(taskDuration))
			if err == nil {
				atomic.AddUint64(&successCounter, 1)
			}
		}()
	}
	wg.Wait()
	require.EqualValues(t, 100, successCounter)
	metricsRegistry.AssertAllExpectations(t)
}

func TestStorageYDBLockAlreadyCancellingTask(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()
	taskDuration := time.Minute

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	var taskState TaskState
	taskState, err = storage.LockTaskToCancel(ctx, TaskInfo{
		ID:           taskID,
		GenerationID: 0,
	}, createdAt.Add(taskDuration), "host", "runner_42")
	require.NoError(t, err)
	require.EqualValues(t, taskState.GenerationID, 1)

	taskState, err = storage.LockTaskToCancel(ctx, TaskInfo{
		ID:           taskID,
		GenerationID: 0,
	}, createdAt.Add(taskDuration), "host", "runner_42")
	require.Truef(t, errors.Is(err, errors.NewWrongGenerationError()), "Expected WrongGenerationError got %v", err)
}

func TestStorageYDBCheckStallingTimeout(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	_, err = storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusCancelling,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastHost:       "other_host",
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	time.Sleep(time.Second)

	tasks, err := storage.ListTasksStallingWhileCancelling(
		ctx,
		"current_host",
		100500,
		nil, // taskTypeWhitelist
	)
	require.NoError(t, err)
	require.Equal(t, 1, len(tasks))
}

func TestStorageYDBCreateRegularTasks(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)

	createdAt := time.Now()
	task := TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		LastRunner:     "runner",
		LastHost:       "host",
	}

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": task.TaskType},
	).On("Add", int64(2)).Once()

	err = storage.CreateRegularTasks(ctx, task, time.Second, 2)
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	taskInfos, err := storage.ListTasksReadyToRun(ctx, 100500, nil)
	require.NoError(t, err)
	require.Equal(t, 2, len(taskInfos))

	task1, err := storage.GetTask(ctx, taskInfos[0].ID)
	require.NoError(t, err)
	require.Equal(t, task1.TaskType, "task")
	require.True(t, task1.Regular)

	task2, err := storage.GetTask(ctx, taskInfos[1].ID)
	require.NoError(t, err)
	require.Equal(t, task2.TaskType, "task")
	require.True(t, task2.Regular)

	// Check idempotency.
	err = storage.CreateRegularTasks(ctx, task, time.Second, 2)
	require.NoError(t, err)

	taskInfos, err = storage.ListTasksReadyToRun(ctx, 100500, nil)
	require.NoError(t, err)
	require.Equal(t, 2, len(taskInfos))

	// Rewind time and check that new tasks were not created.
	task.CreatedAt = createdAt.Add(2 * time.Second)
	err = storage.CreateRegularTasks(ctx, task, time.Second, 2)
	require.NoError(t, err)

	taskInfos, err = storage.ListTasksReadyToRun(ctx, 100500, nil)
	require.NoError(t, err)
	require.Equal(t, 2, len(taskInfos))

	// Finish first task and check that new tasks were not created.
	task1.Status = TaskStatusFinished
	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": task.TaskType},
	).On("Add", int64(1)).Maybe().Panic("shouldn't create new task in UpdateTask")
	metricsRegistry.GetTimer(
		"time/total",
		map[string]string{"type": task.TaskType},
	).On("RecordDuration", mock.Anything).Once()

	_, err = storage.UpdateTask(ctx, task1)
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	err = storage.CreateRegularTasks(ctx, task, time.Second, 2)
	require.NoError(t, err)

	taskInfos, err = storage.ListTasksReadyToRun(ctx, 100500, nil)
	require.NoError(t, err)
	require.Equal(t, 1, len(taskInfos))

	task.CreatedAt = createdAt

	// Finish second task and check that new tasks were not created.
	task2.Status = TaskStatusFinished
	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": task.TaskType},
	).On("Add", int64(1)).Maybe().Panic("shouldn't create new task in UpdateTask")
	metricsRegistry.GetTimer(
		"time/total",
		map[string]string{"type": task.TaskType},
	).On("RecordDuration", mock.Anything).Once()

	_, err = storage.UpdateTask(ctx, task2)
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	err = storage.CreateRegularTasks(ctx, task, time.Second, 2)
	require.NoError(t, err)

	taskInfos, err = storage.ListTasksReadyToRun(ctx, 100500, nil)
	require.NoError(t, err)
	require.Equal(t, 0, len(taskInfos))

	// Rewind time and check that new tasks were created.
	task.CreatedAt = createdAt.Add(time.Second + time.Millisecond)

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": task.TaskType},
	).On("Add", int64(2)).Once()

	err = storage.CreateRegularTasks(ctx, task, time.Second, 2)
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	taskInfos, err = storage.ListTasksReadyToRun(ctx, 100500, nil)
	require.NoError(t, err)
	require.Equal(t, 2, len(taskInfos))
}

func TestStorageYDBClearEndedTasks(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task"},
	).On("Add", int64(1)).Times(4)
	metricsRegistry.GetTimer(
		"time/total",
		map[string]string{"type": "task"},
	).On("RecordDuration", mock.Anything).Twice()

	taskID1, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{0},
		Dependencies:   NewStringSet(),
	})
	require.NoError(t, err)

	task := TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	}
	taskID2, err := storage.CreateTask(ctx, task)
	require.NoError(t, err)
	task.ID = taskID2

	task.Status = TaskStatusCancelled
	_, err = storage.UpdateTask(ctx, task)
	require.NoError(t, err)

	task = TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	}

	taskID3, err := storage.CreateTask(ctx, task)
	require.NoError(t, err)
	task.ID = taskID3

	task.Status = TaskStatusFinished
	_, err = storage.UpdateTask(ctx, task)
	require.NoError(t, err)

	endedBefore := createdAt.Add(time.Microsecond)
	createdAt = createdAt.Add(2 * time.Microsecond)

	task = TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
	}

	taskID4, err := storage.CreateTask(ctx, task)
	require.NoError(t, err)
	task.ID = taskID4

	task.Status = TaskStatusFinished
	_, err = storage.UpdateTask(ctx, task)
	require.NoError(t, err)

	metricsRegistry.AssertAllExpectations(t)

	err = storage.ClearEndedTasks(ctx, endedBefore, 100500)
	require.NoError(t, err)

	_, err = storage.GetTask(ctx, taskID1)
	require.NoError(t, err)

	_, err = storage.GetTask(ctx, taskID2)
	require.True(t, errors.Is(err, errors.NewEmptyNonRetriableError()))
	require.Contains(t, err.Error(), "No task")

	_, err = storage.GetTask(ctx, taskID3)
	require.True(t, errors.Is(err, errors.NewEmptyNonRetriableError()))
	require.Contains(t, err.Error(), "No task")

	_, err = storage.GetTask(ctx, taskID4)
	require.NoError(t, err)
}

func TestStorageYDBPauseResumeTask(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	metricsRegistry := metrics_mocks.NewRegistryMock()

	taskStallingTimeout := "1s"
	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{
		TaskStallingTimeout: &taskStallingTimeout,
	}, metricsRegistry)
	require.NoError(t, err)
	createdAt := time.Now()

	metricsRegistry.GetCounter(
		"created",
		map[string]string{"type": "task1"},
	).On("Add", int64(1)).Once()

	taskID, err := storage.CreateTask(ctx, TaskState{
		IdempotencyKey: getIdempotencyKeyForTest(t),
		TaskType:       "task1",
		Description:    "Some task",
		CreatedAt:      createdAt,
		CreatedBy:      "some_user",
		ModifiedAt:     createdAt,
		GenerationID:   0,
		Status:         TaskStatusReadyToRun,
		State:          []byte{},
		Dependencies:   NewStringSet(),
		LastRunner:     "runner_42",
	})
	require.NoError(t, err)
	metricsRegistry.AssertAllExpectations(t)

	err = storage.PauseTask(ctx, taskID)
	require.NoError(t, err)

	task, err := storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.Equal(t, task.Status, TaskStatusWaitingToRun)
	require.EqualValues(t, task.GenerationID, 1)

	// Check idempotency.
	err = storage.PauseTask(ctx, taskID)
	require.NoError(t, err)

	task, err = storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.Equal(t, task.Status, TaskStatusWaitingToRun)
	require.EqualValues(t, task.GenerationID, 1)

	err = storage.ResumeTask(ctx, taskID)
	require.NoError(t, err)

	// Check idempotency.
	err = storage.ResumeTask(ctx, taskID)
	require.NoError(t, err)

	task, err = storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.Equal(t, task.Status, TaskStatusReadyToRun)
	require.EqualValues(t, task.GenerationID, 2)

	task.Status = TaskStatusFinished
	metricsRegistry.GetTimer(
		"time/total",
		map[string]string{"type": task.TaskType},
	).On("RecordDuration", mock.Anything).Once()

	_, err = storage.UpdateTask(ctx, task)
	require.NoError(t, err)

	err = storage.PauseTask(ctx, taskID)
	require.Error(t, err)
	require.Contains(t, err.Error(), "invalid status")

	task, err = storage.GetTask(ctx, taskID)
	require.NoError(t, err)
	require.Equal(t, task.Status, TaskStatusFinished)
	require.EqualValues(t, task.GenerationID, 3)

	err = storage.ResumeTask(ctx, taskID)
	require.Error(t, err)
	require.Contains(t, err.Error(), "invalid status")

	metricsRegistry.AssertAllExpectations(t)
}

// Test that the first hearbeat adds the node to the table.
func TestHeartbeat(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{}, metrics.NewEmptyRegistry())
	require.NoError(t, err)

	err = storage.Heartbeat(ctx, "host-1", time.Now(), 1)
	require.NoError(t, err)

	node, err := storage.GetNode(ctx, "host-1")
	require.NoError(t, err)

	require.Equal(t, "host-1", node.Host)
	require.Equal(t, uint32(1), node.InflightTaskCount)
}

// Test that the following heartbeats overwrite the previous state.
func TestMultipleHeartbeats(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{}, metrics.NewEmptyRegistry())
	require.NoError(t, err)

	err = storage.Heartbeat(ctx, "host-1", time.Now(), 1)
	require.NoError(t, err)

	initialNode, err := storage.GetNode(ctx, "host-1")
	require.NoError(t, err)

	err = storage.Heartbeat(ctx, "host-1", time.Now(), 2)
	require.NoError(t, err)

	node, err := storage.GetNode(ctx, "host-1")
	require.NoError(t, err)
	require.Equal(t, "host-1", node.Host)
	require.Equal(t, uint32(2), node.InflightTaskCount)
	require.True(t, node.LastHeartbeat.After(initialNode.LastHeartbeat))
}

// Test fetching of the nodes that have sent heartbeats within the interval.
func TestGetAliveNodes(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	storage, err := newStorage(t, ctx, db, &tasks_config.TasksConfig{}, metrics.NewEmptyRegistry())
	require.NoError(t, err)

	referenceTime := time.Now().UTC()

	registerNodeInThePast := func(host string, offset time.Duration) {
		err = storage.Heartbeat(ctx, host, referenceTime.Add(-offset), 1)
		require.NoError(t, err)
	}

	registerNodeInThePast("host-1", 5*time.Second)
	registerNodeInThePast("host-2", 10*time.Second)
	registerNodeInThePast("host-3", 35*time.Second)

	nodes, err := storage.GetAliveNodes(ctx)
	require.NoError(t, err)

	// Reformat timestamp to get the same format.
	for idx := range nodes {
		nodes[idx].LastHeartbeat = nodes[idx].LastHeartbeat.UTC().Round(time.Second)
	}

	require.ElementsMatch(t,
		[]Node{
			{
				Host:              "host-1",
				LastHeartbeat:     referenceTime.Add(-5 * time.Second).Round(time.Second),
				InflightTaskCount: 1,
			},
			{
				Host:              "host-2",
				LastHeartbeat:     referenceTime.Add(-10 * time.Second).Round(time.Second),
				InflightTaskCount: 1,
			},
		},
		nodes,
	)
}
