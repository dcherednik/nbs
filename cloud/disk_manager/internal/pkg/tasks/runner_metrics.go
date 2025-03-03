package tasks

import (
	"bytes"
	"context"
	"runtime/pprof"
	"sync"
	"time"

	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/common"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/logging"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/monitoring/metrics"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks/errors"
	tasks_storage "github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks/storage"
)

////////////////////////////////////////////////////////////////////////////////

const (
	checkTaskHangingPeriod = 15 * time.Second
)

////////////////////////////////////////////////////////////////////////////////

const (
	// To limit printing frequency.
	stackTracesPrintingCooldown = 10 * time.Minute
)

var stackTracesPrintedAtMutex sync.Mutex
var stackTracesPrintedAt time.Time

func printStackTraces() string {
	stackTracesPrintedAtMutex.Lock()
	defer stackTracesPrintedAtMutex.Unlock()

	if time.Since(stackTracesPrintedAt) < stackTracesPrintingCooldown {
		return "printing throttled"
	}

	var stackTraces bytes.Buffer
	_ = pprof.Lookup("goroutine").WriteTo(&stackTraces, 1)
	stackTracesPrintedAt = time.Now()

	return stackTraces.String()
}

////////////////////////////////////////////////////////////////////////////////

type runnerMetrics interface {
	OnExecutionStarted(state tasks_storage.TaskState)
	OnExecutionStopped()
	OnExecutionError(err error)
	OnError(err error)
}

////////////////////////////////////////////////////////////////////////////////

type taskMetrics struct {
	publicErrorsCounter          metrics.Counter
	wrongGenerationErrorsCounter metrics.Counter
	retriableErrorsCounter       metrics.Counter
	nonRetriableErrorsCounter    metrics.Counter
	nonCancellableErrorsCounter  metrics.Counter
	panicCounter                 metrics.Counter
	hangingTasksGauge            metrics.Gauge
	isTaskHanging                bool
	inflightTasksGauge           metrics.Gauge
	taskID                       string
	taskType                     string
}

////////////////////////////////////////////////////////////////////////////////

type runnerMetricsImpl struct {
	registry                     metrics.Registry
	otherErrorsCounter           metrics.Counter
	wrongGenerationErrorsCounter metrics.Counter
	hangingTaskTimeout           time.Duration
	exceptHangingTaskTypes       []string
	taskMetrics                  *taskMetrics
	taskMetricsMutex             sync.Mutex
	onExecutionStopped           func()
	logger                       logging.Logger
}

func (m *runnerMetricsImpl) OnExecutionStarted(state tasks_storage.TaskState) {
	m.taskMetricsMutex.Lock()
	defer m.taskMetricsMutex.Unlock()

	subRegistry := m.registry.WithTags(map[string]string{
		"type": state.TaskType,
	})

	m.taskMetrics = &taskMetrics{
		publicErrorsCounter:          subRegistry.Counter("errors/public"),
		panicCounter:                 subRegistry.Counter("errors/panic"),
		wrongGenerationErrorsCounter: subRegistry.Counter("errors/wrongGeneration"),
		retriableErrorsCounter:       subRegistry.Counter("errors/retriable"),
		nonRetriableErrorsCounter:    subRegistry.Counter("errors/nonRetriable"),
		nonCancellableErrorsCounter:  subRegistry.Counter("errors/nonCancellable"),
		hangingTasksGauge:            subRegistry.Gauge("hangingTasks"),
		inflightTasksGauge:           subRegistry.Gauge("inflightTasks"),
		taskID:                       state.ID,
		taskType:                     state.TaskType,
	}

	ctx, cancel := context.WithCancel(context.Background())
	m.onExecutionStopped = cancel

	// Should not report some tasks as hanging (NBS-4341).
	if !common.Find(m.exceptHangingTaskTypes, state.TaskType) {
		hangingDeadline := state.CreatedAt.Add(m.hangingTaskTimeout)

		go func() {
			for {
				select {
				case <-ctx.Done():
					return
				case <-time.After(checkTaskHangingPeriod):
				}

				m.checkTaskHanging(ctx, hangingDeadline)
			}
		}()

		m.checkTaskHangingImpl(hangingDeadline)
	}

	m.taskMetrics.inflightTasksGauge.Add(1)
}

func (m *runnerMetricsImpl) OnExecutionStopped() {
	m.taskMetricsMutex.Lock()
	defer m.taskMetricsMutex.Unlock()

	if m.taskMetrics == nil {
		// Nothing to do.
		return
	}

	m.setTaskHanging(false)
	m.taskMetrics.inflightTasksGauge.Add(-1)

	m.taskMetrics = nil
	m.onExecutionStopped()
}

func (m *runnerMetricsImpl) OnExecutionError(err error) {
	m.taskMetricsMutex.Lock()
	defer m.taskMetricsMutex.Unlock()

	if errors.IsPublic(err) {
		m.taskMetrics.publicErrorsCounter.Inc()
	} else if errors.IsPanicError(err) {
		m.taskMetrics.panicCounter.Inc()
	} else if errors.Is(err, errors.NewWrongGenerationError()) {
		m.taskMetrics.wrongGenerationErrorsCounter.Inc()
	} else if errors.Is(err, errors.NewInterruptExecutionError()) {
		// InterruptExecutionError is not a failure.
	} else if errors.Is(err, errors.NewEmptyNonCancellableError()) {
		m.taskMetrics.nonCancellableErrorsCounter.Inc()
	} else if errors.Is(err, errors.NewEmptyNonRetriableError()) {
		e := errors.NewEmptyNonRetriableError()
		errors.As(err, &e)

		if !e.Silent {
			m.taskMetrics.nonRetriableErrorsCounter.Inc()
		}
	} else if errors.Is(err, errors.NewEmptyRetriableError()) {
		m.taskMetrics.retriableErrorsCounter.Inc()
	} else if errors.Is(err, errors.NewEmptyDetailedError()) {
		e := errors.NewEmptyDetailedError()
		errors.As(err, &e)

		if !e.Silent {
			m.taskMetrics.nonRetriableErrorsCounter.Inc()
		}
	} else {
		// All other execution errors should be interpreted as non retriable.
		m.taskMetrics.nonRetriableErrorsCounter.Inc()
	}
}

func (m *runnerMetricsImpl) OnError(err error) {
	m.taskMetricsMutex.Lock()
	defer m.taskMetricsMutex.Unlock()

	if errors.Is(err, errors.NewWrongGenerationError()) {
		if m.taskMetrics != nil {
			m.taskMetrics.wrongGenerationErrorsCounter.Inc()
		} else {
			m.wrongGenerationErrorsCounter.Inc()
		}
	} else {
		// Other errors.
		m.otherErrorsCounter.Inc()
	}
}

////////////////////////////////////////////////////////////////////////////////

func (m *runnerMetricsImpl) setTaskHanging(value bool) {
	prevValue := m.taskMetrics.isTaskHanging
	m.taskMetrics.isTaskHanging = value

	gauge := m.taskMetrics.hangingTasksGauge
	switch {
	case !prevValue && value:
		if m.logger != nil {
			m.logger.Fmt().Infof(
				"Task %v with id %v is hanging, stack traces %v",
				m.taskMetrics.taskType,
				m.taskMetrics.taskID,
				printStackTraces(),
			)
		}

		gauge.Add(1)
	case prevValue && !value:
		gauge.Add(-1)
	}
}

func (m *runnerMetricsImpl) checkTaskHangingImpl(deadline time.Time) {
	if m.taskMetrics == nil {
		return
	}

	m.setTaskHanging(time.Now().After(deadline))
}

func (m *runnerMetricsImpl) checkTaskHanging(
	ctx context.Context,
	deadline time.Time,
) {

	m.taskMetricsMutex.Lock()
	defer m.taskMetricsMutex.Unlock()

	if ctx.Err() != nil {
		return
	}

	m.checkTaskHangingImpl(deadline)
}

////////////////////////////////////////////////////////////////////////////////

func newRunnerMetrics(
	ctx context.Context,
	registry metrics.Registry,
	hangingTaskTimeout time.Duration,
	exceptHangingTaskTypes []string,
) *runnerMetricsImpl {

	return &runnerMetricsImpl{
		registry:                     registry,
		otherErrorsCounter:           registry.Counter("errors/other"),
		wrongGenerationErrorsCounter: registry.Counter("errors/wrongGeneration"),
		hangingTaskTimeout:           hangingTaskTimeout,
		exceptHangingTaskTypes:       exceptHangingTaskTypes,
		onExecutionStopped:           func() {},
		logger:                       logging.GetLogger(ctx),
	}
}
