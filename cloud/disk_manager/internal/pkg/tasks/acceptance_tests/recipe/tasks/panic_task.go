package tasks

import (
	"context"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/golang/protobuf/ptypes/empty"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/common"
	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/tasks"
)

////////////////////////////////////////////////////////////////////////////////

type PanicTask struct{}

func (t *PanicTask) Save() ([]byte, error) {
	return nil, nil
}

func (t *PanicTask) Load(request, state []byte) error {
	return nil
}

func (t *PanicTask) Run(
	ctx context.Context,
	execCtx tasks.ExecutionContext,
) error {

	<-time.After(common.RandomDuration(time.Millisecond, 5*time.Second))
	panic("test panic")
}

func (t *PanicTask) Cancel(
	ctx context.Context,
	execCtx tasks.ExecutionContext,
) error {

	return nil
}

func (t *PanicTask) GetMetadata(
	ctx context.Context,
	taskID string,
) (proto.Message, error) {

	return &empty.Empty{}, nil
}

func (t *PanicTask) GetResponse() proto.Message {
	return &empty.Empty{}
}

////////////////////////////////////////////////////////////////////////////////

func NewPanicTask() *PanicTask {
	return &PanicTask{}
}
