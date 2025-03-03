package logging

import (
	"context"
	"time"

	"github.com/ydb-platform/nbs/cloud/disk_manager/internal/pkg/headers"
	"github.com/ydb-platform/nbs/library/go/core/log"
	"github.com/ydb-platform/nbs/library/go/core/log/ctxlog"
)

////////////////////////////////////////////////////////////////////////////////

type Field = log.Field

////////////////////////////////////////////////////////////////////////////////

func String(key, value string) Field {
	return log.String(key, value)
}

func Strings(key string, value []string) Field {
	return log.Strings(key, value)
}

func Bool(key string, value bool) Field {
	return log.Bool(key, value)
}

func Int(key string, value int) Field {
	return log.Int(key, value)
}

func Int64(key string, value int64) Field {
	return log.Int64(key, value)
}

func Duration(key string, value time.Duration) Field {
	return log.Duration(key, value)
}

func ErrorValue(value error) Field {
	return log.Error(value)
}

func Any(key string, value interface{}) Field {
	return log.Any(key, value)
}

////////////////////////////////////////////////////////////////////////////////

func SetLoggingFields(ctx context.Context) context.Context {
	fields := make([]log.Field, 0)

	idempotencyKey := headers.GetIdempotencyKey(ctx)
	if len(idempotencyKey) != 0 {
		fields = append(fields, log.String("IDEMPOTENCY_KEY", idempotencyKey))
	}

	requestID := headers.GetRequestID(ctx)
	if len(requestID) != 0 {
		fields = append(fields, log.String("REQUEST_ID", requestID))
	}

	operationID := headers.GetOperationID(ctx)
	if len(operationID) != 0 {
		fields = append(fields, log.String("OPERATION_ID", operationID))
	}

	fields = append(fields, log.String("SYSLOG_IDENTIFIER", "disk-manager"))

	return ctxlog.WithFields(
		ctx,
		fields...,
	)
}
