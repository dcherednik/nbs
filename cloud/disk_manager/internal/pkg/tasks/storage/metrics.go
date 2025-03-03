package storage

import (
	"context"
	"sync"
	"time"

	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/accounting"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/logging"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/monitoring/metrics"
)

////////////////////////////////////////////////////////////////////////////////

type storageMetrics interface {
	OnTaskCreated(state TaskState, taskCount int)
	OnTaskUpdated(ctx context.Context, state TaskState)
}

////////////////////////////////////////////////////////////////////////////////

type taskMetrics struct {
	created      metrics.Counter
	timeTotal    metrics.Timer
	estimateMiss metrics.Timer
}

type storageMetricsImpl struct {
	registry metrics.Registry

	// By taskType
	taskMetrics       map[string]*taskMetrics
	tasksMetricsMutex sync.Mutex
}

func taskDurationBuckets() metrics.DurationBuckets {
	return metrics.NewDurationBuckets(
		5*time.Second, 10*time.Second, 30*time.Second,
		1*time.Minute, 2*time.Minute, 5*time.Minute,
		10*time.Minute, 30*time.Minute,
		1*time.Hour, 2*time.Hour, 5*time.Hour, 10*time.Hour,
	)
}

func (m *storageMetricsImpl) getOrNewMetrics(taskType string) *taskMetrics {
	m.tasksMetricsMutex.Lock()
	defer m.tasksMetricsMutex.Unlock()

	t, ok := m.taskMetrics[taskType]
	if !ok {
		subRegistry := m.registry.WithTags(map[string]string{
			"type": taskType,
		})

		t = &taskMetrics{
			created:      subRegistry.Counter("created"),
			timeTotal:    subRegistry.DurationHistogram("time/total", taskDurationBuckets()),
			estimateMiss: subRegistry.DurationHistogram("time/estimateMiss", taskDurationBuckets()),
		}
		m.taskMetrics[taskType] = t
	}
	return t
}

////////////////////////////////////////////////////////////////////////////////

func (m *storageMetricsImpl) OnTaskCreated(state TaskState, taskCount int) {
	metrics := m.getOrNewMetrics(state.TaskType)
	metrics.created.Add(int64(taskCount))

	accounting.OnTaskCreated(state.TaskType, state.CloudID, state.FolderID, taskCount)
}

func (m *storageMetricsImpl) OnTaskUpdated(
	ctx context.Context,
	state TaskState,
) {

	metrics := m.getOrNewMetrics(state.TaskType)
	if state.Status == TaskStatusFinished {
		metrics.timeTotal.RecordDuration(state.EndedAt.Sub(state.CreatedAt))

		// Check that EstimatedTime is located within range (CreatedAt, EndedAt).
		if state.EstimatedTime.After(state.CreatedAt) &&
			state.EstimatedTime.Before(state.EndedAt) {

			estimateMiss := state.EndedAt.Sub(state.EstimatedTime)
			metrics.estimateMiss.RecordDuration(estimateMiss)
			logging.Info(
				ctx,
				"Task estimated time debug. Task: %q, started: %q, estimate: %q, actual: %q, miss: %q",
				state.ID,
				state.CreatedAt,
				state.EstimatedTime,
				state.EndedAt,
				estimateMiss,
			)
		}
	}
}

////////////////////////////////////////////////////////////////////////////////

func newStorageMetrics(registry metrics.Registry) storageMetrics {
	return &storageMetricsImpl{
		registry:    registry,
		taskMetrics: make(map[string]*taskMetrics),
	}
}
