package mocks

import (
	"context"
	"time"

	"github.com/stretchr/testify/mock"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks"
)

////////////////////////////////////////////////////////////////////////////////

type ExecutionContextMock struct {
	mock.Mock
}

func (c *ExecutionContextMock) SaveState(ctx context.Context) error {
	args := c.Called(ctx)
	return args.Error(0)
}

func (c *ExecutionContextMock) GetTaskType() string {
	args := c.Called()
	return args.String(0)
}

func (c *ExecutionContextMock) GetTaskID() string {
	args := c.Called()
	return args.String(0)
}

func (c *ExecutionContextMock) AddTaskDependency(
	ctx context.Context,
	taskID string,
) error {

	args := c.Called(ctx, taskID)
	return args.Error(0)
}

func (c *ExecutionContextMock) SetEstimate(estimatedDuration time.Duration) {
	c.Called(estimatedDuration)
}

func (c *ExecutionContextMock) HasEvent(ctx context.Context, event int64) bool {
	args := c.Called(ctx, event)
	return args.Bool(0)
}

////////////////////////////////////////////////////////////////////////////////

func NewExecutionContextMock() *ExecutionContextMock {
	return &ExecutionContextMock{}
}

////////////////////////////////////////////////////////////////////////////////

// Ensure that ExecutionContextMock implements ExecutionContext.
func assertExecutionContextMockIsExecutionContext(
	arg *ExecutionContextMock,
) tasks.ExecutionContext {

	return arg
}
