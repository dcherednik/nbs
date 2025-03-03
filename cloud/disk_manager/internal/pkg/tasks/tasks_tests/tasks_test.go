package tests

import (
	"context"
	"fmt"
	"os"
	"sync"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/golang/protobuf/ptypes/empty"
	"github.com/golang/protobuf/ptypes/wrappers"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/headers"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/logging"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/monitoring/metrics"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/persistence"
	persistence_config "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/persistence/config"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks"
	tasks_config "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks/config"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks/errors"
	tasks_storage "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks/storage"
	grpc_status "google.golang.org/grpc/status"
)

////////////////////////////////////////////////////////////////////////////////

const inflightLongTaskPerNodeLimit = 10

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
) tasks_storage.Storage {

	folder := fmt.Sprintf("tasks_ydb_test/%v", t.Name())
	config.StorageFolder = &folder

	err := tasks_storage.CreateYDBTables(
		ctx,
		config,
		db,
		false, // dropUnusedColums
	)
	require.NoError(t, err)

	storage, err := tasks_storage.NewStorage(config, metricsRegistry, db)
	require.NoError(t, err)

	return storage
}

////////////////////////////////////////////////////////////////////////////////

var lastReqNumber int

func getRequestContext(t *testing.T, ctx context.Context) context.Context {
	lastReqNumber++

	cookie := fmt.Sprintf("%v_%v", t.Name(), lastReqNumber)
	ctx = headers.SetIncomingIdempotencyKey(ctx, cookie)
	ctx = headers.SetIncomingRequestID(ctx, cookie)
	return ctx
}

func newDefaultConfig() *tasks_config.TasksConfig {
	pollForTaskUpdatesPeriod := "100ms"
	pollForTasksPeriodMin := "100ms"
	pollForTasksPeriodMax := "200ms"
	pollForStallingTasksPeriodMin := "100ms"
	pollForStallingTasksPeriodMax := "400ms"
	taskPingPeriod := "100ms"
	taskStallingTimeout := "1s"
	taskWaitingTimeout := "500ms"
	scheduleRegularTasksPeriodMin := "100ms"
	scheduleRegularTasksPeriodMax := "400ms"
	endedTaskExpirationTimeout := "300s"
	clearEndedTasksTaskScheduleInterval := "6s"
	clearEndedTasksLimit := uint64(10)
	maxRetriableErrorCount := uint64(2)
	hangingTaskTimeout := "100s"
	inflightTaskPerNodeLimits := map[string]int64{
		"long": inflightLongTaskPerNodeLimit,
	}

	return &tasks_config.TasksConfig{
		PollForTaskUpdatesPeriod:            &pollForTaskUpdatesPeriod,
		PollForTasksPeriodMin:               &pollForTasksPeriodMin,
		PollForTasksPeriodMax:               &pollForTasksPeriodMax,
		PollForStallingTasksPeriodMin:       &pollForStallingTasksPeriodMin,
		PollForStallingTasksPeriodMax:       &pollForStallingTasksPeriodMax,
		TaskPingPeriod:                      &taskPingPeriod,
		TaskStallingTimeout:                 &taskStallingTimeout,
		TaskWaitingTimeout:                  &taskWaitingTimeout,
		ScheduleRegularTasksPeriodMin:       &scheduleRegularTasksPeriodMin,
		ScheduleRegularTasksPeriodMax:       &scheduleRegularTasksPeriodMax,
		EndedTaskExpirationTimeout:          &endedTaskExpirationTimeout,
		ClearEndedTasksTaskScheduleInterval: &clearEndedTasksTaskScheduleInterval,
		ClearEndedTasksLimit:                &clearEndedTasksLimit,
		MaxRetriableErrorCount:              &maxRetriableErrorCount,
		HangingTaskTimeout:                  &hangingTaskTimeout,
		InflightTaskPerNodeLimits:           inflightTaskPerNodeLimits,
	}
}

////////////////////////////////////////////////////////////////////////////////

type services struct {
	config    *tasks_config.TasksConfig
	registry  *tasks.Registry
	scheduler tasks.Scheduler
	storage   tasks_storage.Storage
}

func createServicesWithConfig(
	t *testing.T,
	ctx context.Context,
	db *persistence.YDBClient,
	config *tasks_config.TasksConfig,
) services {

	registry := tasks.NewRegistry()

	storage := newStorage(
		t,
		ctx,
		db,
		config,
		metrics.NewEmptyRegistry(),
	)

	scheduler, err := tasks.NewScheduler(ctx, registry, storage, config)
	require.NoError(t, err)

	return services{
		config:    config,
		registry:  registry,
		scheduler: scheduler,
		storage:   storage,
	}
}

func createServices(
	t *testing.T,
	ctx context.Context,
	db *persistence.YDBClient,
	runnersCount uint64,
) services {

	config := proto.Clone(newDefaultConfig()).(*tasks_config.TasksConfig)
	config.RunnersCount = &runnersCount
	config.StalkingRunnersCount = &runnersCount
	return createServicesWithConfig(t, ctx, db, config)
}

func (s *services) startRunners(ctx context.Context) error {
	return tasks.StartRunners(
		ctx,
		s.storage,
		s.registry,
		metrics.NewEmptyRegistry(),
		s.config,
		"localhost",
	)
}

////////////////////////////////////////////////////////////////////////////////

type doublerTask struct {
	request *wrappers.UInt64Value
	state   *wrappers.UInt64Value
}

func (t *doublerTask) Save() ([]byte, error) {
	return proto.Marshal(t.state)
}

func (t *doublerTask) Load(request, state []byte) error {
	t.request = &wrappers.UInt64Value{}
	err := proto.Unmarshal(request, t.request)
	if err != nil {
		return err
	}

	t.state = &wrappers.UInt64Value{}
	return proto.Unmarshal(state, t.state)
}

func (t *doublerTask) Run(ctx context.Context, execCtx tasks.ExecutionContext) error {
	t.state.Value = 2 * t.request.Value
	return nil
}

func (t *doublerTask) Cancel(ctx context.Context, execCtx tasks.ExecutionContext) error {
	return nil
}

func (t *doublerTask) GetMetadata(
	ctx context.Context,
	taskID string,
) (proto.Message, error) {

	return &empty.Empty{}, nil
}

func (t *doublerTask) GetResponse() proto.Message {
	return t.state
}

func registerDoublerTask(registry *tasks.Registry) error {
	return registry.RegisterForExecution("doubler", func() tasks.Task {
		return &doublerTask{}
	})
}

func scheduleDoublerTask(
	ctx context.Context,
	scheduler tasks.Scheduler,
	request uint64,
) (string, error) {

	return scheduler.ScheduleTask(ctx, "doubler", "Doubler task", &wrappers.UInt64Value{
		Value: request,
	}, "", "")
}

////////////////////////////////////////////////////////////////////////////////

type longTask struct{}

func (t *longTask) Save() ([]byte, error) {
	return nil, nil
}

func (t *longTask) Load(request, state []byte) error {
	return nil
}

func (t *longTask) Run(ctx context.Context, execCtx tasks.ExecutionContext) error {
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-time.After(1 * time.Second):
		return nil
	}
}

func (t *longTask) Cancel(ctx context.Context, execCtx tasks.ExecutionContext) error {
	return nil
}

func (t *longTask) GetMetadata(
	ctx context.Context,
	taskID string,
) (proto.Message, error) {

	return &empty.Empty{}, nil
}

func (t *longTask) GetResponse() proto.Message {
	return &wrappers.UInt64Value{
		Value: 1,
	}
}

func registerLongTask(registry *tasks.Registry) error {
	return registry.RegisterForExecution("long", func() tasks.Task {
		return &longTask{}
	})
}

func scheduleLongTask(
	ctx context.Context,
	scheduler tasks.Scheduler,
) (string, error) {

	return scheduler.ScheduleTask(ctx, "long", "Long task", &empty.Empty{}, "", "")
}

////////////////////////////////////////////////////////////////////////////////

// Fails exactly n times in a row.
type unstableTask struct {
	// Represents 'number of failures until success'.
	request *wrappers.UInt64Value
	// Represents 'current number of failures'.
	state *wrappers.UInt64Value
}

func (t *unstableTask) Save() ([]byte, error) {
	return proto.Marshal(t.state)
}

func (t *unstableTask) Load(request, state []byte) error {

	t.request = &wrappers.UInt64Value{}
	err := proto.Unmarshal(request, t.request)
	if err != nil {
		return err
	}

	t.state = &wrappers.UInt64Value{}
	return proto.Unmarshal(state, t.state)
}

func (t *unstableTask) Run(ctx context.Context, execCtx tasks.ExecutionContext) error {
	if t.state.Value == t.request.Value {
		return nil
	}

	t.state.Value++

	err := execCtx.SaveState(ctx)
	if err != nil {
		return err
	}

	return errors.NewRetriableError(assert.AnError)
}

func (t *unstableTask) Cancel(ctx context.Context, execCtx tasks.ExecutionContext) error {
	return nil
}

func (t *unstableTask) GetMetadata(
	ctx context.Context,
	taskID string,
) (proto.Message, error) {

	return &empty.Empty{}, nil
}

func (t *unstableTask) GetResponse() proto.Message {
	return t.state
}

func registerUnstableTask(registry *tasks.Registry) error {
	return registry.RegisterForExecution("unstable", func() tasks.Task {
		return &unstableTask{}
	})
}

func scheduleUnstableTask(
	ctx context.Context,
	scheduler tasks.Scheduler,
	failuresUntilSuccess uint64,
) (string, error) {

	return scheduler.ScheduleTask(
		ctx,
		"unstable",
		"Unstable task",
		&wrappers.UInt64Value{
			Value: failuresUntilSuccess,
		},
		"",
		"",
	)
}

////////////////////////////////////////////////////////////////////////////////

type failureTask struct {
	failure error
}

func (t *failureTask) Save() ([]byte, error) {
	return nil, nil
}

func (t *failureTask) Load(_, _ []byte) error {
	return nil
}

func (t *failureTask) Run(ctx context.Context, execCtx tasks.ExecutionContext) error {
	return t.failure
}

func (*failureTask) Cancel(ctx context.Context, execCtx tasks.ExecutionContext) error {
	return nil
}

func (t *failureTask) GetMetadata(
	ctx context.Context,
	taskID string,
) (proto.Message, error) {

	return &empty.Empty{}, nil
}

func (t *failureTask) GetResponse() proto.Message {
	return &empty.Empty{}
}

func registerFailureTask(registry *tasks.Registry, failure error) error {
	return registry.RegisterForExecution("failure", func() tasks.Task {
		return &failureTask{failure: failure}
	})
}

func scheduleFailureTask(
	ctx context.Context,
	scheduler tasks.Scheduler,
) (string, error) {

	return scheduler.ScheduleTask(
		ctx,
		"failure",
		"Failure task",
		&empty.Empty{},
		"",
		"",
	)
}

////////////////////////////////////////////////////////////////////////////////

type sixTimesTask struct {
	scheduler tasks.Scheduler
	request   *wrappers.UInt64Value
	state     *wrappers.UInt64Value
}

func (t *sixTimesTask) Save() ([]byte, error) {
	return proto.Marshal(t.state)
}

func (t *sixTimesTask) Load(request, state []byte) error {
	t.request = &wrappers.UInt64Value{}
	err := proto.Unmarshal(request, t.request)
	if err != nil {
		return err
	}

	t.state = &wrappers.UInt64Value{}
	return proto.Unmarshal(state, t.state)
}

func (t *sixTimesTask) Run(ctx context.Context, execCtx tasks.ExecutionContext) error {
	id, err := scheduleDoublerTask(
		headers.SetIncomingIdempotencyKey(ctx, execCtx.GetTaskID()),
		t.scheduler,
		t.request.Value,
	)
	if err != nil {
		return err
	}

	response, err := t.scheduler.WaitTask(ctx, execCtx, id)
	if err != nil {
		return err
	}

	res := response.(*wrappers.UInt64Value).GetValue()
	t.state.Value = res * 3
	return nil
}

func (t *sixTimesTask) Cancel(ctx context.Context, execCtx tasks.ExecutionContext) error {
	return nil
}

func (t *sixTimesTask) GetMetadata(
	ctx context.Context,
	taskID string,
) (proto.Message, error) {

	return &empty.Empty{}, nil
}

func (t *sixTimesTask) GetResponse() proto.Message {
	return &wrappers.UInt64Value{
		Value: t.state.Value,
	}
}

func registerSixTimesTask(
	registry *tasks.Registry,
	scheduler tasks.Scheduler,
) error {

	return registry.RegisterForExecution("sixTimes", func() tasks.Task {
		return &sixTimesTask{
			scheduler: scheduler,
		}
	})
}

func scheduleSixTimesTask(
	ctx context.Context,
	scheduler tasks.Scheduler,
	request uint64,
) (string, error) {

	return scheduler.ScheduleTask(ctx, "sixTimes", "SixTimes task", &wrappers.UInt64Value{
		Value: request,
	}, "", "")
}

////////////////////////////////////////////////////////////////////////////////

var regularTaskMutex sync.Mutex
var regularTaskCounter int

type regularTask struct {
}

func (t *regularTask) Load(_, _ []byte) error {
	return nil
}

func (t *regularTask) Save() ([]byte, error) {
	return nil, nil
}

func (t *regularTask) Run(ctx context.Context, execCtx tasks.ExecutionContext) error {
	regularTaskMutex.Lock()
	regularTaskCounter++
	regularTaskMutex.Unlock()
	return nil
}

func (t *regularTask) Cancel(ctx context.Context, execCtx tasks.ExecutionContext) error {
	return nil
}

func (t *regularTask) GetMetadata(
	ctx context.Context,
	taskID string,
) (proto.Message, error) {

	return &empty.Empty{}, nil
}

func (t *regularTask) GetResponse() proto.Message {
	return &empty.Empty{}
}

////////////////////////////////////////////////////////////////////////////////

var defaultTimeout = 10 * time.Minute

func waitTaskWithTimeout(
	ctx context.Context,
	scheduler tasks.Scheduler,
	id string,
	timeout time.Duration,
) (uint64, error) {

	response, err := scheduler.WaitTaskSync(ctx, id, timeout)
	if err != nil {
		return 0, err
	}

	return response.(*wrappers.UInt64Value).GetValue(), nil
}

func waitTask(
	ctx context.Context,
	scheduler tasks.Scheduler,
	id string,
) (uint64, error) {

	return waitTaskWithTimeout(ctx, scheduler, id, defaultTimeout)
}

////////////////////////////////////////////////////////////////////////////////

func TestTasksInitInfra(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = s.startRunners(ctx)
	require.NoError(t, err)
}

func TestTasksRunningOneTask(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = registerDoublerTask(s.registry)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id, err := scheduleDoublerTask(reqCtx, s.scheduler, 123)
	require.NoError(t, err)

	response, err := waitTask(ctx, s.scheduler, id)
	require.NoError(t, err)
	require.EqualValues(t, 2*123, response)
}

func TestTasksRunningLimit(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 10*inflightLongTaskPerNodeLimit)

	err = registerLongTask(s.registry)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	tasksIds := []string{}
	scheduledLongTaskCount := 3 * inflightLongTaskPerNodeLimit
	for i := 0; i < scheduledLongTaskCount; i++ {
		reqCtx := getRequestContext(t, ctx)
		id, err := scheduleLongTask(reqCtx, s.scheduler)
		require.NoError(t, err)

		tasksIds = append(tasksIds, id)
	}

	endedLongTaskCount := 0
	errs := make(chan error)
	for _, id := range tasksIds {
		go func(id string) {
			_, err := waitTask(ctx, s.scheduler, id)

			errs <- err
		}(id)
	}

	ticker := time.NewTicker(20 * time.Millisecond)

	for {
		select {
		case <-ticker.C:
			runningTasks, _ := s.storage.ListTasksRunning(ctx, uint64(scheduledLongTaskCount))
			require.NoError(t, err)

			runningLongTaskCount := 0
			for _, task := range runningTasks {
				if task.TaskType == "long" {
					runningLongTaskCount++
				}
			}
			require.LessOrEqual(t, runningLongTaskCount, inflightLongTaskPerNodeLimit)
		case err := <-errs:
			require.NoError(t, err)
			endedLongTaskCount++

			if endedLongTaskCount == scheduledLongTaskCount {
				return
			}
		}
	}
}

func TestTasksSendEvent(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = registerDoublerTask(s.registry)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id, err := scheduleDoublerTask(reqCtx, s.scheduler, 123)
	require.NoError(t, err)

	taskState, err := s.storage.GetTask(ctx, id)
	require.NoError(t, err)

	err = s.scheduler.SendEvent(ctx, id, 10)
	require.NoError(t, err)

	// Events should be unique.
	err = s.scheduler.SendEvent(ctx, id, 10)
	require.NoError(t, err)

	// Should not update "events" field in task state.
	taskState, err = s.storage.UpdateTask(ctx, taskState)
	require.NoError(t, err)
	require.EqualValues(t, []int64{10}, taskState.Events)

	err = s.scheduler.SendEvent(ctx, id, 11)
	require.NoError(t, err)

	// UpdateTask should return up-to-date Events value.
	taskState, err = s.storage.UpdateTask(ctx, taskState)
	require.NoError(t, err)
	require.EqualValues(t, []int64{10, 11}, taskState.Events)
}

func TestTasksShouldNotRunTasksThatWereNotRegisteredForExecution(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = registerDoublerTask(s.registry)
	require.NoError(t, err)

	err = s.registry.Register("sixTimes", func() tasks.Task {
		return &sixTimesTask{}
	})
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id, err := scheduleDoublerTask(reqCtx, s.scheduler, 123)
	require.NoError(t, err)

	response, err := waitTask(ctx, s.scheduler, id)
	require.NoError(t, err)
	require.EqualValues(t, 2*123, response)

	// sixTimes wasn't registered for execution. Shouldn't be executed
	reqCtx = getRequestContext(t, ctx)
	id, err = scheduleSixTimesTask(reqCtx, s.scheduler, 100)
	require.NoError(t, err)

	_, err = waitTaskWithTimeout(
		ctx,
		s.scheduler,
		id,
		10*time.Second,
	)
	require.Error(t, err)
	require.True(t, errors.Is(err, errors.NewEmptyNonRetriableError()))
}

func TestTasksShouldRestoreRunningAfterRetriableError(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = registerUnstableTask(s.registry)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id, err := scheduleUnstableTask(
		reqCtx,
		s.scheduler,
		newDefaultConfig().GetMaxRetriableErrorCount(),
	)
	require.NoError(t, err)

	response, err := waitTask(ctx, s.scheduler, id)
	require.NoError(t, err)
	require.EqualValues(t, 2, response)
}

func TestTasksShouldFailRunningAfterRetriableErrorCountExceeded(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = registerUnstableTask(s.registry)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id, err := scheduleUnstableTask(
		reqCtx,
		s.scheduler,
		newDefaultConfig().GetMaxRetriableErrorCount()+1,
	)
	require.NoError(t, err)

	_, err = waitTask(ctx, s.scheduler, id)
	require.Error(t, err)

	expected := errors.NewRetriableError(assert.AnError)

	status, ok := grpc_status.FromError(err)
	require.True(t, ok)
	require.Equal(t, expected.Error(), status.Message())
}

func TestTasksShouldNotRestoreRunningAfterNonRetriableError(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	failure := errors.NewNonRetriableError(assert.AnError)

	err = registerFailureTask(s.registry, failure)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id, err := scheduleFailureTask(reqCtx, s.scheduler)
	require.NoError(t, err)

	_, err = waitTask(ctx, s.scheduler, id)
	require.Error(t, err)

	status, ok := grpc_status.FromError(err)
	require.True(t, ok)
	require.Equal(t, failure.Error(), status.Message())
}

func TestTasksRunningTwoConcurrentTasks(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = registerDoublerTask(s.registry)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id1, err := scheduleDoublerTask(reqCtx, s.scheduler, 123)
	require.NoError(t, err)

	reqCtx = getRequestContext(t, ctx)
	id2, err := scheduleDoublerTask(reqCtx, s.scheduler, 456)
	require.NoError(t, err)

	response, err := waitTask(ctx, s.scheduler, id1)
	require.NoError(t, err)
	require.EqualValues(t, 2*123, response)

	response, err = waitTask(ctx, s.scheduler, id2)
	require.NoError(t, err)
	require.EqualValues(t, 2*456, response)
}

func TestTasksRunningTwoConcurrentTasksReverseWaiting(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = registerDoublerTask(s.registry)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id1, err := scheduleDoublerTask(reqCtx, s.scheduler, 123)
	require.NoError(t, err)

	reqCtx = getRequestContext(t, ctx)
	id2, err := scheduleDoublerTask(reqCtx, s.scheduler, 456)
	require.NoError(t, err)

	response, err := waitTask(ctx, s.scheduler, id2)
	require.NoError(t, err)
	require.EqualValues(t, 2*456, response)

	response, err = waitTask(ctx, s.scheduler, id1)
	require.NoError(t, err)
	require.EqualValues(t, 2*123, response)
}

func TestTasksRunningTwoConcurrentTasksOnOneRunner(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 1)

	err = registerDoublerTask(s.registry)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id1, err := scheduleDoublerTask(reqCtx, s.scheduler, 123)
	require.NoError(t, err)

	reqCtx = getRequestContext(t, ctx)
	id2, err := scheduleDoublerTask(reqCtx, s.scheduler, 456)
	require.NoError(t, err)

	response, err := waitTask(ctx, s.scheduler, id1)
	require.NoError(t, err)
	require.EqualValues(t, 2*123, response)

	response, err = waitTask(ctx, s.scheduler, id2)
	require.NoError(t, err)
	require.EqualValues(t, 2*456, response)
}

func TestTasksRunningTwoConcurrentTasksOnOneRunnerReverseWaiting(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 1)

	err = registerDoublerTask(s.registry)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id1, err := scheduleDoublerTask(reqCtx, s.scheduler, 123)
	require.NoError(t, err)

	reqCtx = getRequestContext(t, ctx)
	id2, err := scheduleDoublerTask(reqCtx, s.scheduler, 456)
	require.NoError(t, err)

	response, err := waitTask(ctx, s.scheduler, id2)
	require.NoError(t, err)
	require.EqualValues(t, 2*456, response)

	response, err = waitTask(ctx, s.scheduler, id1)
	require.NoError(t, err)
	require.EqualValues(t, 2*123, response)
}

func TestTasksRunningDependentTask(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = registerDoublerTask(s.registry)
	require.NoError(t, err)

	err = registerSixTimesTask(s.registry, s.scheduler)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id, err := scheduleSixTimesTask(reqCtx, s.scheduler, 123)
	require.NoError(t, err)

	response, err := waitTask(ctx, s.scheduler, id)
	require.NoError(t, err)
	require.EqualValues(t, 6*123, response)
}

func TestTasksRunningDependentTaskOnOneRunner(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 1)

	err = registerDoublerTask(s.registry)
	require.NoError(t, err)

	err = registerSixTimesTask(s.registry, s.scheduler)
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	reqCtx := getRequestContext(t, ctx)
	id, err := scheduleSixTimesTask(reqCtx, s.scheduler, 123)
	require.NoError(t, err)

	response, err := waitTask(ctx, s.scheduler, id)

	require.NoError(t, err)
	require.EqualValues(t, 6*123, response)
}

func TestTasksRunningRegularTasks(t *testing.T) {
	ctx, cancel := context.WithCancel(newContext())
	defer cancel()

	db, err := newYDB(ctx)
	require.NoError(t, err)
	defer db.Close(ctx)

	s := createServices(t, ctx, db, 2)

	err = s.registry.RegisterForExecution("regular", func() tasks.Task {
		return &regularTask{}
	})
	require.NoError(t, err)

	err = s.startRunners(ctx)
	require.NoError(t, err)

	regularTaskMutex.Lock()
	regularTaskCounter = 0
	regularTaskMutex.Unlock()

	s.scheduler.ScheduleRegularTasks(ctx, "regular", "Regular task", time.Millisecond, 2)

	for {
		<-time.After(10 * time.Millisecond)

		regularTaskMutex.Lock()

		if regularTaskCounter > 4 {
			regularTaskMutex.Unlock()
			break
		}

		regularTaskMutex.Unlock()
	}
}
