package mocks

import (
	"context"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/stretchr/testify/mock"
	"github.com/ydb-platform/nbs/cloud/disk_manager/api/operation"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks"
)

////////////////////////////////////////////////////////////////////////////////

type SchedulerMock struct {
	mock.Mock
}

func (s *SchedulerMock) ScheduleTask(
	ctx context.Context,
	taskType string,
	description string,
	request proto.Message,
	cloudID string,
	folderID string,
) (string, error) {

	args := s.Called(
		ctx,
		taskType,
		description,
		request,
		cloudID,
		folderID,
	)
	return args.String(0), args.Error(1)
}

func (s *SchedulerMock) ScheduleZonalTask(
	ctx context.Context,
	taskType string,
	description string,
	zoneID string,
	request proto.Message,
	cloudID string,
	folderID string,
) (string, error) {

	args := s.Called(
		ctx,
		taskType,
		description,
		zoneID,
		request,
		cloudID,
		folderID,
	)
	return args.String(0), args.Error(1)
}

func (s *SchedulerMock) ScheduleRegularTasks(
	ctx context.Context,
	taskType string,
	description string,
	scheduleInterval time.Duration,
	maxTasksInflight int,
) {

	s.Called(ctx, taskType, description, scheduleInterval, maxTasksInflight)
}

func (s *SchedulerMock) CancelTask(
	ctx context.Context,
	taskID string,
) (bool, error) {

	args := s.Called(ctx, taskID)
	return args.Bool(0), args.Error(1)
}

func (s *SchedulerMock) WaitTask(
	ctx context.Context,
	execCtx tasks.ExecutionContext,
	taskID string,
) (proto.Message, error) {

	args := s.Called(ctx, execCtx, taskID)
	res, _ := args.Get(0).(proto.Message)
	return res, args.Error(1)
}

func (s *SchedulerMock) WaitAnyTasks(
	ctx context.Context,
	taskIDs []string,
) ([]string, error) {

	args := s.Called(ctx, taskIDs)
	res, _ := args.Get(0).([]string)
	return res, args.Error(1)
}

func (s *SchedulerMock) WaitTaskEnded(
	ctx context.Context,
	taskID string,
) error {

	args := s.Called(ctx, taskID)
	return args.Error(0)
}

func (s *SchedulerMock) GetTaskMetadata(
	ctx context.Context,
	taskID string,
) (proto.Message, error) {

	args := s.Called(ctx, taskID)
	res, _ := args.Get(0).(proto.Message)
	return res, args.Error(1)
}

func (s *SchedulerMock) SendEvent(
	ctx context.Context,
	taskID string,
	event int64,
) error {

	args := s.Called(ctx, taskID, event)
	return args.Error(0)
}

func (s *SchedulerMock) GetOperationProto(
	ctx context.Context,
	taskID string,
) (*operation.Operation, error) {

	args := s.Called(ctx, taskID)
	res, _ := args.Get(0).(*operation.Operation)
	return res, args.Error(1)
}

func (s *SchedulerMock) WaitTaskSync(
	ctx context.Context,
	taskID string,
	timeout time.Duration,
) (proto.Message, error) {

	args := s.Called(ctx, taskID, timeout)
	res, _ := args.Get(0).(proto.Message)
	return res, args.Error(1)
}

func (s *SchedulerMock) ScheduleBlankTask(
	ctx context.Context,
) (string, error) {

	args := s.Called(ctx)
	return args.String(0), args.Error(1)
}

////////////////////////////////////////////////////////////////////////////////

func NewSchedulerMock() *SchedulerMock {
	return &SchedulerMock{}
}

////////////////////////////////////////////////////////////////////////////////

// Ensure that SchedulerMock implements Scheduler.
func assertSchedulerMockIsScheduler(arg *SchedulerMock) tasks.Scheduler {
	return arg
}
