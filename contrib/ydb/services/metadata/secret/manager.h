#pragma once
#include "snapshot.h"
#include <contrib/ydb/services/metadata/abstract/common.h>
#include <contrib/ydb/services/metadata/manager/generic_manager.h>
#include <contrib/ydb/library/accessor/accessor.h>

namespace NKikimr::NMetadata::NSecret {

class TSecretManager: public NModifications::TGenericOperationsManager<TSecret> {
protected:
    virtual void DoPrepareObjectsBeforeModification(std::vector<TSecret>&& patchedObjects,
        NModifications::IAlterPreparationController<TSecret>::TPtr controller,
        const TInternalModificationContext& context) const override;

    virtual NModifications::TOperationParsingResult DoBuildPatchFromSettings(
        const NYql::TObjectSettingsImpl& settings, TInternalModificationContext& context) const override;

public:
};

class TAccessManager: public NModifications::TGenericOperationsManager<TAccess> {
protected:
    virtual void DoPrepareObjectsBeforeModification(std::vector<TAccess>&& patchedObjects,
        NModifications::IAlterPreparationController<TAccess>::TPtr controller,
        const TInternalModificationContext& context) const override;

    virtual NModifications::TOperationParsingResult DoBuildPatchFromSettings(const NYql::TObjectSettingsImpl& settings,
        TInternalModificationContext& context) const override;
public:
};

}
