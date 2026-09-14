#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "SFPExportChatCommand.generated.h"

/** In-game entry point: /sfpplanner open|close|export|hotkey [KEY|off|reset] */
UCLASS()
class SFPFACTORYPLANNER_API ASFPExportChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	ASFPExportChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(
		UCommandSender* Sender,
		const TArray<FString>& Arguments,
		const FString& Label) override;
};
