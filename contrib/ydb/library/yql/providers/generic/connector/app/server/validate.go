package server

import (
	"fmt"
	"regexp"

	api_common "github.com/ydb-platform/nbs/contrib/ydb/library/yql/providers/generic/connector/api/common"
	"github.com/ydb-platform/nbs/contrib/ydb/library/yql/providers/generic/connector/app/server/utils"
	api_service_protos "github.com/ydb-platform/nbs/contrib/ydb/library/yql/providers/generic/connector/libgo/service/protos"
	"github.com/ydb-platform/nbs/library/go/core/log"
)

func ValidateDescribeTableRequest(logger log.Logger, request *api_service_protos.TDescribeTableRequest) error {
	if err := validateDataSourceInstance(logger, request.GetDataSourceInstance()); err != nil {
		return fmt.Errorf("validate data source instance: %w", err)
	}

	if request.GetTable() == "" {
		return fmt.Errorf("empty table: %w", utils.ErrInvalidRequest)
	}

	if err := checkForInjections(request.GetTable()); err != nil {
		return fmt.Errorf("table field is invalid: %w", err)
	}

	return nil
}

func ValidateListSplitsRequest(logger log.Logger, request *api_service_protos.TListSplitsRequest) error {
	if len(request.Selects) == 0 {
		return fmt.Errorf("empty select list: %w", utils.ErrInvalidRequest)
	}

	for i, slct := range request.Selects {
		if err := validateSelect(logger, slct); err != nil {
			return fmt.Errorf("validate select %d: %w", i, err)
		}
	}

	return nil
}

func ValidateReadSplitsRequest(logger log.Logger, request *api_service_protos.TReadSplitsRequest) error {
	if err := validateDataSourceInstance(logger, request.GetDataSourceInstance()); err != nil {
		return fmt.Errorf("validate data source instance: %w", err)
	}

	return nil
}

func validateSelect(logger log.Logger, slct *api_service_protos.TSelect) error {
	if slct == nil {
		return fmt.Errorf("select is empty: %w", utils.ErrInvalidRequest)
	}

	if err := validateDataSourceInstance(logger, slct.GetDataSourceInstance()); err != nil {
		return fmt.Errorf("validate data source instance: %w", err)
	}

	return nil
}

func validateDataSourceInstance(logger log.Logger, dsi *api_common.TDataSourceInstance) error {
	if dsi.GetKind() == api_common.EDataSourceKind_DATA_SOURCE_KIND_UNSPECIFIED {
		return fmt.Errorf("empty kind: %w", utils.ErrInvalidRequest)
	}

	if dsi.Endpoint == nil {
		return fmt.Errorf("endpoint is empty: %w", utils.ErrInvalidRequest)
	}

	if dsi.Endpoint.Host == "" {
		return fmt.Errorf("endpoint.host is empty: %w", utils.ErrInvalidRequest)
	}

	if dsi.Endpoint.Port == 0 {
		return fmt.Errorf("endpoint.port is empty: %w", utils.ErrInvalidRequest)
	}

	if dsi.Database == "" {
		return fmt.Errorf("database field is empty: %w", utils.ErrInvalidRequest)
	}

	if err := checkForInjections(dsi.Database); err != nil {
		return fmt.Errorf("database field is invalid: %w", err)
	}

	if dsi.UseTls {
		logger.Info("connector will use secure connection to access data source")
	} else {
		logger.Warn("connector will use insecure connection to access data source")
	}

	if dsi.Protocol == api_common.EProtocol_PROTOCOL_UNSPECIFIED {
		return fmt.Errorf("protocol field is empty: %w", utils.ErrInvalidRequest)
	}

	switch dsi.GetKind() {
	case api_common.EDataSourceKind_POSTGRESQL:
		if dsi.GetPgOptions().Schema == "" {
			return fmt.Errorf("schema field is empty: %w", utils.ErrInvalidRequest)
		}

		if err := checkForInjections(dsi.GetPgOptions().Schema); err != nil {
			return fmt.Errorf("schema field is invalid: %w", err)
		}
	case api_common.EDataSourceKind_CLICKHOUSE:
		break
	default:
		return fmt.Errorf("unsupported data source: %w", utils.ErrInvalidRequest)
	}

	return nil
}

// TODO: improve during https://st.yandex-team.ru/YQ-2430
func checkForInjections(field string) error {
	pattern, err := regexp.Compile("[a-zA-Z0-9_]+")

	if err != nil {
		return fmt.Errorf("build regexp: %w", err)
	}

	if !pattern.MatchString(field) {
		return fmt.Errorf("potential injection: %w", utils.ErrInvalidRequest)
	}

	return nil
}
